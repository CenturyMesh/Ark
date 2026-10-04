#include "ark/agent.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/mesh_model.hpp"
#include "ark/io.hpp"
#include "ark/compute.hpp"
#include "ark/notch.hpp"
#include "ark/step_export.hpp"
#include "ark/source_tube.hpp"
#include "ark/sketch.hpp"
#include "ark/onboarding.hpp"
#include "ark/native_backend.hpp"
#include "ark/native_features.hpp"
#include "ark/references.hpp"
#include "ark/assembly_io.hpp"
#include "ark/sheet_commands.hpp"
#include "ark/sheet_preview.hpp"
#include <stdexcept>
#include <set>
#include <map>
#include <cmath>
#include <fstream>
#include <filesystem>
namespace ark {
using V=arkjson::Value;
namespace {
const V& field(const V& a,const char* k){auto p=a.find(k);if(!p)throw std::runtime_error(std::string("Missing field: ")+k);return *p;}
std::string str(const V& a,const char* k){auto& p=field(a,k);if(p.t!=V::Str||p.str.empty())throw std::runtime_error(std::string("Expected nonempty string: ")+k);return p.str;}
u64 id(const V& v){if(v.t!=V::Num||!std::isfinite(v.num)||v.num<1||v.num>9007199254740991.||v.num!=std::floor(v.num))throw std::runtime_error("Expected positive integer id");return (u64)v.num;}
std::string expression(const V& v){if(v.t==V::Str && v.str.size()<=2048)return v.str;if(v.t==V::Num&&std::isfinite(v.num))return v.dump();throw std::runtime_error("Expected expression string or finite number");}
std::string absolutePath(const std::string& name){auto path=std::filesystem::absolute(std::filesystem::u8path(name));auto utf=path.u8string();return std::string(utf.begin(),utf.end());}
void params(Feature& f,const V& p){if(p.t!=V::Obj)throw std::runtime_error("p must be an object");for(auto& kv:p.obj)f.p[kv.first]=expression(kv.second);}
void bends(Feature& f,const V& list){
    if(f.type!=FeatureType::Tube||list.t!=V::Arr||list.arr.size()>64)throw std::runtime_error("bends requires a tube and 0..64 bend objects");
    f.bends.clear();for(auto& j:list.arr){if(j.t!=V::Obj)throw std::runtime_error("bend must be object");TubeBend b;
        for(auto pair:std::vector<std::pair<const char*,std::string*>>{{"feed",&b.feed},{"radius",&b.radius},{"angle",&b.angle},{"roll",&b.roll}})if(auto p=j.find(pair.first))*pair.second=expression(*p);
        f.bends.push_back(b);
    }
}
void notches(Feature& f,const V& list){
    if(f.type!=FeatureType::NotchedTube||list.t!=V::Arr||list.arr.size()>64)throw std::runtime_error("notches requires notched_tube and 0..64 points");
    f.notches.clear();for(auto& j:list.arr){NotchPoint p;p.station=expression(field(j,"station"));p.angle=expression(field(j,"angle"));p.face=str(j,"face");f.notches.push_back(p);}
}
u64 namedFeature(const Document& d,const std::string& name){u64 found=0;int count=0;for(const auto& feature:d.features)if(feature.name==name){found=feature.id;++count;}if(count!=1)throw std::runtime_error(count?"Ambiguous feature name: "+name:"Feature name not found: "+name);return found;}
u64 featureRef(const Document& d,const V& value){if(value.t==V::Num)return id(value);if(value.t==V::Str)return namedFeature(d,value.str);throw std::runtime_error("Feature reference must be an id or a unique name");}
u64 namedNode(const Document& d,const std::string& name){u64 found=0;int count=0;for(const auto& node:d.assembly)if(node.name==name){found=node.id;++count;}if(count!=1)throw std::runtime_error(count?"Ambiguous assembly instance name: "+name:"Assembly instance name not found: "+name);return found;}
void bindEndpoint(const Document& d,V& mate,const char* key){auto endpoint=mate.find(key);if(!endpoint||endpoint->t!=V::Str)return;mate.set(key,V::makeNum((double)namedNode(d,endpoint->str)));}
void editNode(AssemblyNode& n,const V& j,const Document& doc){
    if(auto p=j.find("parent"))n.parent=p->t==V::Num&&p->num==0?0:id(*p);
    if(auto p=j.find("feature")){if(p->t==V::Num&&p->num==0)n.feature=0;else if(p->t==V::Str)n.feature=namedFeature(doc,p->str);else n.feature=id(*p);}
    if(auto p=j.find("visible")){if(p->t!=V::Bool)throw std::runtime_error("visible must be boolean");n.visible=p->b;}
    if(auto p=j.find("fixed")){if(p->t!=V::Bool)throw std::runtime_error("fixed must be boolean");n.fixed=p->b;}
    if(j.find("name"))n.name=str(j,"name");
    if(auto p=j.find("p")){if(p->t!=V::Obj)throw std::runtime_error("p must be object");for(auto& kv:p->obj)n.p[kv.first]=expression(kv.second);}
    if(auto pattern=j.find("pattern")){
        if(pattern->t==V::Null)n.pattern={};
        else if(pattern->t!=V::Obj)throw std::runtime_error("assembly pattern must be an object");
        else{
            auto kind=pattern->find("type");if(!kind||kind->t!=V::Str||(kind->str!="linear"&&kind->str!="circular"))throw std::runtime_error("instance pattern type must be linear or circular");
            n.pattern.active=true;n.pattern.type=kind->str;
            auto take=[&](const char* key,std::string& dest,const char* fallback){if(auto value=pattern->find(key))dest=expression(*value);else if(fallback)dest=fallback;else throw std::runtime_error(std::string("instance pattern needs ")+key);};
            take("count",n.pattern.count,nullptr);
            if(n.pattern.type=="circular"){take("step",n.pattern.step,"90");if(auto axis=pattern->find("axis")){if(axis->t!=V::Str||(axis->str!="X"&&axis->str!="Y"&&axis->str!="Z"))throw std::runtime_error("Circular instance pattern axis must be X, Y or Z");n.pattern.axis=axis->str;}else n.pattern.axis="Z";}
            else{take("dx",n.pattern.dx,"0");take("dy",n.pattern.dy,"0");take("dz",n.pattern.dz,"0");}
        }
    }
    if(n.pattern.active&&!n.feature)throw std::runtime_error("Instance pattern requires a part");
}
V result(V data,bool error=false){V r=V::makeObj(),content=V::makeArr(),item=V::makeObj();item.set("type",V::makeStr("text"));item.set("text",V::makeStr(data.dump()));content.arr.push_back(item);r.set("content",content);r.set("structuredContent",data);r.set("isError",V::makeBool(error));return r;}
void validated(Document& d){
    if(d.features.size()>1024||d.params.size()>256||d.sketches.size()>64||d.planes.size()>128)throw std::runtime_error("Document capacity exceeded");
    d.recompute();
    if(!d.parameterError.empty())throw std::runtime_error(d.parameterError);
    for(auto& r:d.lastResults)if(!r.ok)throw std::runtime_error("Feature #"+std::to_string(r.id)+": "+r.error);
    for(auto& sk:d.sketches)if(!sk.error.empty()||!std::isfinite(sk.residual)||sk.residual>1e-6)throw std::runtime_error("Sketch '"+sk.name+"': "+sk.error);
    std::vector<AssemblyPlacement> placements;std::string error;if(!resolveAssembly(d,placements,error))throw std::runtime_error(error);
}
std::set<u64> consumed(const Document& d){std::set<u64> out;for(auto& f:d.features)for(auto x:f.inputs)out.insert(x);return out;}
Mesh selectedMesh(Document& d,const V& args){
    Mesh m;
    u64 selected=args.find("id")?id(*args.find("id")):0;
    auto node=args.find("node");if(node&&selected)throw std::runtime_error("Select id or node, not both");
    if(!d.assembly.empty()&&!selected){
        std::vector<AssemblyPlacement> placements;std::string error;if(!resolveAssembly(d,placements,error))throw std::runtime_error(error);
        std::unordered_map<u64,Mesh> parts;
        for(auto& p:placements)if(p.visible&&(!node||p.node==id(*node))){
            auto part=parts.find(p.feature);if(part==parts.end())for(auto& b:d.evalBodies)if(b.id==p.feature){part=parts.emplace(p.feature,b.tessellate(d.tessOpt)).first;break;}
            if(part!=parts.end())m.append(part->second,p.world);
        }
    }else{
        if(node)throw std::runtime_error("No assembly instance selected");
        auto hidden=consumed(d);for(auto& b:d.evalBodies)if(selected?b.id==selected:!hidden.count(b.id))m.append(b.tessellate(d.tessOpt));
    }
    if(!m.triCount())throw std::runtime_error("No mesh for requested selection");
    return m;
}
Vec3 meshPoint(const Mesh& mesh,u32 index){return {mesh.pos[index*3],mesh.pos[index*3+1],mesh.pos[index*3+2]};}
Vec3 worldPoint(const Mesh& mesh,const Mat4& world,u32 index){return world.transformPoint(meshPoint(mesh,index));}
bool boxesOverlap(const AABB& a,const AABB& b){return a.valid()&&b.valid()&&a.mn.x<=b.mx.x&&b.mn.x<=a.mx.x&&a.mn.y<=b.mx.y&&b.mn.y<=a.mx.y&&a.mn.z<=b.mx.z&&b.mn.z<=a.mx.z;}
bool segmentHitsTriangle(Vec3 p,Vec3 q,Vec3 a,Vec3 b,Vec3 c){
    Vec3 dir=q-p,ab=b-a,ac=c-a,pvec=dir.cross(ac);double det=ab.dot(pvec);if(std::fabs(det)<1e-12)return false;double inv=1.0/det;Vec3 tvec=p-a;double u=tvec.dot(pvec)*inv;if(u<-1e-7||u>1+1e-7)return false;Vec3 qvec=tvec.cross(ab);double v=dir.dot(qvec)*inv;if(v<-1e-7||u+v>1+1e-7)return false;double t=ac.dot(qvec)*inv;return t>1e-5&&t<1.0-1e-5;
}
bool rayHitsTriangle(Vec3 origin,Vec3 dir,Vec3 a,Vec3 b,Vec3 c){
    Vec3 ab=b-a,ac=c-a,pvec=dir.cross(ac);double det=ab.dot(pvec);if(std::fabs(det)<1e-12)return false;double inv=1.0/det;Vec3 tvec=origin-a;double u=tvec.dot(pvec)*inv;if(u<-1e-7||u>1+1e-7)return false;Vec3 qvec=tvec.cross(ab);double v=dir.dot(qvec)*inv;if(v<-1e-7||u+v>1+1e-7)return false;return ac.dot(qvec)*inv>1e-4;
}
bool pointNearTriangle(Vec3 p,Vec3 a,Vec3 b,Vec3 c){
    Vec3 ab=b-a,ac=c-a,n=ab.cross(ac);double area2=n.lensq();if(area2<1e-24)return false;
    double dist=(p-a).dot(n);if(dist*dist>1e-6*area2)return false;
    double d00=ab.dot(ab),d01=ab.dot(ac),d11=ac.dot(ac),den=d00*d11-d01*d01;if(std::fabs(den)<1e-18)return false;
    Vec3 ap=p-a;double d20=ap.dot(ab),d21=ap.dot(ac),v=(d11*d20-d01*d21)/den,w=(d00*d21-d01*d20)/den,u=1-v-w;
    return u>=-1e-4&&v>=-1e-4&&w>=-1e-4;
}
bool insideMesh(const Mesh& mesh,const Mat4& world,Vec3 point){
    Mat4 inverse=world.inverted();Vec3 origin=inverse.transformPoint(point);
    for(size_t t=0;t<mesh.idx.size();t+=3)if(pointNearTriangle(origin,meshPoint(mesh,mesh.idx[t]),meshPoint(mesh,mesh.idx[t+1]),meshPoint(mesh,mesh.idx[t+2])))return false;
    Vec3 dir=inverse.transformDir(Vec3{0.31,0.47,0.82}).normalized();int hits=0;
    for(size_t t=0;t<mesh.idx.size();t+=3){Vec3 a=meshPoint(mesh,mesh.idx[t]),b=meshPoint(mesh,mesh.idx[t+1]),c=meshPoint(mesh,mesh.idx[t+2]);if(rayHitsTriangle(origin,dir,a,b,c))++hits;}
    return hits%2==1;
}
bool displayMeshesIntersect(const Mesh& a,const Mat4& wa,const Mesh& b,const Mat4& wb){
    auto centroid=[&](const Mesh& mesh,const Mat4& world){Vec3 sum{};size_t n=mesh.vertCount();if(!n)return sum;for(size_t i=0;i<n;++i)sum=sum+worldPoint(mesh,world,(u32)i);return sum*(1.0/n);};
    if(insideMesh(b,wb,centroid(a,wa))||insideMesh(a,wa,centroid(b,wb)))return true;
    auto triCenter=[&](const Mesh& mesh,const Mat4& world,size_t t){return (worldPoint(mesh,world,mesh.idx[t*3])+worldPoint(mesh,world,mesh.idx[t*3+1])+worldPoint(mesh,world,mesh.idx[t*3+2]))*(1.0/3.0);};
    for(size_t t=0;t<a.triCount();++t)if(insideMesh(b,wb,triCenter(a,wa,t)))return true;
    for(size_t t=0;t<b.triCount();++t)if(insideMesh(a,wa,triCenter(b,wb,t)))return true;
    for(size_t ta=0;ta<a.triCount();++ta)for(int edge=0;edge<3;++edge){Vec3 p=worldPoint(a,wa,a.idx[ta*3+edge]),q=worldPoint(a,wa,a.idx[ta*3+(edge+1)%3]);for(size_t tb=0;tb<b.triCount();++tb)if(segmentHitsTriangle(p,q,worldPoint(b,wb,b.idx[tb*3]),worldPoint(b,wb,b.idx[tb*3+1]),worldPoint(b,wb,b.idx[tb*3+2])))return true;}
    for(size_t tb=0;tb<b.triCount();++tb)for(int edge=0;edge<3;++edge){Vec3 p=worldPoint(b,wb,b.idx[tb*3+edge]),q=worldPoint(b,wb,b.idx[tb*3+(edge+1)%3]);for(size_t ta=0;ta<a.triCount();++ta)if(segmentHitsTriangle(p,q,worldPoint(a,wa,a.idx[ta*3]),worldPoint(a,wa,a.idx[ta*3+1]),worldPoint(a,wa,a.idx[ta*3+2])))return true;}
    return false;
}
V displayInterference(Document& d,const std::vector<AssemblyPlacement>& placements,bool force){
    V report=V::makeObj(),pairs=V::makeArr();report.set("interference_authority",V::makeStr("display_mesh"));
    std::vector<const AssemblyPlacement*> visible;for(const auto& placement:placements)if(placement.visible)visible.push_back(&placement);
    std::unordered_map<u64,Mesh> meshes;auto meshOf=[&](u64 feature)->const Mesh*{auto found=meshes.find(feature);if(found!=meshes.end())return &found->second;for(const auto& body:d.evalBodies)if(body.id==feature)return &meshes.emplace(feature,body.tessellate(d.tessOpt)).first->second;return nullptr;};
    size_t triangles=0;bool missing=false;for(const auto* placement:visible){const Mesh* mesh=meshOf(placement->feature);if(!mesh){missing=true;break;}triangles+=mesh->triCount();}
    if(missing||triangles>(force?120000u:8000u)){report.set("interference_checked",V::makeBool(false));report.set("interference",pairs);report.set("interference_note",V::makeStr(missing?"An instance has no display mesh.":"Display-mesh interference was skipped because the visible instances exceed the triangle budget. Pass interference:true to raise it to 120000 triangles."));return report;}
    report.set("interference_checked",V::makeBool(true));
    for(size_t i=0;i<visible.size();++i)for(size_t j=i+1;j<visible.size();++j){
        const auto& a=*visible[i];const auto& b=*visible[j];const Mesh& ma=*meshOf(a.feature);const Mesh& mb=*meshOf(b.feature);
        AABB ba=ma.computeBBox().transformed(a.world),bb=mb.computeBBox().transformed(b.world);if(!boxesOverlap(ba,bb))continue;
        bool meshChecked=ma.triCount()*mb.triCount()<=250000;bool hit=meshChecked?displayMeshesIntersect(ma,a.world,mb,b.world):true;if(!hit)continue;
        V pair=V::makeObj();pair.set("node_a",V::makeNum((double)a.node));pair.set("copy_a",V::makeNum(a.copy));pair.set("node_b",V::makeNum((double)b.node));pair.set("copy_b",V::makeNum(b.copy));pair.set("name_a",V::makeStr(a.name));pair.set("name_b",V::makeStr(b.name));pair.set("method",V::makeStr(meshChecked?"display_mesh":"bounds"));pairs.arr.push_back(pair);
    }
    report.set("interference",pairs);return report;
}
}
AgentSession::Cache& AgentSession::meshCache(const V& args){
    if(cacheRevision_!=revision){cache_.clear();cacheRevision_=revision;}
    u64 key=args.find("id")?id(*args.find("id")):0;
    if(auto node=args.find("node")){if(key)throw std::runtime_error("Select id or node, not both");key=id(*node)|(u64(1)<<63);}
    auto it=cache_.find(key);if(it!=cache_.end())return it->second;
    Cache c;c.mesh=selectedMesh(document,args);c.report=analyzeMesh(c.mesh);c.bvh=std::make_unique<MeshBVH>(c.mesh);
    return cache_.emplace(key,std::move(c)).first->second;
}
void AgentSession::remember(){undo_.push_back(documentToJson(document));if(undo_.size()>32)undo_.erase(undo_.begin());redo_.clear();}
V AgentSession::info() const {
    V r=V::makeObj();r.set("revision",V::makeNum((double)revision));r.set("units",V::makeStr("mm"));
    r.set("feature_count",V::makeNum(document.features.size()));r.set("recompute_ms",V::makeNum(document.recomputeMs));
    r.set("parameter_error",V::makeStr(document.parameterError));
    V planes=V::makeArr(),sketches=V::makeArr();for(auto& p:document.planes)planes.arr.push_back(V::makeStr(p.name));for(auto& sk:document.sketches)sketches.arr.push_back(V::makeStr(sk.name));r.set("planes",planes);r.set("sketches",sketches);
    V fs=V::makeArr();auto hidden=consumed(document);
    for(auto& f:document.features){V v=V::makeObj();v.set("id",V::makeNum((double)f.id));v.set("name",V::makeStr(f.name));v.set("type",V::makeStr(featureTypeName(f.type)));v.set("output",V::makeBool(!hidden.count(f.id)));fs.arr.push_back(v);}
    r.set("features",fs);r.set("assembly_nodes",V::makeNum(document.assembly.size()));return r;
}
V AgentSession::call(const std::string& tool,const V& args){
    try{
        if(args.t!=V::Obj)throw std::runtime_error("Arguments must be an object");
        if(tool=="doc_info")return result(info());
        if(tool=="doc_new"){
            const auto& expected=field(args,"expected_revision");
            if(expected.t!=V::Num||expected.num!=(double)revision)throw std::runtime_error("Revision conflict; read doc_info and retry");
            remember();document=Document{};++revision;return result(info());
        }
        if(tool=="agent_onboard")return result(agentOnboard(*this,args));
        if(tool=="doc_get"){V r=info(),j;arkjson::parse(documentToJson(document),j);r.set("document",j);return result(r);}
        if(tool=="plane_inspect"){
            auto name=str(args,"name");auto frame=resolvePlane(document,name);V r=V::makeObj(),matrix=V::makeArr(),axes=V::makeObj();for(auto x:frame.m)matrix.arr.push_back(V::makeNum(x));r.set("name",V::makeStr(name));r.set("world_column_major",matrix);r.set("kind",V::makeStr("plane"));for(auto& p:document.planes)if(p.name==name)r.set("kind",V::makeStr(p.kind));for(int i=0;i<3;++i){V direction=V::makeArr();for(int j=0;j<3;++j)direction.arr.push_back(V::makeNum(frame.m[4*i+j]));axes.set(name+"."+std::string(1,"XYZ"[i]),direction);}r.set("named_axes",axes);return result(r);
        }
        if(tool=="sketch_inspect"){
            auto* sk=document.findSketch(str(args,"name"));if(!sk)throw std::runtime_error("Sketch not found");bool samples=false;if(auto p=args.find("samples")){if(p->t!=V::Bool)throw std::runtime_error("samples must be boolean");samples=p->b;}return result(inspectSketch(document,*sk,samples));
        }
        if(tool=="eval_expr"){
            bool ok=false;auto v=evalExpr(str(args,"expr"),document.buildEnv(),&ok);
            if(!ok)throw std::runtime_error("Invalid mathematical expression");
            V r=V::makeObj();r.set("value",V::makeNum(v));return result(r);
        }
        if(tool=="sketch_diagnose"){
            NamedSketch scratch;std::string error;
            if(auto source=args.find("sketch")){if(!sketchFromJson(source->dump(),scratch.sk,&error))throw std::runtime_error(error);}
            else{auto sk=document.findSketch(str(args,"name"));if(!sk)throw std::runtime_error("Sketch not found");scratch.sk=sk->sk;}
            evaluateSketch(scratch,document.buildEnv());auto r=diagnoseSketch(scratch.sk);r.set("error",V::makeStr(scratch.error));r.set("revision",V::makeNum((double)revision));return result(r);
        }
        if(tool=="reference_inspect"){
            const auto feature=id(field(args,"id"));ensureNativeBody(document,feature,document.evalBodies);
            V r=V::makeObj();for(auto& b:document.evalBodies)if(b.id==feature)if(auto topology=b.nativeData.find("topology"))r.set("topology",*topology);
            if(args.find("reference")){double fraction=.5;if(auto p=args.find("fraction")){if(p->t!=V::Num)throw std::runtime_error("fraction must be numeric");fraction=p->num;}auto ref=resolveGeometryReference(document,feature,str(args,"reference"),fraction);V matrix=V::makeArr();for(auto x:ref.frame.m)matrix.arr.push_back(V::makeNum(x));r.set("world_column_major",matrix);r.set("kind",V::makeStr(ref.kind));}
            r.set("id",V::makeNum((double)feature));r.set("revision",V::makeNum((double)revision));return result(r);
        }
        if(tool=="kernel_info")return result(nativeKernelInfo());
        if(tool=="sheet_preview"){
            const auto selected=id(field(args,"id"));const auto& progress=field(args,"progress");if(progress.t!=V::Num)throw std::runtime_error("progress must be numeric");
            for(auto& body:document.evalBodies)if(body.id==selected){auto preview=buildSheetPreview(body.nativeData,progress.num);V r=V::makeObj(),poses=V::makeArr();for(auto& pose:preview.poses){V m=V::makeArr();for(auto x:pose.m)m.arr.push_back(V::makeNum(x));poses.arr.push_back(m);}r.set("panel_poses",poses);r.set("display_only",V::makeBool(true));r.set("trajectory_collision_checked",V::makeBool(false));r.set("revision",V::makeNum((double)revision));return result(r);}throw std::runtime_error("Authored sheet not found");
        }
        if(tool=="step_inspect"){
            V request=V::makeObj();request.set("operation",V::makeStr("step_inspect"));request.set("path",V::makeStr(absolutePath(str(args,"path"))));
            if(auto p=args.find("faces")){if(p->t!=V::Bool)throw std::runtime_error("faces must be boolean");request.set("faces",*p);}return result(nativeWorkerRequest(request));
        }
        if(tool=="native_inspect"){
            u64 selected=id(field(args,"id"));
            auto* feature=document.find(selected);
            if(!feature)throw std::runtime_error("Select a modeling feature");
            if(isMeshModelFeature(feature->type))throw std::runtime_error("Mesh features have no exact B-rep; use mesh_inspect");
            ensureNativeBody(document,selected,document.evalBodies);
            for(const Body& body:document.evalBodies)if(body.id==selected){
                V r=V::makeObj();r.set("id",V::makeNum((double)selected));
                if(auto report=body.nativeData.find("report"))r.set("report",*report);
                if(auto holes=body.nativeData.find("holes"))r.set("holes",*holes);
                for(auto key:{"panels","bends","topology"})if(auto data=body.nativeData.find(key))r.set(key,*data);
                r.set("read_only_geometry",V::makeBool(feature->type==FeatureType::SourceSheet));
                r.set("has_flat",V::makeBool(body.nativeData.find("flat_brep_base64")!=nullptr));
                return result(r);
            }
            throw std::runtime_error("Native feature has no evaluated body");
        }
        if(tool=="source_inspect"){
            auto* feature=document.find(id(field(args,"id")));if(!feature||feature->type!=FeatureType::SourceTube)throw std::runtime_error("Select a source_tube feature");
            V r=V::makeObj();r.set("report",*feature->sourceData.find("report"));r.set("holes",*feature->sourceData.find("holes"));r.set("read_only_geometry",V::makeBool(true));
            if(auto p=args.find("progress")){if(p->t!=V::Num||p->num<0||p->num>1)throw std::runtime_error("progress must be in [0,1]");auto preview=buildSourceTubePreview(feature->sourceData);auto poses=sourceFoldTransforms(preview,p->num);V matrices=V::makeArr();for(auto& pose:poses){V m=V::makeArr();for(auto v:pose.m)m.arr.push_back(V::makeNum(v));matrices.arr.push_back(m);}r.set("segment_transforms_column_major",matrices);}
            return result(r);
        }
        if(tool=="import_step"){
            bool replace=false;if(auto p=args.find("replace")){if(p->t!=V::Bool)throw std::runtime_error("replace must be boolean");replace=p->b;}
            auto package=importSourceTubeStep(str(args,"path"));buildSourceTubePreview(package);
            Document next;std::string error;if(!replace&&!documentFromJson(documentToJson(document),next,&error))throw std::runtime_error(error);
            Feature f;f.type=FeatureType::SourceTube;f.name="Source tube";if(auto p=package.find("source_name"))f.name=p->asStr();f.sourceData=std::move(package);auto id=next.addFeature(std::move(f));validated(next);
            if(documentToJson(next).size()>4*1024*1024)throw std::runtime_error("Imported document exceeds current 4 MiB persistence limit; use replace:true for a standalone source document");
            remember();document=std::move(next);++revision;V r=info();r.set("created_id",V::makeNum((double)id));r.set("report",*document.find(id)->sourceData.find("report"));return result(r);
        }
        if(tool=="import_sheet_step"){
            bool replace=false;if(auto p=args.find("replace")){if(p->t!=V::Bool)throw std::runtime_error("replace must be boolean");replace=p->b;}
            const V& factor=field(args,"k_factor");
            if(factor.t!=V::Num||!std::isfinite(factor.num)||factor.num<0||factor.num>1)throw std::runtime_error("k_factor must be in [0,1]");
            V request=V::makeObj();request.set("operation",V::makeStr("import_sheet"));request.set("source_path",V::makeStr(absolutePath(str(args,"path"))));request.set("k_factor",factor);
            if(auto body=args.find("body_index")){if(body->t!=V::Num||body->num<0||body->num!=std::floor(body->num))throw std::runtime_error("body_index must be a nonnegative integer");request.set("body_index",*body);}
            if(auto panel=args.find("root_panel")){if(panel->t!=V::Num||(panel->num!=0&&panel->num!=1))throw std::runtime_error("root_panel must be 0 or 1");request.set("root_panel",*panel);}
            V package=nativeWorkerRequest(request);
            (void)nativeResultMesh(package);
            Document next;std::string error;if(!replace&&!documentFromJson(documentToJson(document),next,&error))throw std::runtime_error(error);
            Feature feature;feature.type=FeatureType::SourceSheet;feature.name="Source sheet";if(auto name=package.find("source_name"))feature.name=name->asStr();feature.sourceData=std::move(package);u64 created=next.addFeature(std::move(feature));validated(next);
            if(documentToJson(next).size()>4*1024*1024)throw std::runtime_error("Imported sheet exceeds current 4 MiB document limit");
            remember();document=std::move(next);++revision;V answer=info();answer.set("created_id",V::makeNum((double)created));if(auto report=document.find(created)->sourceData.find("report"))answer.set("report",*report);return result(answer);
        }
        if(tool=="import_mesh"){
            if(auto expected=args.find("expected_revision")){if(expected->t!=V::Num||expected->num!=(double)revision)throw std::runtime_error("Revision conflict; read doc_get and retry");}
            double weld=0;if(auto value=args.find("weld_mm")){if(value->t!=V::Num||!(value->num>0&&value->num<=1))throw std::runtime_error("weld_mm must be in (0, 1]");weld=value->num;}
            Mesh imported=importMeshPath(str(args,"path"));const double rawVertices=(double)imported.vertCount(),rawTriangles=(double)imported.triCount();
            if(weld){imported.weld(weld);imported.computeNormals();if(!imported.triCount())throw std::runtime_error("weld removed every imported triangle");}
            Document next;std::string error;if(!documentFromJson(documentToJson(document),next,&error))throw std::runtime_error(error);
            Feature feature;feature.type=FeatureType::MeshBody;feature.name=args.find("name")?str(args,"name"):"Imported mesh";feature.sourceData=meshToPayload(imported);
            const u64 created=next.addFeature(std::move(feature));validated(next);
            if(documentToJson(next).size()>4*1024*1024)throw std::runtime_error("Imported mesh exceeds the 4 MiB document limit");
            remember();document=std::move(next);++revision;V answer=info();answer.set("created_id",V::makeNum((double)created));
            answer.set("input_vertices",V::makeNum(rawVertices));answer.set("input_triangles",V::makeNum(rawTriangles));
            answer.set("vertices",V::makeNum((double)imported.vertCount()));answer.set("triangles",V::makeNum((double)imported.triCount()));
            answer.set("welded",V::makeBool(weld>0));answer.set("mesh_authoritative",V::makeBool(true));answer.set("exact_geometry",V::makeBool(false));
            answer.set("file_linked",V::makeBool(false));return result(answer);
        }
        if(tool=="export_step"){
            auto* feature=document.find(id(field(args,"id")));if(!feature)throw std::runtime_error("Feature not found");
            if(feature->type==FeatureType::SourceTube)return result(exportSourceTubeStep(feature->sourceData,str(args,"path")));
            if(feature->type==FeatureType::NotchedTube)return result(exportNotchStep(notchRequestForFeature(*feature,document.buildEnv()),str(args,"path")));
            if(!isNativeFeature(feature->type))throw std::runtime_error("STEP export requires a native or supported tube feature");
            const Body* body=nullptr;for(const Body& candidate:document.evalBodies)if(candidate.id==feature->id)body=&candidate;
            if(!body)throw std::runtime_error("Native feature has no evaluated body");
            std::string pose="";if(auto value=args.find("pose")){if(value->t!=V::Str||(value->str!="flat"&&value->str!="formed"))throw std::runtime_error("pose must be flat or formed");pose=value->str;}
            if(feature->type==FeatureType::SourceSheet){
                V request=V::makeObj();request.set("operation",V::makeStr("export_sheet"));request.set("package",feature->sourceData);
                request.set("path",V::makeStr(absolutePath(str(args,"path"))));request.set("pose",V::makeStr(pose.empty()?"flat":pose));
                return result(nativeWorkerRequest(request));
            }
            const bool sheet=feature->type==FeatureType::SheetMetal||feature->type==FeatureType::SourceSheet;
            if(!sheet&&pose=="flat")throw std::runtime_error("Flat pose is only available for sheet features");
            const char* key=sheet&&pose!="formed"?"flat_brep_base64":"brep_base64";
            auto payload=body->nativeData.find(key);if(!payload||payload->t!=V::Str)throw std::runtime_error("Selected native pose has no exact STEP B-rep");
            return result(exportNativeBrep(payload->str,str(args,"path")));
        }
        if(tool=="notch_inspect"){
            auto* feature=document.find(id(field(args,"id")));if(!feature||feature->type!=FeatureType::NotchedTube)throw std::runtime_error("Select a notched_tube feature");
            V r=V::makeObj();auto request=notchRequestForFeature(*feature,document.buildEnv());r.set("request",request);r.set("plan",planNotchedBlank(request));
            if(auto value=args.find("progress")){
                if(value->t!=V::Num||value->num<0||value->num>1)throw std::runtime_error("progress must be in [0,1]");
                auto preview=buildNotchPreview(request);auto transforms=notchFoldTransforms(preview,value->num);V poses=V::makeArr();AABB bounds;
                for(size_t i=0;i<transforms.size();++i){V matrix=V::makeArr();for(auto x:transforms[i].m)matrix.arr.push_back(V::makeNum(x));poses.arr.push_back(matrix);bounds.grow(preview.segments[i].computeBBox().transformed(transforms[i]));}
                r.set("segment_transforms_column_major",poses);r.set("progress",*value);r.set("finite_wall_deformation_verified",V::makeBool(false));
                V box=V::makeArr();for(auto x:{bounds.mn.x,bounds.mn.y,bounds.mn.z,bounds.mx.x,bounds.mx.y,bounds.mx.z})box.arr.push_back(V::makeNum(x));r.set("display_bounds_mm",box);
            }
            return result(r);
        }
        if(tool=="notch_plan")return result(planNotchedBlank(args));
        if(tool=="compute_info")return result(computeInfo());
        if(tool=="compute_benchmark"){
            size_t count=1048576;if(auto value=args.find("vertices")){if(value->t!=V::Num||value->num<1||value->num>4000000||value->num!=std::floor(value->num))throw std::runtime_error("vertices must be an integer in 1..4000000");count=(size_t)value->num;}
            return result(benchmarkCompute(count));
        }
        if(tool=="assembly_info"){
            std::vector<AssemblyPlacement> placements;std::string error;AssemblyDiagnostics diagnostics;if(!resolveAssembly(document,placements,error,&diagnostics))throw std::runtime_error(error);
            V r=info(),nodes=V::makeArr(),bom=V::makeArr();std::map<u64,size_t> quantities;
            for(auto& p:placements){V j=V::makeObj(),matrix=V::makeArr();j.set("node",V::makeNum((double)p.node));j.set("feature",V::makeNum((double)p.feature));j.set("name",V::makeStr(p.name));j.set("visible",V::makeBool(p.visible));j.set("copy",V::makeNum(p.copy));
                for(auto x:p.world.m)matrix.arr.push_back(V::makeNum(x));
                j.set("world_column_major",matrix);nodes.arr.push_back(j);++quantities[p.feature];}
            for(auto& kv:quantities){V j=V::makeObj();j.set("feature",V::makeNum((double)kv.first));j.set("name",V::makeStr(document.find(kv.first)->name));j.set("quantity",V::makeNum(kv.second));bom.arr.push_back(j);}
            r.set("instances",nodes);r.set("bom",bom);r.set("mates_solved",V::makeBool(!document.mates.empty()));r.set("diagnostics",assemblyDiagnosticsJson(diagnostics));V mates=V::makeArr();for(auto& m:document.mates)mates.arr.push_back(mateToJson(m));r.set("mates",mates);
            bool forceInterference=false;if(auto flag=args.find("interference")){if(flag->t!=V::Bool)throw std::runtime_error("interference must be boolean");forceInterference=flag->b;}
            V overlap=displayInterference(document,placements,forceInterference);for(auto& kv:overlap.obj)r.set(kv.first,kv.second);
            if(auto spec=args.find("candidate")){
                if(spec->t!=V::Obj)throw std::runtime_error("candidate must be an object");
                V mateArgs=*spec;bindEndpoint(document,mateArgs,"a");bindEndpoint(document,mateArgs,"b");
                AssemblyMate candidate;editMate(candidate,mateArgs);
                u64 candidateId=candidate.id?candidate.id:document.nextMateId_;
                for(const auto& existing:document.mates)if(existing.id==candidateId)candidateId=document.nextMateId_+document.mates.size()+1;
                AssemblyDiagnostics probed;std::string probeError;bool satisfied=false;
                bool solved=probeAssemblyMate(document,candidate,probed,probeError,satisfied);
                V block=V::makeObj(),frames=V::makeArr();
                block.set("solved",V::makeBool(solved));block.set("satisfied",V::makeBool(solved&&satisfied));block.set("error",V::makeStr(probeError));
                double position=0,angle=0;for(const auto& row:probed.mates)if(row.id==candidateId){position=row.positionMm;angle=row.angleDeg;}
                block.set("position_residual_mm",V::makeNum(position));block.set("angle_residual_deg",V::makeNum(angle));
                auto vec=[&](Vec3 p){V a=V::makeArr();for(double x:{p.x,p.y,p.z})a.arr.push_back(V::makeNum(x));return a;};
                for(const auto& frame:probed.frames)if(frame.id==candidateId){
                    V a=V::makeObj();a.set("instance",V::makeNum((double)candidate.a));a.set("reference",V::makeStr(candidate.reference_a));a.set("origin",vec(frame.originA));a.set("axis",vec(frame.axisA));frames.arr.push_back(a);
                    V b=V::makeObj();b.set("instance",V::makeNum((double)candidate.b));b.set("reference",V::makeStr(candidate.reference_b));b.set("origin",vec(frame.originB));b.set("axis",vec(frame.axisB));frames.arr.push_back(b);
                }
                block.set("frames",frames);
                block.set("axis_convention",V::makeStr("Analytic cylinder, cone and torus meshes run along +Y. A native_solid cylinder, cone or torus uses local +Z. Promoting an analytic cylinder, cone or torus applies Rx(-90 degrees), so the exact B-rep axis becomes +Z. Mate references Z, origin and XY use the instance frame +Z. Semantic reference axis is the exact B-rep cylinder axis. The analytic mesh axis is not retargeted."));
                r.set("candidate",block);
            }
            return result(r);
        }
        if(tool=="tube_inspect"){
            auto* f=document.find(id(field(args,"id")));if(!f||f->type!=FeatureType::Tube)throw std::runtime_error("Tube feature not found");
            auto tube=buildTube(*f,document.buildEnv());if(!tube.error.empty())throw std::runtime_error(tube.error);
            V r=V::makeObj(),table=V::makeArr();r.set("centerline_length_mm",V::makeNum(tube.centerlineLength));r.set("section_area_mm2",V::makeNum(tube.sectionArea));r.set("ideal_material_volume_mm3",V::makeNum(tube.centerlineLength*tube.sectionArea));
            r.set("fabrication_compensated",V::makeBool(false));r.set("self_intersections_checked",V::makeBool(false));r.set("sampled_rings",V::makeNum(tube.frames.size()));
            for(size_t i=0;i<f->bends.size();++i){auto& b=f->bends[i];V row=V::makeObj();auto env=document.buildEnv();row.set("feed_mm",V::makeNum(evalExpr(b.feed,env)));row.set("clr_mm",V::makeNum(evalExpr(b.radius,env)));row.set("angle_deg",V::makeNum(evalExpr(b.angle,env)));row.set("plane_roll_deg",V::makeNum(evalExpr(b.roll,env)));row.set("station_start_mm",V::makeNum(tube.bendStarts[i]));row.set("station_end_mm",V::makeNum(tube.bendEnds[i]));table.arr.push_back(row);}
            r.set("bends",table);return result(r);
        }
        if(tool=="doc_apply"){
            if(auto expected=args.find("expected_revision")){
                if(expected->t!=V::Num||expected->num!=(double)revision)throw std::runtime_error("Revision conflict; read doc_get and retry");
            }
            const auto& commands=field(args,"commands");
            if(commands.t!=V::Arr||commands.arr.empty()||commands.arr.size()>128)throw std::runtime_error("commands must contain 1..128 operations");
            Document next;std::string error;
            if(!documentFromJson(documentToJson(document),next,&error))throw std::runtime_error(error);
            V created=V::makeArr(),createdNodes=V::makeArr(),createdMates=V::makeArr(),sheetEdits=V::makeArr();
            for(auto& c:commands.arr){
                auto op=str(c,"op");
                if(op=="param.set")next.setParam(str(c,"name"),expression(field(c,"expr")));
                else if(op.rfind("sheet.",0)==0){auto edited=applySheetCommand(next,c);if(op=="sheet.create")created.arr.push_back(*edited.find("feature_id"));sheetEdits.arr.push_back(edited);}
                else if(op=="plane.set"||op=="plane.remove"){
                    auto name=str(c,"name");if(name=="XY"||name=="XZ"||name=="YZ"||name.size()>128)throw std::runtime_error("Built-in planes are immutable; custom names must be 1..128 characters");
                    auto it=std::find_if(next.planes.begin(),next.planes.end(),[&](const DatumPlane& p){return p.name==name;});
                    if(op=="plane.remove"){if(it==next.planes.end())throw std::runtime_error("Plane not found");next.planes.erase(it);}
                    else{
                        if(it==next.planes.end()){next.planes.push_back({name});it=next.planes.end()-1;}
                        if(c.find("parent"))it->parent=str(c,"parent");
                        if(c.find("kind"))it->kind=str(c,"kind");
                        if(auto support=c.find("support")){if(support->t!=V::Obj&&support->t!=V::Null)throw std::runtime_error("Datum support must be object or null");it->support=*support;}
                        if(auto p=c.find("p")){if(p->t!=V::Obj)throw std::runtime_error("plane p must be object");for(auto& kv:p->obj)it->p[kv.first]=expression(kv.second);}
                    }
                }
                else if(op=="feature.add"){
                    Feature f;auto type=str(c,"type");
                    if(!featureTypeFromName(type,f.type))throw std::runtime_error("Unknown feature type: "+type);
                    if(f.type==FeatureType::SourceTube||f.type==FeatureType::SourceSheet)throw std::runtime_error("Use the source STEP import tool to create verified source geometry");
                    f.name=c.find("name")?str(c,"name"):type;
                    if(auto p=c.find("p"))params(f,*p);
                    if(auto b=c.find("bends"))bends(f,*b);
                    if(auto n=c.find("notches"))notches(f,*n);
                    if(auto spec=c.find("native")){if(!isNativeFeature(f.type)||spec->t!=V::Obj)throw std::runtime_error("native must be an object for a native feature");f.nativeSpec=*spec;}
                    else if(isNativeFeature(f.type))f.nativeSpec=V::makeObj();
                    if(auto mesh=c.find("mesh")){if(f.type!=FeatureType::MeshBody||mesh->t!=V::Obj)throw std::runtime_error("mesh is only valid as an object on mesh_body");f.sourceData=*mesh;}
                    if(f.type==FeatureType::MeshBody&&f.sourceData.t!=V::Obj)throw std::runtime_error("mesh_body requires mesh:{positions,indices}");
                    if(auto in=c.find("inputs")){if(in->t!=V::Arr)throw std::runtime_error("inputs must be an array");for(auto& x:in->arr)f.inputs.push_back(featureRef(next,x));}
                    if(auto mode=c.find("mode")){if(mode->t!=V::Num||mode->num<0||mode->num>2||mode->num!=std::floor(mode->num))throw std::runtime_error("mode must be 0, 1 or 2");f.opInt=(int)mode->num;}
                    created.arr.push_back(V::makeNum((double)next.addFeature(std::move(f))));
                }
                else if(op=="feature.edit"){
                    auto* f=next.find(id(field(c,"id")));if(!f)throw std::runtime_error("Feature not found");
                    if((f->type==FeatureType::SourceTube||f->type==FeatureType::SourceSheet)&&(c.find("p")||c.find("bends")||c.find("notches")||c.find("native")))throw std::runtime_error("Imported source geometry is read-only; reimport changed STEP");
                    if(auto p=c.find("p"))params(*f,*p);
                    if(auto b=c.find("bends"))bends(*f,*b);
                    if(auto n=c.find("notches"))notches(*f,*n);
                    if(auto spec=c.find("native")){if(!isNativeFeature(f->type)||spec->t!=V::Obj)throw std::runtime_error("native must be an object for a native feature");f->nativeSpec=*spec;}
                    if(auto mesh=c.find("mesh")){if(f->type!=FeatureType::MeshBody||mesh->t!=V::Obj)throw std::runtime_error("mesh is only valid as an object on mesh_body");f->sourceData=*mesh;}
                    if(c.find("name"))f->name=str(c,"name");
                }
                else if(op=="feature.remove"){if(!next.remove(id(field(c,"id"))))throw std::runtime_error("Feature not found");}
                else if(op=="sketch.set"){
                    Sketch sk;if(!sketchFromJson(field(c,"sketch").dump(),sk,&error))throw std::runtime_error(error);
                    next.setSketch(str(c,"name"),std::move(sk));
                }
                else if(op=="sketch.remove"){if(!next.removeSketch(str(c,"name")))throw std::runtime_error("Sketch not found");}
                else if(op=="sketch.entity.set"){
                    auto* sk=next.findSketch(str(c,"name"));if(!sk)throw std::runtime_error("Sketch not found");
                    const auto& entity=field(c,"entity");auto name=str(entity,"name");V wrapper=V::makeObj(),entities=V::makeArr();entities.arr.push_back(entity);wrapper.set("entities",entities);Sketch single;
                    if(!sketchFromJson(wrapper.dump(),single,&error))throw std::runtime_error(error);
                    auto it=std::find_if(sk->sk.ents.begin(),sk->sk.ents.end(),[&](const SketchEnt& e){return e.name==name;});
                    if(it==sk->sk.ents.end())sk->sk.ents.push_back(std::move(single.ents.front()));else *it=std::move(single.ents.front());
                    Sketch checked;if(!sketchFromJson(sketchToJson(sk->sk),checked,&error))throw std::runtime_error(error);sk->sk=std::move(checked);
                }
                else if(op=="assembly.add"){
                    AssemblyNode n;n.id=next.nextAssemblyId_++;n.name="Component";editNode(n,c,next);next.assembly.push_back(n);createdNodes.arr.push_back(V::makeNum((double)n.id));
                }
                else if(op=="mate.add"){
                    if(next.mates.size()>=256)throw std::runtime_error("Assembly mate capacity exceeded");AssemblyMate m;m.id=next.nextMateId_++;V mateArgs=c;bindEndpoint(next,mateArgs,"a");bindEndpoint(next,mateArgs,"b");editMate(m,mateArgs);next.mates.push_back(m);createdMates.arr.push_back(V::makeNum((double)m.id));
                }
                else if(op=="mate.edit"||op=="mate.remove"){
                    auto key=id(field(c,"id"));auto it=std::find_if(next.mates.begin(),next.mates.end(),[&](const AssemblyMate& m){return m.id==key;});if(it==next.mates.end())throw std::runtime_error("Mate not found");if(op=="mate.edit"){V mateArgs=c;bindEndpoint(next,mateArgs,"a");bindEndpoint(next,mateArgs,"b");editMate(*it,mateArgs);}else next.mates.erase(it);
                }
                else if(op=="assembly.edit"||op=="assembly.remove"){
                    u64 key=id(field(c,"id"));auto it=std::find_if(next.assembly.begin(),next.assembly.end(),[&](const AssemblyNode& n){return n.id==key;});
                    if(it==next.assembly.end())throw std::runtime_error("Assembly node not found");
                    if(op=="assembly.edit")editNode(*it,c,next);else next.assembly.erase(it);
                }
                else throw std::runtime_error("Unknown operation: "+op);
            }
            validated(next); // one evaluation for the entire batch, commit only on success
            remember();document=std::move(next);++revision;
            V r=info();r.set("created_ids",created);r.set("created_node_ids",createdNodes);r.set("created_mate_ids",createdMates);r.set("sheet_edits",sheetEdits);return result(r);
        }
        if(tool=="doc_undo"||tool=="doc_redo"){
            auto& source=tool=="doc_undo"?undo_:redo_;auto& destination=tool=="doc_undo"?redo_:undo_;
            if(source.empty())throw std::runtime_error("No history available");
            Document next;std::string error;if(!documentFromJson(source.back(),next,&error))throw std::runtime_error(error);
            validated(next);destination.push_back(documentToJson(document));source.pop_back();document=std::move(next);++revision;return result(info());
        }
        if(tool=="doc_save"){
            auto path=str(args,"path");if(!saveDocument(document,path.c_str()))throw std::runtime_error("Could not save document");
            V r=info();r.set("path",V::makeStr(path));return result(r);
        }
        if(tool=="doc_load"){
            Document next;std::string error;
            if(!loadDocument(str(args,"path").c_str(),next,&error))throw std::runtime_error(error);
            validated(next);remember();document=std::move(next);++revision;return result(info());
        }
        if(tool=="mesh_inspect"){
            V report=meshCache(args).report.json();
            if(auto key=args.find("id");key&&!args.find("node")){
                const u64 feature=id(*key);
                for(const Body& body:document.evalBodies)if(body.id==feature&&body.nativeData.find("mesh_operation")){report.set("mesh_model",body.nativeData);break;}
            }
            return result(report);
        }
        if(tool=="mesh_get"){
            auto& cached=meshCache(args);const Mesh& m=cached.mesh;V r=cached.report.json(),positions=V::makeArr(),indices=V::makeArr();
            // Explicit opt-in: inspection never returns large vertex buffers.
            for(auto x:m.pos)positions.arr.push_back(V::makeNum(x));
            for(auto x:m.idx)indices.arr.push_back(V::makeNum(x));
            r.set("positions",positions);r.set("indices",indices);return result(r);
        }
        if(tool=="mesh_raycast"){
            auto vector=[&](const char* key){auto& a=field(args,key);if(a.t!=V::Arr||a.arr.size()!=3)throw std::runtime_error("Ray vectors require three finite numbers");for(auto& x:a.arr)if(x.t!=V::Num||!std::isfinite(x.num)||std::fabs(x.num)>1e8)throw std::runtime_error("Invalid ray coordinate");return Vec3{a.arr[0].num,a.arr[1].num,a.arr[2].num};};
            Ray ray{vector("origin"),vector("direction")};if(ray.d.len()<1e-15)throw std::runtime_error("Ray direction cannot be zero");
            auto& c=meshCache(args);auto hit=c.bvh->raycast(c.mesh,ray);V r=V::makeObj();r.set("hit",V::makeBool(hit.hit));
            if(hit.hit){r.set("distance_mm",V::makeNum(hit.distance));r.set("triangle",V::makeNum(hit.triangle));
                auto arr=[](Vec3 p){V a=V::makeArr();for(auto x:{p.x,p.y,p.z})a.arr.push_back(V::makeNum(x));return a;};r.set("point",arr(hit.point));r.set("normal",arr(hit.normal));}
            return result(r);
        }
        if(tool=="export_mesh"){
            auto path=str(args,"path"),format=str(args,"format");Mesh m=selectedMesh(document,args);bool ok=false;
            if(format=="stl")ok=exportSTL(path.c_str(),m);
            else if(format=="obj")ok=exportOBJ(path.c_str(),m);
            else throw std::runtime_error("format must be stl or obj");
            if(!ok)throw std::runtime_error("Export failed");
            V r=V::makeObj();r.set("path",V::makeStr(path));r.set("triangles",V::makeNum(m.triCount()));return result(r);
        }
        if(tool=="snapshot"){
            auto path=str(args,"path");std::string view="iso";
            if(auto requested=args.find("view")){if(requested->t!=V::Str||(requested->str!="iso"&&requested->str!="front"&&requested->str!="right"&&requested->str!="top"))throw std::runtime_error("snapshot view must be iso, front, right or top");view=requested->str;}
            Mesh m=selectedMesh(document,args);SnapCam camera;
            if(view=="front"){camera.yaw=0;camera.pitch=0;}
            else if(view=="right"){camera.yaw=K_PI/2;camera.pitch=0;}
            else if(view=="top"){camera.yaw=0;camera.pitch=1.52;}
            AABB box=m.computeBBox();camera.useTarget=true;camera.target=box.valid()?box.center():Vec3{};
            camera.dist=std::max((box.valid()?box.diagonal():1.)*1.7,1.);
            if(!writePNG(path.c_str(),renderSnapshot(m,1280,800,camera)))throw std::runtime_error("Snapshot failed");
            V r=V::makeObj();r.set("path",V::makeStr(path));r.set("view",V::makeStr(view));r.set("display_mesh_authoritative",V::makeBool(false));r.set("authority",V::makeStr("display_mesh"));return result(r);
        }
        throw std::runtime_error("Unknown tool: "+tool);
    }catch(const std::exception& e){V r=V::makeObj();r.set("error",V::makeStr(e.what()));r.set("revision",V::makeNum((double)revision));return result(r,true);}
}
V AgentSession::tools(){
    struct Def{const char* name;const char* desc;const char* schema;bool read;};
    const Def defs[]={
        {"agent_onboard","START HERE. Discover current document/revision, supported modeling workflows, expression-driven splines, examples and validation limits. Read-only; choose a topic before modeling.",R"({"type":"object","properties":{"topic":{"type":"string","enum":["overview","sketch","tube","assembly","surfacing","sheet","solids","mesh"]}}})",true},
        {"doc_info","Compact document summary with revision and stable feature IDs.",R"({"type":"object"})",true},
        {"doc_new","Start an empty document only when requested. Clears the active model with undo and a monotonically increasing revision; does not overwrite saved files.",R"({"type":"object","properties":{"expected_revision":{"type":"integer","minimum":0}},"required":["expected_revision"]})",false},
        {"doc_get","Read full parametric document, including expressions and revision.",R"({"type":"object"})",true},
        {"doc_apply","Atomic batch; evaluate once, roll back on any failure. Operations: param.set{name,expr}, feature.add{type,name,p,native,inputs,mode}, feature.edit{id,p,native,name}, feature.remove{id}, plane.set{name,parent,p:{ox,oy,oz,rx,ry,rz},support?:{kind:face|midplane|normal_to_curve|tube_path,feature,reference,second_feature?,second_reference?,fraction?,station?}}, plane.remove{name}, sketch.set{name,sketch:{plane,tolerance,entities,constraints}}, sketch.entity.set{name,entity}, sketch.remove{name}, assembly.add{name,parent,feature,p,visible}, assembly.edit{id,parent,feature,p,visible}, assembly.remove{id}. feature=0 is a group; feature and feature inputs accept a unique name or an id, and the saved document stores the id. placements are rigid tx/ty/tz/rx/ry/rz. assembly pattern is type linear or circular with count and dx/dy/dz or step/axis. It repeats one part, a mate moves that pattern rigidly, and degrees of freedom count the node once. Tube feature accepts p{section:round|rect,od,width,height,wall,tail,chord_tol,sides} plus bends:[{feed,radius,angle,roll}] in mm/degrees. Sketch entities: kind point|line|circle|spline, optional unique name, dof numbers or expression strings (strings are driven), construction. Spline dof is 2..64 XY anchor pairs flattened; closed wraps without repeated first point, tension 0..1. Constraints val/val2 accept expressions. Entity edits replace one named entity. Mates: mate.add{a,b,type,reference_a,reference_b,value}, mate.edit{id,...}, mate.remove{id}. Endpoints a and b accept a unique instance name or an id; the saved mate stores ids. ground instances with assembly.edit{id,fixed:true}. assembly_info candidate probes a mate without saving it. Sheet edits: sheet.create{name,p:{width,thickness,k_factor},base:{id,length}}, sheet.base.edit{id,values}, sheet.flange.add{id,flange:{id?,parent,edge:u_min|u_max|v_min|v_max,length,width?,offset?,inner_radius,angle,k_factor?}}, sheet.opening.add{id,opening:{id?,panel,kind:circle|polygon,x,y,radius,points}}, sheet.flange.edit/sheet.opening.edit{id,entity,values}, sheet.flange.remove/sheet.opening.remove{id,entity,cascade?}. Local expressions: panel_length/panel_width/thickness in openings, parent_length/parent_width/edge_length/thickness in flanges. Native feature types: native_solid{native:{op:box|cylinder|sphere|cone|torus|extrude|union|cut|common|hole|fillet|chamfer|draft|linear_pattern|circular_pattern|mirror|transform,...},inputs,p} exact ops. Fillet/chamfer take native.edges or native.selection sharp|convex|concave|all plus p.min_angle. Draft takes p.angle and native.direction/neutral/faces; freeform faces reject. Patterns take p.count and p.dx/dy/dz or p.step plus native.axis; mirror takes native.plane XY|XZ|YZ or a datum name, or native.face naming one planar face. p.fuse defaults to 0. A Boolean tool may be a compound of those solids. loft{native:{sections:[sketch names]},p:{solid,ruled,max_degree?,smooth?,compatible?,continuity?}}. A one-point sketch is a first or last tip. Loft edges are edge.start/end.<entity> and rail.<index>, surface_fill{p:{sketch} or native:{profile}}, bspline_surface{native:{poles,weights?},p:{u_degree,v_degree}}, thicken{inputs:[native surface],p:{thickness}}, sheet_metal{p:{width,leg_a,leg_b,thickness,inner_radius,angle,k_factor},native:{openings?}}. Feature types: sketch_cut{inputs:[base],p:{sketch,depth,offset}}, notched_tube{p:{width_mm,height_mm,wall_mm,length_mm,hinge_offset_mm},notches:[{station,angle,face:bottom|top|left|right}]}, tube, box{dx,dy,dz}, cylinder{r,h}, sphere{r}, cone{rb,rt,h}, torus{R,r}, extrude{shape,w,d,height,sketch}, revolve{shape,w,d,angle,sketch}, transform{tx,ty,tz,rx,ry,rz}, boolean{mode:0 union/1 cut/2 intersect}, pattern{count,dx,dy,dz}, surface{x,y,z,u0,u1,v0,v1,nu,nv,wrap_u,wrap_v}. Geometry parameters belong in p and accept math expressions. Transforms and solid patterns take one input. native_solid union, cut or common accepts 2..16 feature inputs and/or native.instances of placed parts, including every pattern copy, and returns one exact solid. Two-input booleans keep the previous document version. Instance patterns, native.instances, and unions of more than two exact solids save as document version 10. Use expected_revision to reject stale writes. Angles in degrees except trig expressions in radians. Surface coordinates use u,v; periodic seams are checked. Exact surfaces also include sweep{native:{section,path} sketch names or profiles,p:{solid,correction,contact}}, extrude_surface{p:{sketch,distance,solid} or native.profile}, revolve_surface{p:{sketch,angle,solid},native:{axis:X|Y|Z default Y}}, offset_surface{inputs:[one face or shell],p:{distance}}, and sew{inputs:[2..16 faces or shells],p:{solid}}. Mesh features are sampled authority and never become a B-rep: mesh_body{mesh:{positions,indices}}, mesh_weld{inputs,p:{epsilon}}, mesh_smooth{inputs,p:{iterations,lambda,pin_boundary}}, mesh_decimate{inputs,p:{ratio}}, mesh_offset{inputs,p:{distance}}, mesh_fill{inputs}. An exact B-rep input needs p.sample=1, which opts into the display tessellation. import_mesh reads an STL or OBJ snapshot under the triangle cap.",R"({"type":"object","properties":{"expected_revision":{"type":"integer","minimum":0},"commands":{"type":"array","minItems":1,"maxItems":128,"items":{"type":"object","properties":{"op":{"type":"string","enum":["param.set","feature.add","feature.edit","feature.remove","sketch.set","sketch.entity.set","sketch.remove","plane.set","plane.remove","assembly.add","assembly.edit","assembly.remove","mate.add","mate.edit","mate.remove","sheet.create","sheet.base.edit","sheet.flange.add","sheet.flange.edit","sheet.flange.remove","sheet.opening.add","sheet.opening.edit","sheet.opening.remove"]}},"required":["op"]}}},"required":["commands"]})",false},
        {"plane_inspect","Read a named construction plane's world transform. Built-ins: XY, XZ (normal -Y), YZ (normal +X). Offsets/XYZ Euler rotations are relative to parent.",R"({"type":"object","properties":{"name":{"type":"string"}},"required":["name"]})",true},
        {"sketch_inspect","Read resolved sketch coordinates, local constraint rank/remaining DOFs, profile readiness and sampling counts. Optional samples:true includes local/world polylines. Spline chord tolerance bounds curve approximation, not manufacturing accuracy.",R"({"type":"object","properties":{"name":{"type":"string"},"samples":{"type":"boolean"}},"required":["name"]})",true},
        {"sketch_diagnose","Diagnose a named sketch or an uncommitted sketch candidate. Evaluates dimensions, attempts solve, reports each constraint residual and local rank/redundancy even if inconsistent. Does not mutate the document; no minimal conflict-set claim.",R"({"type":"object","properties":{"name":{"type":"string"},"sketch":{"type":"object"}},"oneOf":[{"required":["name"]},{"required":["sketch"]}]})",true},
        {"reference_inspect","Discover exact producer semantic face/edge/axis names and resolve a selected reference frame. Derived B-rep cache may be materialized from analytic parameters; document/revision unchanged. Split, deleted, ambiguous or stale references fail; numeric topology indices are not persistent IDs.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"reference":{"type":"string"},"fraction":{"type":"number","minimum":0,"maximum":1}},"required":["id"]})",true},
        {"sheet_preview","Read sequential flat-to-formed panel transforms for an authored sheet at progress 0..1. Display only; exact exports use explicit flat/formed B-reps. No trajectory or press-tool collision certification.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"progress":{"type":"number","minimum":0,"maximum":1}},"required":["id","progress"]})",true},
        {"doc_undo","Undo the previous committed transaction; revision increases.",R"({"type":"object"})",false},
        {"doc_redo","Redo a transaction after undo.",R"({"type":"object"})",false},
        {"eval_expr","Evaluate finite arithmetic and trigonometry with named parameters; trig uses radians, deg() converts degrees.",R"({"type":"object","properties":{"expr":{"type":"string"}},"required":["expr"]})",true},
        {"mesh_inspect","Compact indexed topology audit: bounds, area, edge counts, components and signed volume for closed oriented edges. Does not certify solids or detect self intersections. Omit id to inspect final outputs only.",R"({"type":"object","properties":{"node":{"type":"integer","minimum":1},"id":{"type":"integer","minimum":1}}})",true},
        {"mesh_get","Explicitly retrieve mesh positions and triangle indices; potentially large. Prefer mesh_inspect for reasoning.",R"({"type":"object","properties":{"node":{"type":"integer","minimum":1},"id":{"type":"integer","minimum":1}}})",true},
        {"mesh_raycast","Nearest triangle hit using a cached AABB hierarchy. Direction is normalized; distances in mm. Triangle IDs last for this tessellation/revision only.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"origin":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"direction":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["origin","direction"]})",true},
        {"assembly_info","Hierarchy placements, pattern copies and BOM quantities by shared feature definition. Each instance includes copy. Hidden instances remain in BOM. A patterned node counts once for degrees of freedom. Optional candidate probes one mate and reports residuals, the two frames and the axis convention without saving it or changing revision. interference compares display meshes: the default budget is 8000 triangles and interference:true raises it to 120000. A skip sets interference_checked false. Overlap is display mesh evidence, not exact clearance or motion. Concentric, coincident/contact, signed distance and angle mates, grounded instances, local DOFs and residual diagnosis.",R"({"type":"object","properties":{"interference":{"type":"boolean"},"candidate":{"type":"object"}}})",true},
        {"tube_inspect","Ideal centerline length, section area and bend table. No springback, weld allowance or material compensation.",R"({"type":"object","properties":{"node":{"type":"integer","minimum":1},"id":{"type":"integer","minimum":1}},"required":["id"]})",true},
        {"source_inspect","Read source projection evidence and exact opening coordinates without large mesh buffers. Optional progress returns finite-hinge display transforms; source geometry stays read-only.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"progress":{"type":"number","minimum":0,"maximum":1}},"required":["id"]})",true},
        {"native_inspect","Read native feature validation, opening inventory, and flat availability without transmitting B-rep or mesh buffers.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1}},"required":["id"]})",true},
        {"kernel_info","Report the isolated native OCCT modeling backend and version.",R"({"type":"object"})",true},
        {"step_inspect","Read STEP solids, units, B-rep validity, dimensions, volume and surface types before choosing an explicit body. Optional faces includes source face inventory. Read-only C++ OCCT; no automatic sheet classification.",R"({"type":"object","properties":{"path":{"type":"string"},"faces":{"type":"boolean"}},"required":["path"]})",true},
        {"import_step","Import and geometrically verify a planar tube STEP with cylindrical retained-wall hinges. Preserves exact hole/cutout B-reps, projects rigid spans, checks refolding and imports native preview. Optional replace:true starts a new document. Fails closed outside supported domain.",R"({"type":"object","properties":{"path":{"type":"string"},"replace":{"type":"boolean"}},"required":["path"]})",false},
        {"import_sheet_step","Import a source-owned supported sheet body through the isolated C++ OCCT worker. k_factor is explicit; body_index selects a zero-based solid occurrence and root_panel resolves a panel tie. Source geometry stays read-only.",R"({"type":"object","properties":{"path":{"type":"string"},"k_factor":{"type":"number","minimum":0,"maximum":1},"body_index":{"type":"integer","minimum":0},"root_panel":{"type":"integer","minimum":0,"maximum":1},"replace":{"type":"boolean"}},"required":["path","k_factor"]})",false},
        {"import_mesh","Import an STL or OBJ file as a mesh_body snapshot. Optional weld_mm merges vertices. The file is not linked: later edits of the file do not change the document. Caps are 12288 vertices and 4096 triangles. The result is mesh authority, not an exact B-rep.",R"({"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"},"weld_mm":{"type":"number","exclusiveMinimum":0,"maximum":1},"expected_revision":{"type":"integer","minimum":0}},"required":["path"]})",false},
        {"export_step","Export an exact native B-rep to STEP with readback validation. Sheet features default to the flat pose; pose:formed exports the formed source. Existing tube exports keep their original straight behavior.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"path":{"type":"string"},"pose":{"type":"string","enum":["flat","formed"]}},"required":["id","path"]})",false},
        {"notch_inspect","Read a notched_tube feature's straight request and ideal cut plan. Optional progress returns sequential display matrices and bounds without document mutation.",R"({"type":"object","properties":{"progress":{"type":"number","minimum":0,"maximum":1},"id":{"type":"integer","minimum":1}},"required":["id"]})",true},
        {"notch_plan","Ideal preview of a straight hollow rectangular tube with V cuts leaving one wall. Requires explicit hinge offset inside that wall. Not a source reconstruction or production-approved laser program. Returns cutter polygons, analytic removed volume and cut-face closure proof; finite-wall strain is not validated.",R"({"type":"object","properties":{"width_mm":{"type":"number"},"height_mm":{"type":"number"},"wall_mm":{"type":"number"},"length_mm":{"type":"number"},"hinge_offset_mm":{"type":"number"},"notches":{"type":"array","items":{"type":"object","properties":{"station_mm":{"type":"number"},"angle_deg":{"type":"number"},"hinge_face":{"type":"string","enum":["bottom","top","left","right"]}},"required":["station_mm","angle_deg","hinge_face"]}}},"required":["width_mm","height_mm","wall_mm","length_mm","hinge_offset_mm","notches"]})",true},
        {"compute_info","Actual CUDA availability, selected device, transfer-inclusive last execution and CPU fallback reason.",R"({"type":"object"})",true},
        {"compute_benchmark","Bounded CPU/CUDA benchmark (default 1M vertices) with transfer, synchronization and numerical parity; initializes GPU runtime.",R"({"type":"object","properties":{"vertices":{"type":"integer","minimum":1,"maximum":4000000}}})",true},
        {"doc_save","Save .ark or legacy .arkjson parametric document as versioned JSON.",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})",false},
        {"doc_load","Load .ark/.arkjson into this session, validate and commit atomically.",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})",false},
        {"export_mesh","Export final output bodies or a selected id as STL/OBJ; construction inputs are excluded.",R"({"type":"object","properties":{"path":{"type":"string"},"format":{"type":"string","enum":["stl","obj"]},"id":{"type":"integer","minimum":1}},"required":["path","format"]})",false},
        {"snapshot","Render final output display meshes to a 1280x800 PNG. view is iso, front, right or top. The image is display evidence, not STEP authority.",R"({"type":"object","properties":{"path":{"type":"string"},"id":{"type":"integer","minimum":1},"view":{"type":"string","enum":["iso","front","right","top"]}},"required":["path"]})",false}
    };
    V list=V::makeArr();for(auto& d:defs){V t=V::makeObj(),schema,annotations=V::makeObj();t.set("name",V::makeStr(d.name));t.set("description",V::makeStr(d.desc));arkjson::parse(d.schema,schema);if(std::string(d.name)=="mesh_raycast"||std::string(d.name)=="export_mesh"||std::string(d.name)=="snapshot"){
        V props=schema.find("properties")?*schema.find("properties"):V::makeObj(),node=V::makeObj();node.set("type",V::makeStr("integer"));node.set("minimum",V::makeNum(1));props.set("node",node);schema.set("properties",props);
    }t.set("inputSchema",schema);annotations.set("readOnlyHint",V::makeBool(d.read));t.set("annotations",annotations);list.arr.push_back(t);}return list;
}
Document makeDemoDocument(){
    Document d;d.setParam("radius","32");d.setParam("tube","11");d.setParam("waves","7");d.setParam("amplitude","0.22");
    Feature f;f.type=FeatureType::ParametricSurface;f.name="Harmonic ring";
    std::string t="tube*(1+amplitude*cos(waves*u+3*v))";
    f.p={{"x","(radius+"+t+"*cos(v))*cos(u)"},{"y",t+"*sin(v)+15"},{"z","(radius+"+t+"*cos(v))*sin(u)"},{"nu","160"},{"nv","64"},{"wrap_u","1"},{"wrap_v","1"}};
    d.addFeature(f);d.recompute();return d;
}
}
