#include "ark/agent.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/io.hpp"
#include "ark/compute.hpp"
#include "ark/notch.hpp"
#include "ark/step_export.hpp"
#include "ark/source_tube.hpp"
#include <stdexcept>
#include <set>
#include <map>
#include <fstream>
namespace ark {
using V=arkjson::Value;
namespace {
const V& field(const V& a,const char* k){auto p=a.find(k);if(!p)throw std::runtime_error(std::string("Missing field: ")+k);return *p;}
std::string str(const V& a,const char* k){auto& p=field(a,k);if(p.t!=V::Str||p.str.empty())throw std::runtime_error(std::string("Expected nonempty string: ")+k);return p.str;}
u64 id(const V& v){if(v.t!=V::Num||!std::isfinite(v.num)||v.num<1||v.num>9007199254740991.||v.num!=std::floor(v.num))throw std::runtime_error("Expected positive integer id");return (u64)v.num;}
std::string expression(const V& v){if(v.t==V::Str && v.str.size()<=2048)return v.str;if(v.t==V::Num&&std::isfinite(v.num))return v.dump();throw std::runtime_error("Expected expression string or finite number");}
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
void editNode(AssemblyNode& n,const V& j){
    if(auto p=j.find("parent"))n.parent=p->t==V::Num&&p->num==0?0:id(*p);
    if(auto p=j.find("feature"))n.feature=p->t==V::Num&&p->num==0?0:id(*p);
    if(auto p=j.find("visible")){if(p->t!=V::Bool)throw std::runtime_error("visible must be boolean");n.visible=p->b;}
    if(j.find("name"))n.name=str(j,"name");
    if(auto p=j.find("p")){if(p->t!=V::Obj)throw std::runtime_error("p must be object");for(auto& kv:p->obj)n.p[kv.first]=expression(kv.second);}
}
V result(V data,bool error=false){V r=V::makeObj(),content=V::makeArr(),item=V::makeObj();item.set("type",V::makeStr("text"));item.set("text",V::makeStr(data.dump()));content.arr.push_back(item);r.set("content",content);r.set("structuredContent",data);r.set("isError",V::makeBool(error));return r;}
void validated(Document& d){
    if(d.features.size()>1024||d.params.size()>256||d.sketches.size()>64)throw std::runtime_error("Document capacity exceeded");
    std::vector<AssemblyPlacement> placements;std::string error;if(!resolveAssembly(d,placements,error))throw std::runtime_error(error);
    d.recompute();
    if(!d.parameterError.empty())throw std::runtime_error(d.parameterError);
    for(auto& r:d.lastResults)if(!r.ok)throw std::runtime_error("Feature #"+std::to_string(r.id)+": "+r.error);
    for(auto& sk:d.sketches)if(!std::isfinite(sk.residual)||sk.residual>1e-6)throw std::runtime_error("Sketch did not converge: "+sk.name);
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
    V fs=V::makeArr();auto hidden=consumed(document);
    for(auto& f:document.features){V v=V::makeObj();v.set("id",V::makeNum((double)f.id));v.set("name",V::makeStr(f.name));v.set("type",V::makeStr(featureTypeName(f.type)));v.set("output",V::makeBool(!hidden.count(f.id)));fs.arr.push_back(v);}
    r.set("features",fs);r.set("assembly_nodes",V::makeNum(document.assembly.size()));return r;
}
V AgentSession::call(const std::string& tool,const V& args){
    try{
        if(args.t!=V::Obj)throw std::runtime_error("Arguments must be an object");
        if(tool=="doc_info")return result(info());
        if(tool=="doc_get"){V r=info(),j;arkjson::parse(documentToJson(document),j);r.set("document",j);return result(r);}
        if(tool=="eval_expr"){
            bool ok=false;auto v=evalExpr(str(args,"expr"),document.buildEnv(),&ok);
            if(!ok)throw std::runtime_error("Invalid mathematical expression");
            V r=V::makeObj();r.set("value",V::makeNum(v));return result(r);
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
        if(tool=="export_step"){
            auto* feature=document.find(id(field(args,"id")));if(!feature)throw std::runtime_error("Feature not found");
            if(feature->type==FeatureType::SourceTube)return result(exportSourceTubeStep(feature->sourceData,str(args,"path")));
            if(feature->type!=FeatureType::NotchedTube)throw std::runtime_error("STEP export requires a source_tube or notched_tube feature");
            return result(exportNotchStep(notchRequestForFeature(*feature,document.buildEnv()),str(args,"path")));
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
            std::vector<AssemblyPlacement> placements;std::string error;if(!resolveAssembly(document,placements,error))throw std::runtime_error(error);
            V r=info(),nodes=V::makeArr(),bom=V::makeArr();std::map<u64,size_t> quantities;
            for(auto& p:placements){V j=V::makeObj(),matrix=V::makeArr();j.set("node",V::makeNum((double)p.node));j.set("feature",V::makeNum((double)p.feature));j.set("name",V::makeStr(p.name));j.set("visible",V::makeBool(p.visible));
                for(auto x:p.world.m)matrix.arr.push_back(V::makeNum(x));
                j.set("world_column_major",matrix);nodes.arr.push_back(j);++quantities[p.feature];}
            for(auto& kv:quantities){V j=V::makeObj();j.set("feature",V::makeNum((double)kv.first));j.set("name",V::makeStr(document.find(kv.first)->name));j.set("quantity",V::makeNum(kv.second));bom.arr.push_back(j);}
            r.set("instances",nodes);r.set("bom",bom);r.set("mates_solved",V::makeBool(false));return result(r);
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
            V created=V::makeArr(),createdNodes=V::makeArr();
            for(auto& c:commands.arr){
                auto op=str(c,"op");
                if(op=="param.set")next.setParam(str(c,"name"),expression(field(c,"expr")));
                else if(op=="feature.add"){
                    Feature f;auto type=str(c,"type");
                    if(!featureTypeFromName(type,f.type))throw std::runtime_error("Unknown feature type: "+type);
                    if(f.type==FeatureType::SourceTube)throw std::runtime_error("Use import_step to create a verified source tube");
                    f.name=c.find("name")?str(c,"name"):type;
                    if(auto p=c.find("p"))params(f,*p);
                    if(auto b=c.find("bends"))bends(f,*b);
                    if(auto n=c.find("notches"))notches(f,*n);
                    if(auto in=c.find("inputs")){if(in->t!=V::Arr)throw std::runtime_error("inputs must be an array");for(auto& x:in->arr)f.inputs.push_back(id(x));}
                    if(auto mode=c.find("mode")){if(mode->t!=V::Num||mode->num<0||mode->num>2||mode->num!=std::floor(mode->num))throw std::runtime_error("mode must be 0, 1 or 2");f.opInt=(int)mode->num;}
                    created.arr.push_back(V::makeNum((double)next.addFeature(std::move(f))));
                }
                else if(op=="feature.edit"){
                    auto* f=next.find(id(field(c,"id")));if(!f)throw std::runtime_error("Feature not found");
                    if(f->type==FeatureType::SourceTube&&(c.find("p")||c.find("bends")||c.find("notches")))throw std::runtime_error("Imported source geometry is read-only; reimport changed STEP");
                    if(auto p=c.find("p"))params(*f,*p);
                    if(auto b=c.find("bends"))bends(*f,*b);
                    if(auto n=c.find("notches"))notches(*f,*n);
                    if(c.find("name"))f->name=str(c,"name");
                }
                else if(op=="feature.remove"){if(!next.remove(id(field(c,"id"))))throw std::runtime_error("Feature not found");}
                else if(op=="sketch.set"){
                    Sketch sk;if(!sketchFromJson(field(c,"sketch").dump(),sk,&error))throw std::runtime_error(error);
                    next.setSketch(str(c,"name"),std::move(sk));
                }
                else if(op=="assembly.add"){
                    AssemblyNode n;n.id=next.nextAssemblyId_++;n.name="Component";editNode(n,c);next.assembly.push_back(n);createdNodes.arr.push_back(V::makeNum((double)n.id));
                }
                else if(op=="assembly.edit"||op=="assembly.remove"){
                    u64 key=id(field(c,"id"));auto it=std::find_if(next.assembly.begin(),next.assembly.end(),[&](const AssemblyNode& n){return n.id==key;});
                    if(it==next.assembly.end())throw std::runtime_error("Assembly node not found");
                    if(op=="assembly.edit")editNode(*it,c);else next.assembly.erase(it);
                }
                else throw std::runtime_error("Unknown operation: "+op);
            }
            validated(next); // one evaluation for the entire batch, commit only on success
            remember();document=std::move(next);++revision;
            V r=info();r.set("created_ids",created);r.set("created_node_ids",createdNodes);return result(r);
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
        if(tool=="mesh_inspect")return result(meshCache(args).report.json());
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
            auto path=str(args,"path");Mesh m=selectedMesh(document,args);SnapCam camera;
            camera.dist=std::max(m.computeBBox().diagonal()*2.,1.);
            if(!writePNG(path.c_str(),renderSnapshot(m,1280,800,camera)))throw std::runtime_error("Snapshot failed");
            V r=V::makeObj();r.set("path",V::makeStr(path));return result(r);
        }
        throw std::runtime_error("Unknown tool: "+tool);
    }catch(const std::exception& e){V r=V::makeObj();r.set("error",V::makeStr(e.what()));r.set("revision",V::makeNum((double)revision));return result(r,true);}
}
V AgentSession::tools(){
    struct Def{const char* name;const char* desc;const char* schema;bool read;};
    const Def defs[]={
        {"doc_info","Compact document summary with revision and stable feature IDs.",R"({"type":"object"})",true},
        {"doc_get","Read full parametric document, including expressions and revision.",R"({"type":"object"})",true},
        {"doc_apply","Atomic batch; evaluate once, roll back on any failure. Operations: param.set{name,expr}, feature.add{type,name,p,inputs,mode}, feature.edit{id,p,name}, feature.remove{id}, sketch.set{name,sketch}, assembly.add{name,parent,feature,p,visible}, assembly.edit{id,parent,feature,p,visible}, assembly.remove{id}. feature=0 is a group; placements are rigid tx/ty/tz/rx/ry/rz. Tube feature accepts p{section:round|rect,od,width,height,wall,tail,chord_tol,sides} plus bends:[{feed,radius,angle,roll}] in mm/degrees. Feature types: notched_tube{p:{width_mm,height_mm,wall_mm,length_mm,hinge_offset_mm},notches:[{station,angle,face:bottom|top|left|right}]}, tube, box{dx,dy,dz}, cylinder{r,h}, sphere{r}, cone{rb,rt,h}, torus{R,r}, extrude{shape,w,d,height,sketch}, revolve{shape,w,d,angle,sketch}, transform{tx,ty,tz,rx,ry,rz}, boolean{mode:0 union/1 cut/2 intersect}, pattern{count,dx,dy,dz}, surface{x,y,z,u0,u1,v0,v1,nu,nv,wrap_u,wrap_v}. Geometry parameters belong in p and accept math expressions. Transforms/patterns take one input, booleans two. Use expected_revision to reject stale writes. Angles in degrees except trig expressions in radians. Surface coordinates use u,v; periodic seams are checked.",R"({"type":"object","properties":{"expected_revision":{"type":"integer","minimum":0},"commands":{"type":"array","minItems":1,"maxItems":128,"items":{"type":"object","properties":{"op":{"type":"string","enum":["param.set","feature.add","feature.edit","feature.remove","sketch.set","assembly.add","assembly.edit","assembly.remove"]}},"required":["op"]}}},"required":["commands"]})",false},
        {"doc_undo","Undo the previous committed transaction; revision increases.",R"({"type":"object"})",false},
        {"doc_redo","Redo a transaction after undo.",R"({"type":"object"})",false},
        {"eval_expr","Evaluate finite arithmetic and trigonometry with named parameters; trig uses radians, deg() converts degrees.",R"({"type":"object","properties":{"expr":{"type":"string"}},"required":["expr"]})",true},
        {"mesh_inspect","Compact indexed topology audit: bounds, area, edge counts, components and signed volume for closed oriented edges. Does not certify solids or detect self intersections. Omit id to inspect final outputs only.",R"({"type":"object","properties":{"node":{"type":"integer","minimum":1},"id":{"type":"integer","minimum":1}}})",true},
        {"mesh_get","Explicitly retrieve mesh positions and triangle indices; potentially large. Prefer mesh_inspect for reasoning.",R"({"type":"object","properties":{"node":{"type":"integer","minimum":1},"id":{"type":"integer","minimum":1}}})",true},
        {"mesh_raycast","Nearest triangle hit using a cached AABB hierarchy. Direction is normalized; distances in mm. Triangle IDs last for this tessellation/revision only.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"origin":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"direction":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["origin","direction"]})",true},
        {"assembly_info","Hierarchy placements and BOM quantities by shared feature definition. Hidden instances remain in BOM. Rigid transforms, no mate solver.",R"({"type":"object"})",true},
        {"tube_inspect","Ideal centerline length, section area and bend table. No springback, weld allowance or material compensation.",R"({"type":"object","properties":{"node":{"type":"integer","minimum":1},"id":{"type":"integer","minimum":1}},"required":["id"]})",true},
        {"source_inspect","Read source projection evidence and exact opening coordinates without large mesh buffers. Optional progress returns finite-hinge display transforms; source geometry stays read-only.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"progress":{"type":"number","minimum":0,"maximum":1}},"required":["id"]})",true},
        {"import_step","Import and geometrically verify a planar tube STEP with cylindrical retained-wall hinges. Preserves exact hole/cutout B-reps, projects rigid spans, checks refolding and imports native preview. Optional replace:true starts a new document. Fails closed outside supported domain.",R"({"type":"object","properties":{"path":{"type":"string"},"replace":{"type":"boolean"}},"required":["path"]})",false},
        {"export_step","Export a source_tube or notched_tube in its straight pose to validated STEP using optional OCCT Python worker. Animation never affects the output. Writes STEP, .arkcut and .validation.json. Ideal hinge preview, not production qualification.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"path":{"type":"string"}},"required":["id","path"]})",false},
        {"notch_inspect","Read a notched_tube feature's straight request and ideal cut plan. Optional progress returns sequential display matrices and bounds without document mutation.",R"({"type":"object","properties":{"progress":{"type":"number","minimum":0,"maximum":1},"id":{"type":"integer","minimum":1}},"required":["id"]})",true},
        {"notch_plan","Ideal preview of a straight hollow rectangular tube with V cuts leaving one wall. Requires explicit hinge offset inside that wall. Not a source reconstruction or production-approved laser program. Returns cutter polygons, analytic removed volume and cut-face closure proof; finite-wall strain is not validated.",R"({"type":"object","properties":{"width_mm":{"type":"number"},"height_mm":{"type":"number"},"wall_mm":{"type":"number"},"length_mm":{"type":"number"},"hinge_offset_mm":{"type":"number"},"notches":{"type":"array","items":{"type":"object","properties":{"station_mm":{"type":"number"},"angle_deg":{"type":"number"},"hinge_face":{"type":"string","enum":["bottom","top","left","right"]}},"required":["station_mm","angle_deg","hinge_face"]}}},"required":["width_mm","height_mm","wall_mm","length_mm","hinge_offset_mm","notches"]})",true},
        {"compute_info","Actual CUDA availability, selected device, transfer-inclusive last execution and CPU fallback reason.",R"({"type":"object"})",true},
        {"compute_benchmark","Bounded CPU/CUDA benchmark (default 1M vertices) with transfer, synchronization and numerical parity; initializes GPU runtime.",R"({"type":"object","properties":{"vertices":{"type":"integer","minimum":1,"maximum":4000000}}})",true},
        {"doc_save","Save .ark or legacy .arkjson parametric document as versioned JSON.",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})",false},
        {"doc_load","Load .ark/.arkjson into this session, validate and commit atomically.",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})",false},
        {"export_mesh","Export final output bodies or a selected id as STL/OBJ; construction inputs are excluded.",R"({"type":"object","properties":{"path":{"type":"string"},"format":{"type":"string","enum":["stl","obj"]},"id":{"type":"integer","minimum":1}},"required":["path","format"]})",false},
        {"snapshot","Render final output meshes to a 1280x800 PNG for visual feedback.",R"({"type":"object","properties":{"path":{"type":"string"},"id":{"type":"integer","minimum":1}},"required":["path"]})",false}
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
