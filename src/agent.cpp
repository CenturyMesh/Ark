#include <tuple>
#include <atomic>
#include "ark/agent_activity.hpp"
#include "ark/agent.hpp"
#include "ark/agent_response_032d.hpp"
#include "ark/document_container.hpp"
#include "ark/authoring_024b.hpp"
#include "ark/motion_graph.hpp"
#include "ark/exploded_view.hpp"
#include "ark/appearance_024b.hpp"
#include "ark/skeleton.hpp"
#include "ark/history_authoring.hpp"
#include "ark/assembly_authoring.hpp"
#include "ark/drawings.hpp"
#include "ark/authoring_023b.hpp"
#include "ark/design_containers.hpp"
#include "ark/holes_orchestrator.hpp"
#include "ark/collision.hpp"
#include "ark/mesh_model.hpp"
#include "ark/modeling_031b.hpp"
#include "ark/datum_authoring.hpp"
#include "ark/spatial_sketch.hpp"
#include "ark/bom.hpp"
#include "ark/manufacturing.hpp"
#include "ark/component_library_tools.hpp"
#include "ark/project_memory_tools.hpp"
#include "ark/agent_features.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/mesh_model.hpp"
#include "ark/io.hpp"
#include "ark/snapshot_032d.hpp"
#include "ark/media_044b.hpp"
#include "ark/compute.hpp"
#include "ark/notch.hpp"
#include "ark/step_export.hpp"
#include "ark/source_tube.hpp"
#include "ark/sketch.hpp"
#include "ark/onboarding.hpp"
#include "ark/native_backend.hpp"
#include "ark/native_features.hpp"
#include "ark/frame.hpp"
#include "ark/references.hpp"
#include "ark/assembly_io.hpp"
#include "ark/sheet_commands.hpp"
#include "ark/sheet_preview.hpp"
#include "ark/electronics.hpp"
#include "ark/usage.hpp"
#include "ark/iteration_history.hpp"
#include "ark/stock_preview.hpp"
#include "ark/suspension.hpp"
#include "ark/parametric_hud.hpp"
#include <stdexcept>
#include <set>
#include <map>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <algorithm>
namespace ark {
using V=arkjson::Value;
namespace {
const V& field(const V& a,const char* k){auto p=a.find(k);if(!p)throw std::runtime_error(std::string("Missing field: ")+k);return *p;}
std::string str(const V& a,const char* k){auto& p=field(a,k);if(p.t!=V::Str||p.str.empty())throw std::runtime_error(std::string("Expected nonempty string: ")+k);return p.str;}
std::string skeletonSource(const V& args){const auto& value=field(args,"source");if(value.t!=V::Str||value.str.size()>65536)throw std::runtime_error("Skeleton source must be a string of at most 64 KiB");return value.str;}
u64 id(const V& v){if(v.t!=V::Num||!std::isfinite(v.num)||v.num<1||v.num>9007199254740991.||v.num!=std::floor(v.num))throw std::runtime_error("Expected positive integer id");return (u64)v.num;}
std::string expression(const V& v){if(v.t==V::Str && v.str.size()<=2048)return v.str;if(v.t==V::Num&&std::isfinite(v.num))return v.dump();throw std::runtime_error("Expected expression string or finite number");}
struct DensityValue { bool present=false; bool ok=false; double gramsPerCm3=0; std::string reason; };
DensityValue featureDensity(const Document& document,const Feature& feature, const ExprEnv& env) {
    auto found=feature.p.find("density");
    if(found==feature.p.end()||found->second.empty()){
        if(!feature.material.empty())for(const Material& material:document.materials)if(material.name==feature.material&&material.densityGcm3>0)return {true,true,material.densityGcm3,{}};
        return {};
    }
    DensityValue value;value.present=true;
    bool ok=false;const double number=evalExpr(found->second,env,&ok);
    if(!ok||!std::isfinite(number)||!(number>0)||number>100) value.reason="Density must be a positive finite number of grams per cubic centimetre";
    else { value.ok=true;value.gramsPerCm3=number; }
    return value;
}
double gravity(const V& args){
    const V* value=args.find("gravity_m_s2");
    if(!value)return 9.80665;
    if(value->t!=V::Num||!std::isfinite(value->num)||value->num<0||value->num>100)throw std::runtime_error("gravity_m_s2 must be finite and 0..100");
    return value->num;
}
void setMass(V& result,double grams,double gravityMs2){result.set("mass_g",V::makeNum(grams));result.set("mass_kg",V::makeNum(grams/1000.0));result.set("gravity_m_s2",V::makeNum(gravityMs2));result.set("weight_N",V::makeNum(grams*gravityMs2/1000.0));}
// A native worker timeout is not a missing solid: keep the resource-limit
// wording (RESOURCE_LIMIT) so a client does not retry the identical request.
std::string exactSolidFailure(const std::exception& error){
    const std::string reason=error.what();
    if(reason.find("worker limit")!=std::string::npos)return "Exact mass properties exceeded the native OCCT worker limit; repeating the same request will exceed it again";
    return "Density needs an exact solid";
}
V centroidOf(Document& document, u64 feature) {
    ensureNativeBody(document,feature,document.evalBodies);
    const V* brep=nullptr;
    for(const auto& body:document.evalBodies) if(body.id==feature) { brep=body.nativeData.find("brep_base64"); break; }
    if(!brep||brep->t!=V::Str||brep->str.empty()) throw std::runtime_error("Density needs an exact solid");
    const auto digest=iterationSha256(brep->str);if(const auto cached=document.centroidCache.find(feature);cached!=document.centroidCache.end()&&cached->second.first==digest)return cached->second.second;
    V model=V::makeObj();model.set("brep",*brep);
    V request=V::makeObj();request.set("operation",V::makeStr("solid_centroid"));request.set("request",std::move(model));
    V measured=nativeWorkerRequest(request);if(document.centroidCache.size()>=256)document.centroidCache.erase(document.centroidCache.begin());document.centroidCache[feature]={digest,measured};return measured;
}
std::string absolutePath(const std::string& name){auto path=std::filesystem::absolute(std::filesystem::u8path(name));auto utf=path.u8string();return std::string(utf.begin(),utf.end());}
std::string failedPostconditionSummary(const V& report){
    // Name the failing checks in the error text: logs, memory and compacted
    // history keep this text even when the conditions object is dropped.
    std::string text="Required postconditions failed";size_t named=0,failed=0;
    if(const auto* rows=report.find("checks");rows&&rows->t==V::Arr)for(const auto& row:rows->arr){
        const auto* passed=row.find("passed");if(!passed||passed->t!=V::Bool||passed->b)continue;++failed;if(named==4||text.size()>600)continue;
        text+=(named++?"; ":": ")+(row.find("id")&&row.find("id")->t==V::Str?row.find("id")->str:std::string("check"));
        if(const auto* observed=row.find("observed")){auto value=observed->dump();if(value.size()>48)value=value.substr(0,48)+"...";text+=" observed "+value;}
        if(const auto* target=row.find("target"))text+=" target "+target->dump();
        else if(row.find("min")&&row.find("max"))text+=" range ["+row.find("min")->dump()+", "+row.find("max")->dump()+"]";
        if(const auto* delta=row.find("required_delta"))text+=" delta "+delta->dump();
    }
    if(failed>named)text+="; "+std::to_string(failed-named)+" more in conditions.checks";
    return text;
}
void nativeSpecAllowed(FeatureType type,const V& spec){
    if(!isNativeFeature(type)&&type!=FeatureType::Suspension&&type!=FeatureType::MeshEdit)throw std::runtime_error(std::string("Feature type ")+featureTypeName(type)+" does not accept a native object; its inputs go in p. Exact revolves use type revolve_surface; native solid primitives and patterns use type native_solid with native.op");
    if(spec.t!=V::Obj)throw std::runtime_error("native must be an object for a native feature");
}
std::string unknownFeatureType(const std::string& type){
    // Common native_solid ops are often sent as feature types.
    static const std::set<std::string> nativeOps={"circular_pattern","linear_pattern","mirror","common","hole","fillet","chamfer","shell","offset","draft","round_tube","rect_tube","split_plane"};
    if(nativeOps.count(type))return "Unknown feature type: "+type+"; it is a native_solid operation: use type native_solid with native:{op:\""+type+"\", ...} and its inputs";
    std::string names;for(int i=0;i<=(int)FeatureType::MeshNet;++i){FeatureType known;const char* name=featureTypeName((FeatureType)i);if(name&&*name&&featureTypeFromName(name,known)&&known==(FeatureType)i){if(!names.empty())names+=", ";names+=name;}}
    return "Unknown feature type: "+type+"; accepted types: "+names;
}
void params(Feature& f,const V& p){if(p.t!=V::Obj)throw std::runtime_error("p must be an object");for(auto& kv:p.obj){
    if(kv.second.t==V::Bool)throw std::runtime_error("Feature parameter p."+kv.first+" must be an expression string or finite number; flags use 1 or 0, not true/false");
    try{f.p[kv.first]=expression(kv.second);}catch(const std::exception& e){throw std::runtime_error("Feature parameter p."+kv.first+": "+e.what());}}}
void featureAppearance(Feature& feature,const V& command){
    if(const V* intent=command.find("manufacturing")){
        std::string error;if(!applyManufacturingIntent(feature,*intent,error))throw std::runtime_error(error);
    }
    if(const V* material=command.find("material")){
        if(material->t==V::Null)feature.material.clear();
        else if(material->t==V::Str&&!material->str.empty()&&material->str.size()<=128)feature.material=material->str;
        else throw std::runtime_error("Feature material must be a name or null");
    }
    if(const V* patch=command.find("appearance")){
        if(patch->t==V::Null)feature.appearance=V{};
        else {Appearance check;std::string error;if(!applyAppearancePatch(check,*patch,error))throw std::runtime_error(error);feature.appearance=*patch;}
    }
}
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
std::string featureNameList(const Document& d){std::string names;size_t listed=0;for(const auto& feature:d.features){if(feature.name.empty())continue;if(listed==24||names.size()>480){names+=", ...";break;}names+=(listed++?", ":"")+feature.name;}return names.empty()?std::string("none"):names;}
u64 namedFeature(const Document& d,const std::string& name){u64 found=0;int count=0;for(const auto& feature:d.features)if(feature.name==name){found=feature.id;++count;}if(count!=1)throw std::runtime_error(count?"Ambiguous feature name: "+name:"Feature name not found: "+name+"; existing feature names: "+featureNameList(d));return found;}
u64 featureRef(const Document& d,const V& value){if(value.t==V::Num)return id(value);if(value.t==V::Str)return namedFeature(d,value.str);throw std::runtime_error("Feature reference must be an id or a unique name");}
void bindProjectFeature(const Document& document,V& entity){
    auto kind=entity.find("kind");if(!kind||kind->t!=V::Str||kind->str!="project")return;
    if(entity.find("source_sketch"))return;
    auto feature=entity.find("feature");if(!feature)throw std::runtime_error("Project needs a feature id");
    if(feature->t==V::Str)entity.set("feature",V::makeNum((double)namedFeature(document,feature->str)));
    else if(feature->t!=V::Num)throw std::runtime_error("Project needs a feature id");
}
void bindSketchProjects(const Document& document,V& sketch){
    if(sketch.t!=V::Obj)return;
    for(auto& field:sketch.obj){
        if(field.first!="entities"||field.second.t!=V::Arr)continue;
        for(auto& entity:field.second.arr)if(entity.t==V::Obj)bindProjectFeature(document,entity);
    }
}
u64 namedNode(const Document& d,const std::string& name){u64 found=0;int count=0;for(const auto& node:d.assembly)if(node.name==name){found=node.id;++count;}if(count!=1)throw std::runtime_error(count?"Ambiguous assembly instance name: "+name:"Assembly instance name not found: "+name);return found;}
void bindEndpoint(const Document& d,V& mate,const char* key){auto endpoint=mate.find(key);if(!endpoint||endpoint->t!=V::Str)return;mate.set(key,V::makeNum((double)namedNode(d,endpoint->str)));}
u64 nodeRef(const Document& d,const V& value){if(value.t==V::Num)return id(value);if(value.t==V::Str)return namedNode(d,value.str);throw std::runtime_error("Assembly node reference must be an id or a unique name");}
AssemblyManipulationPreview driveAssembly(Document& document,const V& args){
    if(args.t!=V::Obj)throw std::runtime_error("Assembly drive must be an object");
    for(const auto& item:args.obj)if(item.first!="op"&&item.first!="driver"&&item.first!="mate"&&item.first!="mode"&&item.first!="delta")throw std::runtime_error("Unknown assembly drive field: "+item.first);
    const u64 driver=nodeRef(document,field(args,"driver"));
    const u64 mate=args.find("mate")?id(*args.find("mate")):0;
    const std::string mode=args.find("mode")?str(args,"mode"):"rotate";
    bool ok=false;const double delta=evalExpr(expression(field(args,"delta")),document.buildEnv(),&ok);
    if(!ok||!std::isfinite(delta))throw std::runtime_error("Assembly drive delta must evaluate to a finite number");
    AssemblyManipulationProgram program;AssemblyManipulationPreview preview;std::string error;
    if(!buildAssemblyManipulation(document,program,error)||!previewAssemblyManipulation(program,driver,mate,mode,delta,preview,error))throw std::runtime_error(error);
    return preview;
}
V driveReport(const AssemblyManipulationPreview& preview,u64 revision){
    V report=V::makeObj(),joint=V::makeObj(),instances=V::makeArr(),moving=V::makeArr();
    joint.set("mate",V::makeNum((double)preview.joint.mate));joint.set("type",V::makeStr(preview.joint.type));joint.set("mode",V::makeStr(preview.joint.mode));
    joint.set("moving_endpoint",V::makeNum((double)preview.joint.movingEndpoint));joint.set("constrained",V::makeBool(preview.joint.constrained));
    joint.set("position",V::makeNum(preview.joint.position));joint.set("unit",V::makeStr(preview.joint.mode=="rotate"?"deg":"mm"));joint.set("limited",V::makeBool(preview.joint.limited));joint.set("coordinate_sign",V::makeNum(preview.joint.coordinateSign));
    if(preview.joint.limited){joint.set("min",V::makeNum(preview.joint.min));joint.set("max",V::makeNum(preview.joint.max));}
    for(const auto& entry:std::initializer_list<std::pair<const char*,Vec3>>{{"origin_mm",preview.joint.origin},{"axis",preview.joint.axis}}){V xyz=V::makeArr();for(double value:{entry.second.x,entry.second.y,entry.second.z})xyz.arr.push_back(V::makeNum(value));joint.set(entry.first,xyz);}
    for(u64 node:preview.joint.movingNodes)moving.arr.push_back(V::makeNum((double)node));joint.set("moving_nodes",moving);
    for(const auto& placement:preview.placements){V row=V::makeObj(),matrix=V::makeArr();row.set("node",V::makeNum((double)placement.node));row.set("feature",V::makeNum((double)placement.feature));row.set("copy",V::makeNum(placement.copy));row.set("name",V::makeStr(placement.name));row.set("visible",V::makeBool(placement.visible));for(double value:placement.world.m)matrix.arr.push_back(V::makeNum(value));row.set("world_column_major",matrix);instances.arr.push_back(row);}
    report.set("joint",joint);report.set("solver",V::makeStr(preview.joint.constrained?"constrained_continuation":"rigid_side"));report.set("continuation_steps",V::makeNum(preview.continuationSteps));report.set("coupling_residuals",preview.couplings.t==V::Arr?preview.couplings:V::makeArr());report.set("delta",V::makeNum(preview.delta));report.set("revision",V::makeNum((double)revision));report.set("instances",instances);V diagnostics=assemblyDiagnosticsJson(preview.diagnostics);diagnostics.set("rank_scope",V::makeStr("resolved baseline local numerical geometric equality Jacobian; stop inequalities excluded; residuals measured at preview pose"));report.set("diagnostics",diagnostics);report.set("commands",preview.commands);report.set("document_unchanged",V::makeBool(true));report.set("dynamics_checked",V::makeBool(false));return report;
}
u64 mateKey(const Document& d,const V& command){
    if(const V* supplied=command.find("id")){if(supplied->t==V::Str)throw std::runtime_error("Mate ids are numeric; pass a and b to select the mate");return id(*supplied);}
    V probe=command;bindEndpoint(d,probe,"a");bindEndpoint(d,probe,"b");
    const u64 a=id(field(probe,"a")),b=id(field(probe,"b"));u64 found=0;int matches=0;
    for(const auto& mate:d.mates)if((mate.a==a&&mate.b==b)||(mate.a==b&&mate.b==a)){found=mate.id;++matches;}
    if(matches!=1)throw std::runtime_error(matches?"Ambiguous mate endpoints":"Mate not found");
    return found;
}
const char* axisConvention(){return "Analytic cylinder, cone and torus meshes run along +Y. A native_solid cylinder, cone or torus uses local +Z. Promoting an analytic cylinder, cone or torus applies Rx(-90 degrees), so the exact B-rep axis becomes +Z. Mate references Z, origin and XY use the instance frame +Z. Semantic reference axis is the exact B-rep cylinder axis. The analytic mesh axis is not retargeted. A round tube exact axis is its centerline and starts along +X. The tube display mesh uses that same centerline. A rectangular tube exact axis is its centerline and starts along +X. Width lies along the centerline normal and height along the binormal.";}
void editNode(AssemblyNode& n,const V& j,const Document& doc){
    if(auto p=j.find("parent")){if(p->t==V::Num&&p->num==0)n.parent=0;else if(p->t==V::Str)n.parent=namedNode(doc,p->str);else n.parent=id(*p);}
    if(auto p=j.find("feature")){if(p->t==V::Num&&p->num==0)n.feature=0;else if(p->t==V::Str)n.feature=namedFeature(doc,p->str);else n.feature=id(*p);}
    if(auto p=j.find("visible")){if(p->t!=V::Bool)throw std::runtime_error("visible must be boolean");n.visible=p->b;}
    if(auto p=j.find("fixed")){if(p->t!=V::Bool)throw std::runtime_error("fixed must be boolean");n.fixed=p->b;}
    if(j.find("name"))n.name=str(j,"name");
    if(auto p=j.find("p")){if(p->t!=V::Obj)throw std::runtime_error("p must be object");for(auto& kv:p->obj)n.p[kv.first]=expression(kv.second);}
    if(auto pattern=j.find("pattern")){
        readAssemblyPattern(n.pattern,*pattern);
        if(n.pattern.active)bindAssemblyPatternSource(doc,n.pattern);
    }
    if(n.pattern.active&&!n.feature)throw std::runtime_error("Instance pattern requires a part");
}
V result(V data,bool error=false){V r=V::makeObj(),content=V::makeArr(),item=V::makeObj();item.set("type",V::makeStr("text"));item.set("text",V::makeStr(data.dump()));content.arr.push_back(item);r.set("content",content);r.set("structuredContent",data);r.set("isError",V::makeBool(error));return r;}
void validatedMaterials(const Document& d){
    validateAppearanceAuthoring024B(d,false);
    if(d.features.size()>1024||d.params.size()>256||d.sketches.size()>64||d.planes.size()>128||d.canvases.size()>8||d.materials.size()>256)throw std::runtime_error("Document capacity exceeded");
    for(const auto& material:d.materials){std::string reason;if(!validateMaterial(material,reason))throw std::runtime_error(reason);}
    for(const auto& feature:d.features){
        if(!feature.material.empty()&&std::none_of(d.materials.begin(),d.materials.end(),[&](const Material& material){return material.name==feature.material;}))throw std::runtime_error("Feature references a missing material: "+feature.material);
        if(feature.appearance.t==V::Obj){Appearance appearance;std::string reason;if(!applyAppearancePatch(appearance,feature.appearance,reason))throw std::runtime_error(reason);}
    }
}
void validated(Document& d){
    validatedMaterials(d);
    for(const auto& node:d.assembly)if(const auto* definition=d.find(node.feature);definition&&definition->suppressed)
        throw std::runtime_error("Assembly occurrence requires a suppressed definition; remove its occurrences before suppressing, or use assembly visibility for display");
    validateAssemblyAuthoring(d);
    verifyAssemblyExternalReferences(d);
    std::string manufacturingError;if(!validateManufacturingRoutes(d,manufacturingError))throw std::runtime_error(manufacturingError);
    if(std::any_of(d.assembly.begin(),d.assembly.end(),[](const AssemblyNode& n){return specializedAssemblyPattern(n.pattern);})){std::string patternError;if(!validateAssemblyPatternAuthoring(d,patternError))throw std::runtime_error(patternError);}
    d.recompute();
    if(!d.parameterError.empty())throw std::runtime_error(d.parameterError);
    for(auto& r:d.lastResults)if(!r.ok)throw std::runtime_error("Feature #"+std::to_string(r.id)+": "+r.error);
    for(auto& sk:d.sketches)if(!sk.error.empty()||!std::isfinite(sk.residual)||sk.residual>1e-6)throw std::runtime_error("Sketch '"+sk.name+"': "+sk.error);
    validateAppearanceAuthoring024B(d,true);
    std::vector<AssemblyPlacement> placements;std::string error;if(!resolveAssembly(d,placements,error))throw std::runtime_error(error);
}
std::set<u64> consumed(const Document& d){return consumedFeatureInputs(d);}
struct MeshSelection { u64 feature=0; u64 node=0; int copy=-1; std::string nodeName; bool useNode=false; u64 cacheKey=0; };
MeshSelection meshSelection(const Document& d,const V& args){
    MeshSelection s;
    if(args.find("id")&&args.find("node"))throw std::runtime_error("Select id or node, not both");
    if(args.find("copy")&&!args.find("node"))throw std::runtime_error("copy requires a node");
    if(auto copy=args.find("copy")){
        if(copy->t!=V::Num||copy->num<0||copy->num>31||copy->num!=std::floor(copy->num))throw std::runtime_error("copy must be an integer from 0 to 31");
        s.copy=(int)copy->num;
    }
    if(auto node=args.find("node")){
        if(node->t==V::Str){s.node=namedNode(d,node->str);s.nodeName=node->str;}
        else s.node=id(*node);
        if(s.node>=(1ull<<56))throw std::runtime_error("Assembly node id does not fit the mesh cache");
        s.useNode=true;
        s.cacheKey=s.node|(1ull<<63);
        if(s.copy>=0)s.cacheKey|=(u64)(s.copy+1)<<57;
    }else if(auto feature=args.find("id")){s.feature=id(*feature);s.cacheKey=s.feature;}
    return s;
}
Mesh selectedMesh(Document& d,const V& args,std::vector<AssemblyPlacement>* captured=nullptr,std::vector<SnapshotMaterial>* styles=nullptr,SnapshotAppearanceOptions044B* appearance=nullptr){
    MeshSelection sel=meshSelection(d,args);
    Mesh m;
    std::unordered_map<u64,std::vector<SnapshotMaterial>> bodyStyles;
    auto append=[&](const Body& body,const Mesh& part,const Mat4& world=Mat4::identity()){
        if(styles){auto found=bodyStyles.find(body.id);if(found==bodyStyles.end())found=bodyStyles.emplace(body.id,snapshotBodyMaterials(d,body,part,appearance)).first;const size_t first=styles->size();styles->insert(styles->end(),found->second.begin(),found->second.end());if(appearance)placeSnapshotMaterials044B(*styles,first,world);}
        m.append(part,world);
    };
    if(!d.assembly.empty()&&!sel.feature){
        std::vector<AssemblyPlacement> placements;std::string error;if(!resolveAssembly(d,placements,error))throw std::runtime_error(error);
        std::unordered_map<u64,Mesh> parts;bool placed=false;
        for(auto& p:placements){
            if(sel.useNode&&(p.node!=sel.node||(sel.copy>=0&&p.copy!=sel.copy)))continue;
            if(sel.useNode)placed=true;
            if(!p.visible)continue;
            auto part=parts.find(p.feature);if(part==parts.end())for(auto& b:d.evalBodies)if(b.id==p.feature){part=parts.emplace(p.feature,b.tessellate(d.tessOpt)).first;break;}
            if(part!=parts.end()){for(const auto& body:d.evalBodies)if(body.id==p.feature){append(body,part->second,p.world);break;}if(captured)captured->push_back(p);}
        }
        if(sel.useNode&&!placed)throw std::runtime_error("Assembly copy is not placed");
    }else{
        if(sel.useNode)throw std::runtime_error("No assembly instance selected");
        auto hidden=consumed(d);for(auto& b:d.evalBodies)if(sel.feature?b.id==sel.feature:!hidden.count(b.id))append(b,b.tessellate(d.tessOpt));
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
    if(missing||triangles>(force?120000u:8000u)){report.set("interference_checked",V::makeBool(false));report.set("interference",pairs);report.set("visible_triangles",V::makeNum((double)triangles));report.set("interference_note",V::makeStr(missing?"An instance has no display mesh.":force?"Display-mesh interference was skipped: the visible instances exceed even the raised 120000-triangle budget. Check specific pairs with collision_inspect or assembly_info clearance instead.":"Display-mesh interference was skipped because the visible instances exceed the triangle budget. Pass interference:true to raise it to 120000 triangles, or check specific pairs with collision_inspect."));return report;}
    report.set("interference_checked",V::makeBool(true));
    for(size_t i=0;i<visible.size();++i)for(size_t j=i+1;j<visible.size();++j){
        const auto& a=*visible[i];const auto& b=*visible[j];const Mesh& ma=*meshOf(a.feature);const Mesh& mb=*meshOf(b.feature);
        AABB ba=ma.computeBBox().transformed(a.world),bb=mb.computeBBox().transformed(b.world);if(!boxesOverlap(ba,bb))continue;
        bool meshChecked=ma.triCount()*mb.triCount()<=250000;bool hit=meshChecked?displayMeshesIntersect(ma,a.world,mb,b.world):true;if(!hit)continue;
        V pair=V::makeObj();pair.set("node_a",V::makeNum((double)a.node));pair.set("copy_a",V::makeNum(a.copy));pair.set("node_b",V::makeNum((double)b.node));pair.set("copy_b",V::makeNum(b.copy));pair.set("name_a",V::makeStr(a.name));pair.set("name_b",V::makeStr(b.name));pair.set("method",V::makeStr(meshChecked?"display_mesh":"bounds"));pairs.arr.push_back(pair);
    }
    report.set("interference",pairs);return report;
}
struct PlacedEnd { u64 node=0; int copy=0; const AssemblyPlacement* placement=nullptr; };
PlacedEnd placedEnd(const Document& d,const std::vector<AssemblyPlacement>& placements,const V& spec,const char* key,const char* copyKey){
    const V& value=field(spec,key);PlacedEnd end;const V* copy=nullptr;
    auto nodeOf=[&](const V& item){if(item.t==V::Str)return namedNode(d,item.str);if(item.t==V::Num)return id(item);throw std::runtime_error(std::string("clearance ")+key+" must name an instance");};
    if(value.t==V::Obj){
        if(const V* node=value.find("node"))end.node=nodeOf(*node);
        else if(const V* node=value.find("id"))end.node=nodeOf(*node);
        else throw std::runtime_error(std::string("clearance ")+key+" needs a node");
        copy=value.find("copy");
    }else end.node=nodeOf(value);
    if(const V* extra=spec.find(copyKey))copy=extra;
    if(copy){if(copy->t!=V::Num||copy->num<0||copy->num>31||copy->num!=std::floor(copy->num))throw std::runtime_error("clearance copy must be an integer from 0 to 31");end.copy=(int)copy->num;}
    for(const auto& placement:placements)if(placement.node==end.node&&placement.copy==end.copy){end.placement=&placement;break;}
    if(!end.placement)throw std::runtime_error("Assembly copy is not placed");
    return end;
}
struct ExactPart { bool ok=false; std::string brep; const V* topology=nullptr; std::string reason; };
ExactPart exactPart(Document& d,u64 feature){
    ExactPart part;
    try{
        ensureNativeBody(d,feature,d.evalBodies);
        for(const auto& body:d.evalBodies)if(body.id==feature){
            const V* brep=body.nativeData.find("brep_base64");
            if(!brep||brep->t!=V::Str||brep->str.empty()){part.reason="Feature has no exact B-rep";return part;}
            part.brep=brep->str;part.topology=body.nativeData.find("topology");part.ok=true;return part;
        }
        part.reason="Feature has no evaluated body";
    }catch(const std::exception& error){part.reason=error.what();}
    return part;
}
void attachClearance(Document& d,V& report,const std::vector<AssemblyPlacement>& placements,const V& spec){
    if(spec.t!=V::Obj)throw std::runtime_error("clearance must be an object");
    PlacedEnd a=placedEnd(d,placements,spec,"a","copy_a"),b=placedEnd(d,placements,spec,"b","copy_b");
    ExactPart partA=exactPart(d,a.placement->feature),partB=exactPart(d,b.placement->feature);
    if(!partA.ok||!partB.ok){report.set("clearance_checked",V::makeBool(false));report.set("clearance_reason",V::makeStr(!partA.ok?partA.reason:partB.reason));return;}
    V model=V::makeObj();model.set("a_brep",V::makeStr(partA.brep));model.set("b_brep",V::makeStr(partB.brep));
    if(partA.topology)model.set("a_topology",*partA.topology);if(partB.topology)model.set("b_topology",*partB.topology);
    auto columns=[&](const Mat4& m){V frame=V::makeArr();for(double value:m.m)frame.arr.push_back(V::makeNum(value));return frame;};
    model.set("a_frame",columns(a.placement->world));model.set("b_frame",columns(b.placement->world));
    V request=V::makeObj();request.set("operation",V::makeStr("clearance"));request.set("request",std::move(model));
    V response=nativeWorkerRequest(request);
    for(const char* key:{"clearance_checked","clearance_mm","clearance_state","clearance_method","face_a","face_b","clearance_authority","separation_mm","overlap_volume_mm3","overlap_checked","penetration_estimate_mm","penetration_depth_checked","clearance_is_estimate","distance_tolerance_mm","overlap_tolerance_mm3"})
        if(const V* value=response.find(key))report.set(key,*value);
}
struct MeasureEnd { Mat4 world; std::string reference; bool hasNode=false; bool hasFeature=false; u64 node=0; u64 feature=0; int copy=0; };
Mat4 semanticFrame(Document& document,u64 feature,const std::string& name){
    if(name=="origin"||name=="Z"||name=="XY")return Mat4::identity();
    if(name=="X")return Mat4::rotY(K_HALF_PI);
    if(name=="Y")return Mat4::rotX(-K_HALF_PI);
    if(!feature)throw std::runtime_error("Semantic reference requires a part");
    return resolveGeometryReference(document,feature,name,.5).frame;
}
MeasureEnd measureEnd(Document& document,const std::vector<AssemblyPlacement>& placements,const V& spec){
    if(spec.t!=V::Obj)throw std::runtime_error("measure endpoint must be an object");
    const bool hasNode=spec.find("node")!=nullptr,hasFeature=spec.find("feature")!=nullptr;
    if(hasNode&&hasFeature)throw std::runtime_error("Measure one node or one feature, not both");
    if(!hasNode&&!hasFeature)throw std::runtime_error("Measure endpoint needs a node or a feature");
    MeasureEnd end;end.hasNode=hasNode;end.hasFeature=hasFeature;end.reference="origin";
    if(const V* name=spec.find("reference")){if(name->t!=V::Str||name->str.empty())throw std::runtime_error("measure reference must be a name");end.reference=name->str;}
    bool copySet=false;
    if(const V* value=spec.find("copy")){if(value->t!=V::Num||value->num<0||value->num>31||value->num!=std::floor(value->num))throw std::runtime_error("measure copy must be an integer from 0 to 31");end.copy=(int)value->num;copySet=true;}
    Mat4 placement=Mat4::identity();
    if(hasNode){
        const V& node=*spec.find("node");end.node=node.t==V::Str?namedNode(document,node.str):id(node);
        const AssemblyPlacement* found=nullptr;for(const auto& item:placements)if(item.node==end.node&&item.copy==end.copy)found=&item;
        if(!found)throw std::runtime_error("Assembly copy is not placed");
        placement=found->world;end.feature=found->feature;
    }else{
        if(copySet)throw std::runtime_error("measure copy requires a node");
        const V& feature=*spec.find("feature");end.feature=feature.t==V::Str?namedFeature(document,feature.str):id(feature);
        if(!document.find(end.feature))throw std::runtime_error("Feature not found");
    }
    end.world=placement*semanticFrame(document,end.feature,end.reference);return end;
}
void attachMeasure(Document& document,V& report,const std::vector<AssemblyPlacement>& placements,const V& spec){
    if(spec.t!=V::Obj)throw std::runtime_error("measure must be an object");
    MeasureEnd a=measureEnd(document,placements,field(spec,"a")),b=measureEnd(document,placements,field(spec,"b"));
    const Vec3 originA{a.world.m[12],a.world.m[13],a.world.m[14]},originB{b.world.m[12],b.world.m[13],b.world.m[14]};
    const Vec3 axisA{a.world.m[8],a.world.m[9],a.world.m[10]},axisB{b.world.m[8],b.world.m[9],b.world.m[10]};
    V block=V::makeObj(),frames=V::makeArr();
    block.set("distance_mm",V::makeNum((originB-originA).len()));
    block.set("angle_deg",V::makeNum(rad2deg(std::acos(clamp(axisA.dot(axisB),-1.0,1.0)))));
    auto push=[&](const MeasureEnd& end){V frame=V::makeObj(),origin=V::makeArr(),axis=V::makeArr(),matrix=V::makeArr();
        for(double value:{end.world.m[12],end.world.m[13],end.world.m[14]})origin.arr.push_back(V::makeNum(value));
        for(double value:{end.world.m[8],end.world.m[9],end.world.m[10]})axis.arr.push_back(V::makeNum(value));
        for(double value:end.world.m)matrix.arr.push_back(V::makeNum(value));
        frame.set("origin",origin);frame.set("axis",axis);frame.set("reference",V::makeStr(end.reference));frame.set("world_column_major",matrix);
        if(end.hasNode){frame.set("node",V::makeNum((double)end.node));frame.set("copy",V::makeNum(end.copy));}
        if(end.hasFeature)frame.set("feature",V::makeNum((double)end.feature));
        frames.arr.push_back(frame);};
    push(a);push(b);block.set("frames",frames);block.set("axis_convention",V::makeStr(axisConvention()));report.set("measure",block);
}
}
static Vec3 snapshotVector(const V& value,const char* name){
    if(value.t!=V::Arr||value.arr.size()!=3)throw std::runtime_error(std::string("snapshot camera ")+name+" must contain three finite numbers");
    Vec3 result;double* coordinates[]={&result.x,&result.y,&result.z};
    for(size_t i=0;i<3;++i){const auto& number=value.arr[i];if(number.t!=V::Num||!std::isfinite(number.num)||std::abs(number.num)>1e9)throw std::runtime_error(std::string("snapshot camera ")+name+" must contain three finite numbers");*coordinates[i]=number.num;}
    return result;
}
static V snapshotVectorValue(Vec3 value){V result=V::makeArr();for(double coordinate:{value.x,value.y,value.z})result.arr.push_back(V::makeNum(coordinate));return result;}
static SnapCam configuredSnapshotCamera(const V& args,const AABB& box,std::string& view){
    SnapCam camera;camera.useTarget=true;camera.target=box.valid()?box.center():Vec3{};camera.dist=std::max((box.valid()?box.diagonal():1.)*1.7,1.);
    const V* requested=args.find("view"),*custom=args.find("camera");
    if(requested){if(requested->t!=V::Str)throw std::runtime_error("snapshot view must be iso, front, right, top or custom");view=requested->str;}
    camera.useDirection=true;
    if(custom){
        if(requested&&view!="custom")throw std::runtime_error("snapshot camera requires view custom or an omitted view");
        view="custom";
        if(custom->t!=V::Obj)throw std::runtime_error("snapshot camera must be an object");
        for(const auto& field:custom->obj)if(field.first!="direction"&&field.first!="up"&&field.first!="target_mm"&&field.first!="distance_mm"&&field.first!="fov_deg")throw std::runtime_error("Unknown snapshot camera field: "+field.first);
        const auto* direction=custom->find("direction"),*up=custom->find("up");
        if(!direction||!up)throw std::runtime_error("snapshot camera requires direction and up");
        camera.direction=snapshotVector(*direction,"direction");camera.up=snapshotVector(*up,"up");
        if(const auto* target=custom->find("target_mm"))camera.target=snapshotVector(*target,"target_mm");
        if(const auto* distance=custom->find("distance_mm")){if(distance->t!=V::Num||!std::isfinite(distance->num)||distance->num<=0||distance->num>1e9)throw std::runtime_error("snapshot camera distance_mm must be positive and finite");camera.dist=distance->num;}
        if(const auto* fov=custom->find("fov_deg")){if(fov->t!=V::Num||!std::isfinite(fov->num)||fov->num<5||fov->num>120)throw std::runtime_error("snapshot camera fov_deg must be within 5..120");camera.fovDeg=fov->num;}
    }else if(view=="iso"){camera.direction={1,-1,0.8};camera.up={0,0,1};}
    else if(view=="front"){camera.direction={0,-1,0};camera.up={0,0,1};}
    else if(view=="right"){camera.direction={1,0,0};camera.up={0,0,1};}
    else if(view=="top"){camera.direction={0,0,1};camera.up={0,1,0};}
    else throw std::runtime_error("snapshot view must be iso, front, right, top or custom");
    if(camera.direction.lensq()<1e-20||camera.up.lensq()<1e-20)throw std::runtime_error("snapshot camera direction and up must be nonzero");
    camera.direction=camera.direction.normalized();camera.up=camera.up.normalized();
    if(camera.direction.cross(camera.up).lensq()<1e-10)throw std::runtime_error("snapshot camera direction and up cannot be parallel");
    return camera;
}
V snapshotImageResult(const Image& image,const V& visualEvidence,const std::string& path){
    if(visualEvidence.t!=V::Obj)throw std::runtime_error("Snapshot visual evidence metadata must be an object");
    const auto bytes=encodePNG(image);if(bytes.size()>4*1024*1024)throw std::runtime_error("Snapshot exceeds inline image size limit");
    if(!path.empty()&&!writePNG(path.c_str(),image))throw std::runtime_error("Snapshot failed");
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string encoded;encoded.reserve((bytes.size()+2)/3*4);
    for(size_t i=0;i<bytes.size();i+=3){unsigned packed=(unsigned)bytes[i]<<16;if(i+1<bytes.size())packed|=(unsigned)bytes[i+1]<<8;if(i+2<bytes.size())packed|=bytes[i+2];encoded.push_back(alphabet[(packed>>18)&63]);encoded.push_back(alphabet[(packed>>12)&63]);encoded.push_back(i+1<bytes.size()?alphabet[(packed>>6)&63]:'=');encoded.push_back(i+2<bytes.size()?alphabet[packed&63]:'=');}
    V metadata=visualEvidence;metadata.set("width",V::makeNum(image.w));metadata.set("height",V::makeNum(image.h));V data=V::makeObj();data.set("visual_evidence",metadata);if(!path.empty())data.set("path",V::makeStr(path));data.set("view",field(metadata,"view"));data.set("revision",field(metadata,"revision"));data.set("authority",V::makeStr("display_mesh"));data.set("display_mesh_authoritative",V::makeBool(false));
    if(const auto* feature=metadata.find("feature_id"))data.set("id",*feature);if(const auto* node=metadata.find("occurrence_id"))data.set("node",*node);for(const char* key:{"name","copy"})if(const auto* value=metadata.find(key))data.set(key,*value);
    V output=result(data),pixels=V::makeObj(),content=*output.find("content");pixels.set("type",V::makeStr("image"));pixels.set("mimeType",V::makeStr("image/png"));pixels.set("data",V::makeStr(encoded));content.arr.push_back(pixels);output.set("content",content);return output;
}
AgentSession::Cache& AgentSession::meshCache(const V& args){
    if(cacheRevision_!=revision){cache_.clear();cacheRevision_=revision;}
    const u64 key=meshSelection(document,args).cacheKey;
    auto it=cache_.find(key);if(it!=cache_.end())return it->second;
    Cache c;c.mesh=selectedMesh(document,args);c.report=analyzeMesh(c.mesh);c.bvh=std::make_unique<MeshBVH>(c.mesh);
    return cache_.emplace(key,std::move(c)).first->second;
}
bool metadataOnlyApply(const Document& document,const V& args){
    const auto* requested=args.find("commands");if(args.find("candidate_id")||!requested||requested->t!=V::Arr||requested->arr.empty()||requested->arr.size()>128)return false;
    const auto& commands=*requested;
    return std::all_of(commands.arr.begin(),commands.arr.end(),[&](const V& command){
                if(command.t!=V::Obj)return false;
                const V* operation=command.find("op");if(!operation||operation->t!=V::Str)return false;
                const std::string& op=operation->str;
                if(op=="light_up")return true;
                if(op=="bom.initialize")return command.obj.size()==1;
                if(op=="image_asset.set"||op=="image_asset.remove"||op=="stamp.set"||op=="stamp.remove"||op=="face_appearance.set"||op=="face_appearance.remove")return true;
                if(op!="material.set"&&op!="material.remove"&&op!="feature.edit")return false;
                if(op=="material.remove"||(op=="material.set"&&(command.find("density")||command.find("density_kg_m3"))))
                    for(const auto& feature:document.features)if(feature.type==FeatureType::ConstructionFrame)return false;
                for(const auto& field:command.obj){
                    const std::string& key=field.first;
                    if(key=="op")continue;
                    if(op=="material.set"&&(key=="name"||key=="density"||key=="density_kg_m3"||key=="physical"||key=="appearance"))continue;
                    if(op=="material.remove"&&key=="name")continue;
                    if(op=="feature.edit"&&(key=="id"||key=="material"||key=="appearance"||key=="part"))continue;
                    return false;
                }
                return true;
    });
}
void AgentSession::remember(bool projectBoundary){if(projectBoundary)prepared_.clear();if(projectBoundary||document.iterationHistory.t==V::Null){undo_.push_back(documentToJson(document));if(undo_.size()>32)undo_.erase(undo_.begin());}redo_.clear();}
int patternCopies(const Document& document,const AssemblyNode& node){
    if(!node.pattern.active)return 1;
    try{return (int)assemblyPatternCopySlots(document,node).size();}catch(const std::exception&){return 0;}
}
bool profileReady(const Document& document,const NamedSketch& sketch){
    try{sketchProfileRegions(sketch.sk,resolvePlane(document,sketch.sk.plane));return true;}
    catch(const std::exception&){return false;}
}
V designView(const Document& document,u64 revision){
    V r=V::makeObj(),parameters=V::makeArr(),features=V::makeArr(),sketches=V::makeArr(),nodes=V::makeArr();
    r.set("revision",V::makeNum((double)revision));
    if(!document.derivedGeometryStatus.isNull())r.set("derived_geometry",document.derivedGeometryStatus);
    const ExprEnv env=document.buildEnv();
    std::unordered_map<u64,std::set<u64>> placedDefinitions;V identityErrors=V::makeArr();
    for(const auto& node:document.assembly)if(node.feature)try{for(int copy:assemblyPatternCopySlots(document,node))placedDefinitions[assemblyPatternCopyFeature(document,node,copy)].insert(node.id);}catch(const std::exception& error){V issue=V::makeObj();issue.set("node",V::makeNum((double)node.id));issue.set("error",V::makeStr(error.what()));identityErrors.arr.push_back(issue);}
    r.set("placement_identity_checked",V::makeBool(identityErrors.arr.empty()));if(!identityErrors.arr.empty())r.set("placement_identity_errors",identityErrors);
    for(const auto& parameter:document.params){V item=V::makeObj();item.set("name",V::makeStr(parameter.name));item.set("value",V::makeNum(parameter.value));parameters.arr.push_back(item);}
    for(const auto& feature:document.features){
        const FeatureResult* status=nullptr;const Body* body=nullptr;
        for(const auto& item:document.lastResults)if(item.id==feature.id)status=&item;
        for(const auto& item:document.evalBodies)if(item.id==feature.id)body=&item;
        V row=V::makeObj();row.set("id",V::makeNum((double)feature.id));row.set("name",V::makeStr(feature.name));row.set("type",V::makeStr(featureTypeName(feature.type)));
        row.set("operation",V::makeStr(featureOperation(feature)));
        if(feature.part.t==V::Obj)row.set("part",feature.part);
        V inputs=V::makeArr(),dependents=V::makeArr(),instances=V::makeArr(),usedSketches=V::makeArr();
        for(u64 input:feature.inputs)inputs.arr.push_back(V::makeNum((double)input));
        for(const auto& child:document.features)if(std::find(child.inputs.begin(),child.inputs.end(),feature.id)!=child.inputs.end())dependents.arr.push_back(V::makeNum((double)child.id));
        for(u64 node:placedDefinitions[feature.id])instances.arr.push_back(V::makeNum((double)node));
        auto addSketch=[&](const std::string& name){if(name.empty())return;for(const auto& sketch:document.sketches)if(sketch.name==name){for(const auto& old:usedSketches.arr)if(old.str==name)return;usedSketches.arr.push_back(V::makeStr(name));}};
        addSketch(feature.get("sketch"));
        if(const auto* source=feature.nativeSpec.find("source_sketch");source&&source->t==V::Str)addSketch(source->str);
        for(const char* key:{"section","path","sections","guides"})if(const auto* names=feature.nativeSpec.find(key)){if(names->t==V::Str)addSketch(names->str);else if(names->t==V::Arr)for(const auto& name:names->arr)if(name.t==V::Str)addSketch(name.str);}
        if(const auto* bend=feature.nativeSpec.find("sketch_bend"))if(const auto* name=bend->find("sketch"))if(name->t==V::Str)addSketch(name->str);
        row.set("inputs",inputs);row.set("dependents",dependents);row.set("instances",instances);row.set("sketches",usedSketches);
        row.set("role",V::makeStr(!instances.arr.empty()?"placed_definition":dependents.arr.empty()?"terminal_output":"intermediate"));
        const auto lineage=featureManufacturingLineage(document,feature);V manufacturing=V::makeObj();
        manufacturing.set("declared_route",V::makeStr(feature.manufacturingRoute.empty()?"undeclared":feature.manufacturingRoute));
        if(feature.manufacturingProcess.t==V::Obj)manufacturing.set("process",feature.manufacturingProcess);
        manufacturing.set("geometry_route",V::makeStr(lineage.geometryRoute));manufacturing.set("editable_sheet",V::makeBool(lineage.editableSheet));
        if(lineage.origin){manufacturing.set("origin",V::makeNum((double)lineage.origin));if(lineage.geometryRoute=="sheet_metal")manufacturing.set("sheet_origin",V::makeNum((double)lineage.origin));if(lineage.geometryRoute=="frame")manufacturing.set("frame_origin",V::makeNum((double)lineage.origin));const auto* author=document.find(lineage.origin);if(author&&!author->manufacturingRoute.empty())manufacturing.set("origin_declared_route",V::makeStr(author->manufacturingRoute));}
        manufacturing.set("manufacturing_qualified",V::makeBool(false));row.set("manufacturing",manufacturing);
        row.set("ok",V::makeBool(status&&status->ok));row.set("error",V::makeStr(status?status->error:""));
        if(feature.suppressed)row.set("suppressed",V::makeBool(true));
        if(!feature.material.empty())row.set("material",V::makeStr(feature.material));
        if(!feature.material.empty()||feature.appearance.t==V::Obj)row.set("appearance",appearanceToJson(resolvedAppearance(document,feature)));
        if(!feature.faceAppearances.empty()){V rows=V::makeArr();for(const auto& face:feature.faceAppearances)rows.arr.push_back(faceAppearanceToJson(face));row.set("face_appearances",rows);}
        const DensityValue density=featureDensity(document,feature,env);
        if(density.present&&density.ok)row.set("density_g_cm3",V::makeNum(density.gramsPerCm3));
        if(body){
            if(const V* report=body->nativeData.find("report")){
                if(const V* volume=report->find("volume_mm3"))row.set("volume_mm3",*volume);
                if(const V* bounds=report->find("bounds_mm"))row.set("bounds_mm",*bounds);
            }
            if(!row.find("bounds_mm")){AABB box=body->bbox();if(box.valid()){V bounds=V::makeArr();for(double n:{box.mn.x,box.mn.y,box.mn.z,box.mx.x,box.mx.y,box.mx.z})bounds.arr.push_back(V::makeNum(n));row.set("bounds_mm",bounds);}}
        }
        if(!row.find("volume_mm3"))row.set("volume_mm3",V{});
        features.arr.push_back(row);
    }
    for(const auto& sketch:document.sketches){
        V row=V::makeObj(),entities=V::makeArr();row.set("name",V::makeStr(sketch.name));row.set("plane",V::makeStr(sketch.sk.plane));row.set("profile_ready",V::makeBool(profileReady(document,sketch)));
        for(size_t index=0;index<sketch.sk.ents.size();++index){const auto& entity=sketch.sk.ents[index];V item=V::makeObj();item.set("name",V::makeStr(entity.name));item.set("kind",V::makeStr(sketchEntityKindName(entity.kind)));item.set("index",V::makeNum((double)index));item.set("construction",V::makeBool(entity.construction));if(!entity.name.empty()&&entity.kind==SketchEnt::Pnt)item.set("reference",V::makeStr("point."+entity.name));if(entity.sourceFeature){item.set("source_feature",V::makeNum((double)entity.sourceFeature));item.set("source_reference",V::makeStr(entity.reference));}entities.arr.push_back(item);}
        V consumers=V::makeArr();for(const auto& feature:features.arr)for(const auto& used:feature.find("sketches")->arr)if(used.str==sketch.name)consumers.arr.push_back(*feature.find("id"));row.set("consumers",consumers);
        row.set("entities",entities);sketches.arr.push_back(row);
    }
    for(const auto& node:document.assembly){V row=V::makeObj();row.set("id",V::makeNum((double)node.id));row.set("name",V::makeStr(node.name));row.set("feature",V::makeNum((double)node.feature));row.set("copies",V::makeNum(patternCopies(document,node)));nodes.arr.push_back(row);}
    r.set("parameters",parameters);r.set("features",features);r.set("sketches",sketches);r.set("assembly",nodes);
    if(!document.materials.empty()){V materials=V::makeArr();for(const auto& material:document.materials){V item=V::makeObj();item.set("name",V::makeStr(material.name));item.set("density_g_cm3",V::makeNum(material.densityGcm3));item.set("appearance",appearanceToJson(material.appearance));materials.arr.push_back(item);}r.set("materials",materials);}
    if(!document.imageAssets.empty()){V assets=V::makeArr();for(const auto& asset:document.imageAssets)assets.arr.push_back(imageAssetToJson(asset));r.set("image_assets",assets);}
    if(!document.stamps.empty()){V stamps=V::makeArr();for(const auto& stamp:document.stamps)stamps.arr.push_back(stampToJson(stamp));r.set("stamps",stamps);}
    if(!document.canvases.empty()){V canvases=V::makeArr();for(const auto& canvas:document.canvases){V item=V::makeObj();item.set("name",V::makeStr(canvas.name));item.set("plane",V::makeStr(canvas.plane));canvases.arr.push_back(item);}r.set("canvases",canvases);}
    return r;
}
// 042D: compact per-revision digest built from the same authored state as
// detail design. Each row's fingerprint covers its full design row, so a later
// request with known_fingerprints returns rows only for what changed and lists
// what was removed. Deterministic change evidence, not validation.
V designDigest(const Document& document,u64 revision,const V& args){
    const auto* known=args.find("known_fingerprints");
    if(known&&(known->t!=V::Obj||known->obj.size()>4096))throw std::runtime_error("doc_info known_fingerprints must be an object of at most 4096 keys");
    if(known)for(const auto& entry:known->obj)if(entry.second.t!=V::Str||entry.first.size()>160||entry.second.str.size()>64)throw std::runtime_error("doc_info known_fingerprints entries must be bounded strings");
    const V design=designView(document,revision);
    V r=V::makeObj(),fingerprints=V::makeObj();unsigned changed=0,unchangedRows=0;
    // True when the caller already holds this fingerprint; records it either way.
    auto known_=[&](const std::string& key,const V& row){const auto fp="dg1-"+iterationSha256(row.dump()).substr(0,16);fingerprints.set(key,V::makeStr(fp));const auto* old=known?known->find(key.c_str()):nullptr;const bool same=old&&old->str==fp;if(known){if(same)++unchangedRows;else ++changed;}return same;};
    auto copy=[](const V& from,V& to,std::initializer_list<const char*> names){for(const char* name:names)if(const auto* value=from.find(name))to.set(name,*value);};
    r.set("schema",V::makeStr("ark.design-digest.v1"));r.set("revision",V::makeNum((double)revision));r.set("units",V::makeStr("mm"));
    r.set("source_sha256",V::makeStr(iterationSha256(documentToJson(document,false,false))));
    V features=V::makeArr(),sketches=V::makeArr(),nodes=V::makeArr();
    for(const auto& row:design.find("features")->arr){
        if(known_("feature:"+std::to_string((u64)row.find("id")->num),row))continue;
        V out=V::makeObj();copy(row,out,{"id","name","type","ok","inputs","material","suppressed","bounds_mm"});
        if(const auto* part=row.find("part"))if(const auto* name=part->find("name"))out.set("part",*name);
        if(const auto* error=row.find("error");error&&error->t==V::Str&&!error->str.empty())out.set("error",V::makeStr(error->str.substr(0,240)));
        features.arr.push_back(std::move(out));
    }
    for(const auto& row:design.find("sketches")->arr){
        if(known_("sketch:"+row.find("name")->str,row))continue;
        V out=V::makeObj();copy(row,out,{"name","plane","profile_ready","consumers"});out.set("entities",V::makeNum((double)row.find("entities")->arr.size()));sketches.arr.push_back(std::move(out));
    }
    for(const auto& row:design.find("assembly")->arr){if(!known_("node:"+std::to_string((u64)row.find("id")->num),row))nodes.arr.push_back(row);}
    r.set("features",features);r.set("sketches",sketches);r.set("assembly",nodes);
    if(!known_("parameters",*design.find("parameters")))r.set("parameters",*design.find("parameters"));
    if(const auto* materials=design.find("materials")){V names=V::makeArr();for(const auto& material:materials->arr)names.arr.push_back(*material.find("name"));if(!known_("materials",*materials))r.set("materials",names);}
    V counts=V::makeObj();counts.set("features",V::makeNum((double)design.find("features")->arr.size()));counts.set("sketches",V::makeNum((double)design.find("sketches")->arr.size()));counts.set("assembly_nodes",V::makeNum((double)design.find("assembly")->arr.size()));counts.set("parameters",V::makeNum((double)design.find("parameters")->arr.size()));counts.set("materials",V::makeNum((double)document.materials.size()));r.set("counts",counts);
    if(known){V removed=V::makeArr();for(const auto& entry:known->obj)if(!fingerprints.find(entry.first.c_str()))removed.arr.push_back(V::makeStr(entry.first));r.set("changed",V::makeNum(changed));r.set("unchanged",V::makeNum(unchangedRows));r.set("removed",removed);}
    r.set("fingerprints",fingerprints);
    r.set("evidence_scope",V::makeStr("Authored design at this revision. Fingerprints only detect changes and never replace expected_revision. Not validation; use object_context or detail design for full rows."));
    return r;
}
V selectedDesignView(const Document& document,u64 revision,const V& args){
    const auto* requestedIds=args.find("ids");const auto* requestedSections=args.find("sections");
    if(!requestedIds&&!requestedSections)return designView(document,revision);
    std::set<u64> selectedIds;
    if(requestedIds){
        if(requestedIds->t!=V::Arr||requestedIds->arr.empty()||requestedIds->arr.size()>64)throw std::runtime_error("doc_info ids must contain 1..64 feature ids or unique names");
        for(const auto& selection:requestedIds->arr){
            const u64 feature=selection.t==V::Str?namedFeature(document,selection.str):id(selection);
            if(!document.find(feature))throw std::runtime_error("doc_info selected feature was not found");
            if(!selectedIds.insert(feature).second)throw std::runtime_error("doc_info ids must select distinct features");
        }
    }
    const std::set<std::string> allowed{"features","sketches","parameters","assembly","materials"};
    std::set<std::string> sections;
    if(requestedSections){
        if(requestedSections->t!=V::Arr||requestedSections->arr.empty()||requestedSections->arr.size()>allowed.size())throw std::runtime_error("doc_info sections must contain 1..5 section names");
        for(const auto& section:requestedSections->arr)if(section.t!=V::Str||!allowed.count(section.str)||!sections.insert(section.str).second)throw std::runtime_error("doc_info section is unknown or repeated");
    }else sections=requestedIds?std::set<std::string>{"features"}:allowed;
    const V complete=designView(document,revision);
    std::set<std::string> associatedSketches,associatedMaterials;
    if(requestedIds)for(const auto& row:complete.find("features")->arr)if(selectedIds.count((u64)row.find("id")->num)){
        for(const auto& name:row.find("sketches")->arr)associatedSketches.insert(name.str);
        if(auto name=row.find("material"))associatedMaterials.insert(name->str);
    }
    V result=V::makeObj(),totals=V::makeObj(),selection=V::makeArr(),included=V::makeArr(),scopes=V::makeObj();
    result.set("revision",V::makeNum((double)revision));result.set("units",V::makeStr("mm"));result.set("parameter_error",V::makeStr(document.parameterError));
    if(!document.derivedGeometryStatus.isNull())result.set("derived_geometry",document.derivedGeometryStatus);
    result.set("partial",V::makeBool(true));result.set("scope",V::makeStr(requestedIds?"selected_features":"selected_sections"));
    for(u64 feature:selectedIds)selection.arr.push_back(V::makeNum((double)feature));if(requestedIds)result.set("selected_ids",selection);
    for(const auto& section:allowed){const auto* all=complete.find(section.c_str());totals.set(section,V::makeNum(all?all->arr.size():0));}
    result.set("total_counts",totals);
    for(const auto& section:sections){
        V rows=V::makeArr();const auto* all=complete.find(section.c_str());
        if(all)for(const auto& row:all->arr){
            bool keep=true;
            if(requestedIds){
                if(section=="features")keep=selectedIds.count((u64)row.find("id")->num)!=0;
                else if(section=="sketches")keep=associatedSketches.count(row.find("name")->str)!=0;
                else if(section=="assembly")keep=selectedIds.count((u64)row.find("feature")->num)!=0;
                else if(section=="materials")keep=associatedMaterials.count(row.find("name")->str)!=0;
            }
            if(keep)rows.arr.push_back(row);
        }
        result.set(section,rows);included.arr.push_back(V::makeStr(section));
        scopes.set(section,V::makeStr(requestedIds&&section!="parameters"?(section=="features"?"selected_features":"direct_associations"):"document"));
    }
    result.set("sections",included);result.set("section_scopes",scopes);return result;
}
#include "object_context.inl"
static std::atomic_size_t contextGraphAuthoringSerializationsCount{0};
size_t contextGraphAuthoringSerializations(){return contextGraphAuthoringSerializationsCount.load(std::memory_order_relaxed);}
#include "context_graph_034d.inl"
int wholeIndex(const V& value,const char* what){
    if(value.t!=V::Num||!std::isfinite(value.num)||value.num<0||value.num>1024||value.num!=std::floor(value.num))throw std::runtime_error(std::string(what)+" must be a nonnegative integer");
    return (int)value.num;
}
int wireSegmentOrigin(const SketchEnt& entity,int segment){
    if(segment<0||segment>=(int)entity.segments.size())throw std::runtime_error("sketch.patch segment is outside the wire");
    int index=2;
    for(int i=0;i<segment;++i){auto kind=entity.segments[(size_t)i].kind;index+=kind==SketchEnt::WireSeg::Line?2:kind==SketchEnt::WireSeg::Arc?3:6;}
    return index;
}
void patchSketch(NamedSketch& named,const V& command,std::string& error){
    const std::string entityName=str(command,"entity");
    auto it=std::find_if(named.sk.ents.begin(),named.sk.ents.end(),[&](const SketchEnt& entity){return entity.name==entityName;});
    if(it==named.sk.ents.end())throw std::runtime_error("Sketch entity not found");
    if(it->kind==SketchEnt::Recipe)throw std::runtime_error("Recipe geometry is driven; edit source fields with sketch.entity.set");
    if(it->kind==SketchEnt::Project||it->kind==SketchEnt::Offset)throw std::runtime_error("Project and offset entities are driven");
    if(it->arcRecipe.t==V::Obj)throw std::runtime_error("Arc recipe geometry is driven; replace its source recipe with sketch.entity.set");
    int values=0;for(const char* key:{"x","y","r","a0","a1","expr"})if(command.find(key))++values;
    if(values!=1)throw std::runtime_error("sketch.patch changes one coordinate");
    SketchEnt& entity=*it;int index=-1;const V* value=nullptr;
    if(command.find("role")||command.find("segment")){
        if(entity.kind!=SketchEnt::Wire)throw std::runtime_error("sketch.patch segment is for a wire");
        const std::string role=str(command,"role");const int segment=wholeIndex(field(command,"segment"),"sketch.patch segment");
        const int origin=wireSegmentOrigin(entity,segment);const auto kind=entity.segments[(size_t)segment].kind;
        if(role=="radius"){
            if(kind!=SketchEnt::WireSeg::Arc)throw std::runtime_error("sketch.patch radius is for a wire arc");
            if(command.find("expr"))value=command.find("expr");else if(command.find("x"))value=command.find("x");
            else throw std::runtime_error("sketch.patch radius needs expr or x");
            index=origin+2;
        }else if(role=="c1"||role=="c2"){
            if(kind!=SketchEnt::WireSeg::Cubic)throw std::runtime_error("sketch.patch control is for a wire cubic");
            const bool y=command.find("y")!=nullptr;if(!command.find("x")&&!y)throw std::runtime_error("sketch.patch control needs x or y");
            index=origin+(role=="c1"?0:2)+(y?1:0);value=command.find(y?"y":"x");
        }else throw std::runtime_error("sketch.patch role must be c1, c2 or radius");
    }else if(command.find("r")||command.find("a0")||command.find("a1")){
        if(command.find("r")){if(entity.kind!=SketchEnt::Circle&&entity.kind!=SketchEnt::Arc)throw std::runtime_error("sketch.patch r is for a circle or arc");index=2;value=command.find("r");}
        else if(command.find("a0")){if(entity.kind!=SketchEnt::Arc)throw std::runtime_error("sketch.patch a0 is for an arc");index=3;value=command.find("a0");}
        else{if(entity.kind!=SketchEnt::Arc)throw std::runtime_error("sketch.patch a1 is for an arc");index=4;value=command.find("a1");}
    }else{
        if(!command.find("anchor"))throw std::runtime_error("sketch.patch anchor is required");
        const int anchor=wholeIndex(field(command,"anchor"),"sketch.patch anchor");
        if(anchor>=sketchVertexCount(entity))throw std::runtime_error("sketch.patch anchor is outside the entity");
        const bool y=command.find("y")!=nullptr;if(!command.find("x")&&!y)throw std::runtime_error("sketch.patch changes one coordinate");
        index=(entity.kind==SketchEnt::Wire?sketchVertexDof(entity,anchor):anchor*2)+(y?1:0);value=command.find(y?"y":"x");
    }
    if(!value||index<0||index>=(int)entity.dof.size())throw std::runtime_error("sketch.patch coordinate is outside the entity");
    if(entity.expressions.size()<entity.dof.size())entity.expressions.resize(entity.dof.size());
    entity.expressions[(size_t)index]=expression(*value);
    Sketch checked;if(!sketchFromJson(sketchToJson(named.sk),checked,&error))throw std::runtime_error(error);named.sk=std::move(checked);
}
Vec3 findAxis(const std::string& name){
    if(name=="X")return {1,0,0};if(name=="-X")return {-1,0,0};
    if(name=="Y")return {0,1,0};if(name=="-Y")return {0,-1,0};
    if(name=="Z")return {0,0,1};if(name=="-Z")return {0,0,-1};
    throw std::runtime_error("find normal must be X, Y, Z, -X, -Y or -Z");
}
V AgentSession::info() const {
    V r=V::makeObj();r.set("revision",V::makeNum((double)revision));r.set("units",V::makeStr("mm"));
    r.set("feature_count",V::makeNum(document.features.size()));r.set("recompute_ms",V::makeNum(document.recomputeMs));
    r.set("features_reused",V::makeNum(document.recomputeReused));r.set("features_evaluated",V::makeNum(document.recomputeEvaluated));
    if(!document.derivedGeometryStatus.isNull())r.set("derived_geometry",document.derivedGeometryStatus);
    r.set("feature_workers",V::makeNum(document.featureWorkers));r.set("native_threads_per_worker",V::makeNum(document.nativeThreadsPerWorker));r.set("parallel_features",V::makeNum(document.parallelFeatures));r.set("peak_parallel_features",V::makeNum(document.peakParallelFeatures));r.set("dependency_edges",V::makeNum(document.dependencyEdges));
    r.set("parameter_error",V::makeStr(document.parameterError));
    r.set("history_tip",document.historyTip?V::makeNum((double)document.historyTip):V{});
    V drawings=V::makeArr();for(const auto& drawing:document.drawings)drawings.arr.push_back(*drawing.find("name"));r.set("drawings",drawings);
    V planes=V::makeArr(),sketches=V::makeArr();for(auto& p:document.planes)planes.arr.push_back(V::makeStr(p.name));for(auto& sk:document.sketches)sketches.arr.push_back(V::makeStr(sk.name));r.set("planes",planes);r.set("sketches",sketches);
    // Query-local state: follow authored order once, then reuse it for output
    // and consumption. No retained cache can go stale after a direct source edit.
    const bool loadAll=document.assemblyLoadSelection.isNull();
    const auto loaded=loadAll?std::unordered_set<u64>{}:assemblyLoadedFeatures(document);
    struct FeatureState {bool history,active;};
    std::vector<FeatureState> states;states.reserve(document.features.size());
    std::unordered_set<u64> hidden;bool within=true;
    for(const auto& feature:document.features){
        const bool active=within&&!feature.suppressed&&(loadAll||loaded.count(feature.id));
        states.push_back({within,active});
        if(active&&featureConsumesInputBodies(feature))for(size_t index=0;index<feature.inputs.size();++index)if(featureConsumesInputBody(feature,index))hidden.insert(feature.inputs[index]);
        if(feature.id==document.historyTip)within=false;
    }
    V fs=V::makeArr();fs.arr.reserve(document.features.size());
    for(size_t index=0;index<document.features.size();++index){
        const auto& f=document.features[index];const auto state=states[index];
        V v=V::makeObj();v.obj.reserve(6);v.set("id",V::makeNum((double)f.id));v.set("name",V::makeStr(f.name));v.set("type",V::makeStr(featureTypeName(f.type)));v.set("history_active",V::makeBool(state.history));v.set("evaluation_active",V::makeBool(state.active));v.set("output",V::makeBool(state.active&&!hidden.count(f.id)));fs.arr.push_back(std::move(v));
    }
    r.set("features",std::move(fs));r.set("assembly_nodes",V::makeNum(document.assembly.size()));r.set("material_count",V::makeNum(document.materials.size()));return r;
}
V AgentSession::dispatch(const std::string& tool,const V& args){
    size_t commandIndex=0;std::string commandOp;bool applying=false;
    try{
        if(args.t!=V::Obj)throw std::runtime_error("Arguments must be an object");
        if(document.historyTip&&(tool=="import_mesh"||tool=="import_cad"||tool=="library_insert"||tool=="import_step"||tool=="import_sheet_step")){
            const auto* replace=args.find("replace");
            if(!(tool=="import_step"||tool=="import_sheet_step")||!replace||replace->t!=V::Bool||!replace->b)
                throw std::runtime_error("Resume full feature history before appending imported definitions or library components");
        }
        static const std::set<std::string> custodyTools{"export_step","export_iges","export_mesh","drawing_export","drawing_inspect","bom_export_parts","snapshot","geometry_query","native_inspect","collision_inspect","assembly_info","assembly_motion","motion_info","explode_info","visualize_render","mesh_inspect","mesh_raycast","physics_inspect","manufacturing_inspect","reference_inspect","frame_inspect","electronics_inspect","pcb_export"};
        if(custodyTools.count(tool))verifyAssemblyExternalReferences(document);
        // Older direct clients may omit this field. When supplied (mandatory
        // for agent teams), every document replacement/history/import path
        // must honor it before reading files or changing the document.
        if(tool=="doc_undo"||tool=="doc_redo"||tool=="doc_load"||tool=="import_step"||tool=="import_sheet_step")
            if(const auto* expected=args.find("expected_revision"))
                if(expected->t!=V::Num||expected->num!=(double)revision)
                    throw std::runtime_error("Revision conflict; the current revision is in this result. Inspect only what your edit depends on and retry");
        if(tool=="context_graph")return result(projectContextGraph(document,revision,args,contextGraphCache_,contextGraphSource_,contextGraphStamp_));
        V extension;if(callProjectMemoryTool(*this,tool,args,extension))return result(extension);if(callAuthoring023Tool(*this,tool,args,extension))return result(extension);if(callComponentLibraryTool(*this,tool,args,extension))return result(extension);if(callAgentFeature(*this,tool,args,extension))return result(extension);if(callParametricHudTool(*this,tool,args,extension))return result(extension);
        {V out;if(callAuthoring024Tool(*this,tool,args,out))return result(out);if(callMotionTool(*this,tool,args,out))return result(out);Image image;if(callExplodedViewTool(*this,tool,args,out,&image))return image.w?snapshotImageResult(image,out,std::string()):result(out);}
        if(tool=="collision_inspect"||tool=="skeleton_inspect"||tool=="skeleton_compile"){
            if(const auto* expected=args.find("expected_revision"))if(expected->t!=V::Num||expected->num!=(double)revision)throw std::runtime_error("Revision conflict; inspect the current document before querying");
            V report;if(tool=="collision_inspect")report=inspectDocumentCollision(document,args);else if(tool=="skeleton_inspect")report=inspectSkeleton(document);else report=compileSkeletonPreview(document,skeletonSource(args));report.set("revision",V::makeNum((double)revision));return result(report);
        }
        if(tool=="feature_history"){
            if(!args.obj.empty())throw std::runtime_error("feature_history takes no arguments");
            auto report=inspectFeatureHistory(document);report.set("revision",V::makeNum((double)revision));return result(report);
        }
        if(tool=="assembly_authoring_info"){
            if(!args.obj.empty())throw std::runtime_error("assembly_authoring_info takes no arguments");
            return result(inspectAssemblyAuthoring(document,revision));
        }
        if(tool=="object_context")return result(objectContext(document,revision,args));
        if(tool=="doc_info"){
            std::string detail="summary";
            const bool filtered=args.find("ids")||args.find("sections");
            auto invalidDetail=[&](const char* message){
                V failure=V::makeObj();failure.set("error",V::makeStr(message));failure.set("error_code",V::makeStr("INVALID_ARGUMENT"));failure.set("stage",V::makeStr("arguments"));failure.set("tool",V::makeStr(tool));failure.set("field",V::makeStr("detail"));failure.set("revision",V::makeNum((double)revision));failure.set("document_unchanged",V::makeBool(true));
                V recovery=V::makeObj();recovery.set("hint",V::makeStr("Use detail design with ids/sections; omit both filters for detail summary. No document operation was executed."));
                if(filtered){V retry=args;retry.set("detail",V::makeStr("design"));recovery.set("retry_arguments",std::move(retry));}
                failure.set("recovery",std::move(recovery));return result(failure,true);
            };
            if(auto requested=args.find("detail")){if(requested->t!=V::Str||(requested->str!="summary"&&requested->str!="design"&&requested->str!="revision"&&requested->str!="digest"))return invalidDetail("doc_info detail must be summary, design, revision or digest");detail=requested->str;}
            if(filtered){if(args.find("detail")&&detail!="design")return invalidDetail("doc_info ids/sections require detail design; retry with detail design and the same filters");detail="design";}
            if(args.find("known_fingerprints")&&detail!="digest")return invalidDetail("doc_info known_fingerprints requires detail digest");
            return result(detail=="design"?selectedDesignView(document,revision,args):detail=="revision"?revisionReceipt032D(document,revision):detail=="digest"?designDigest(document,revision,args):info());
        }
        if(tool=="history_info"){
            size_t limit=64;if(const auto* value=args.find("limit")){if(value->t!=V::Num||value->num<1||value->num>64||value->num!=std::floor(value->num))throw std::runtime_error("History limit must be an integer from 1 to 64");limit=(size_t)value->num;}
            std::string cursor;if(const auto* value=args.find("before_id")){if(value->t!=V::Str||value->str.empty())throw std::runtime_error("History before_id must be a nonempty iteration id");cursor=value->str;}
            V r=iterationInfo(document,limit,cursor);r.set("revision",V::makeNum((double)revision));return result(r);
        }
        if(tool=="iteration_restore"){
            const auto& expected=field(args,"expected_revision");if(expected.t!=V::Num||expected.num!=(double)revision)throw std::runtime_error("Revision conflict; the current revision is in this result. Inspect only what your edit depends on and retry");
            Document next;iterationRestore(document,str(args,"id"),next);validated(next);remember();document=std::move(next);++revision;return result(info());
        }
        if(tool=="doc_new"){
            const auto& expected=field(args,"expected_revision");
            if(expected.t!=V::Num||expected.num!=(double)revision)throw std::runtime_error("Revision conflict; read doc_info and retry");
            remember(true);document=Document{};memorySessionScope.clear();++revision;return result(info());
        }
        if(tool=="agent_onboard")return result(agentOnboard(*this,args));
        if(tool=="doc_get"){V r=info(),j;arkjson::parse(documentToJson(document,true,false),j);r.set("document",j);if(document.iterationHistory.t==V::Obj){V summary=V::makeObj();summary.set("head_id",*document.iterationHistory.find("head_id"));summary.set("node_count",V::makeNum(document.iterationHistory.find("nodes")->arr.size()));r.set("history_summary",summary);}return result(r);}
        if(tool=="plane_inspect"){
            auto name=str(args,"name");auto frame=resolvePlane(document,name);V r=inspectDatum(document,name),matrix=V::makeArr(),axes=V::makeObj();for(auto x:frame.m)matrix.arr.push_back(V::makeNum(x));r.set("name",V::makeStr(name));r.set("world_column_major",matrix);const bool builtin=name=="Origin"||name=="XY"||name=="XZ"||name=="YZ";if(!builtin){r.set("kind",V::makeStr("plane"));for(auto& p:document.planes)if(p.name==name)r.set("kind",V::makeStr(p.kind));}for(int i=0;i<3;++i){V direction=V::makeArr();for(int j=0;j<3;++j)direction.arr.push_back(V::makeNum(frame.m[4*i+j]));axes.set(name+"."+std::string(1,"XYZ"[i]),direction);}r.set("named_axes",axes);return result(r);
        }
        if(tool=="sketch_inspect"){
            auto* sk=document.findSketch(str(args,"name"));if(!sk)throw std::runtime_error("Sketch not found");
            if(args.find("entity"))return result(measureSketchEntity(document,*sk,str(args,"entity")));
            bool samples=false;if(auto p=args.find("samples")){if(p->t!=V::Bool)throw std::runtime_error("samples must be boolean");samples=p->b;}return result(inspectSketch(document,*sk,samples));
        }
        if(tool=="eval_expr"){
            bool ok=false;auto v=evalExpr(str(args,"expr"),document.buildEnv(),&ok);
            if(!ok)throw std::runtime_error("Invalid mathematical expression");
            V r=V::makeObj();r.set("value",V::makeNum(v));return result(r);
        }
        if(tool=="sketch_repair"){
            for(const auto& item:args.obj)if(item.first!="name"&&item.first!="sketch"&&item.first!="expected_revision")throw std::runtime_error("Unknown sketch_repair field: "+item.first);
            const auto& expected=field(args,"expected_revision");if(expected.t!=V::Num||expected.num!=(double)revision)throw std::runtime_error("Revision conflict; inspect the current sketch before proposing repairs");
            const auto name=str(args,"name");if(name.empty())throw std::runtime_error("sketch_repair requires a target sketch name");
            if(const auto* manifest=document.skeleton.find("manifest"))if(const auto* owned=manifest->find("sketches"))for(const auto& row:owned->arr)if(const auto* n=row.find("name");n&&n->asStr()==name)throw std::runtime_error("Skeleton-owned sketch: edit the producer source");
            V source;NamedSketch scratch;std::string error;
            if(const auto* candidate=args.find("sketch")){source=*candidate;if(!sketchFromJson(source.dump(),scratch.sk,&error))throw std::runtime_error(error);}
            else {auto* current=document.findSketch(name);if(!current)throw std::runtime_error("Sketch not found");scratch.sk=current->sk;if(!arkjson::parse(sketchToJson(current->sk),source))throw std::runtime_error("Cannot read sketch source");}
            // Keep exact authored expressions/projections in command recipes;
            // the solver may change only its private evaluated copy.
            V report;
            if(std::any_of(scratch.sk.ents.begin(),scratch.sk.ents.end(),[](const SketchEnt& e){return e.kind==SketchEnt::Project||e.kind==SketchEnt::Offset;})){
                report=V::makeObj();report.set("status",V::makeStr("unsupported_driven_geometry"));report.set("reason",V::makeStr("Review the producer/projection first; single-deletion assistance requires self-contained sketch geometry"));report.set("proposals",V::makeArr());
            }else{evaluateSketch(scratch,document.buildEnv());if(!scratch.error.empty()&&scratch.error.rfind("Conflicting constraints or solver failed to converge",0)!=0)throw std::runtime_error(scratch.error);report=proposeSketchConstraintRepairs(scratch.sk);}
            report.set("name",V::makeStr(name));report.set("revision",V::makeNum((double)revision));report.set("expected_revision",expected);report.set("source_sha256",V::makeStr(iterationSha256(source.dump())));report.set("document_mutated",V::makeBool(false));
            if(auto* current=document.findSketch(name))report.set("document_sketch_sha256",V::makeStr(iterationSha256(sketchToJson(current->sk))));
            V alternatives=report.find("proposals")?*report.find("proposals"):V::makeArr();for(auto& alternative:alternatives.arr){
                auto recipe=source;const auto* original=recipe.find("constraints");const auto index=(size_t)alternative.find("remove_index")->num;if(!original||index>=original->arr.size())throw std::runtime_error("Repair proposal source mismatch");auto constraints=*original;constraints.arr.erase(constraints.arr.begin()+index);recipe.set("constraints",std::move(constraints));
                V command=V::makeObj(),commands=V::makeArr();command.set("op",V::makeStr("sketch.set"));command.set("name",V::makeStr(name));command.set("sketch",std::move(recipe));commands.arr.push_back(std::move(command));alternative.set("commands",std::move(commands));alternative.set("expected_revision",expected);
            }
            report.set("proposals",std::move(alternatives));
            return result(report);
        }
        if(tool=="sketch_diagnose"){
            NamedSketch scratch;std::string error;
            if(auto source=args.find("sketch")){if(!sketchFromJson(source->dump(),scratch.sk,&error))throw std::runtime_error(error);}
            else{auto sk=document.findSketch(str(args,"name"));if(!sk)throw std::runtime_error("Sketch not found");scratch.sk=sk->sk;}
            evaluateSketch(scratch,document.buildEnv());auto r=diagnoseSketch(scratch.sk);r.set("error",V::makeStr(scratch.error));r.set("revision",V::makeNum((double)revision));return result(r);
        }
        if(tool=="reference_inspect"){
            const bool hasId=args.find("id")!=nullptr,hasSketch=args.find("sketch")!=nullptr,hasReference=args.find("reference")!=nullptr,hasFind=args.find("find")!=nullptr;
            if(!hasId&&!hasSketch)throw std::runtime_error("reference_inspect needs an id or a sketch");
            if(hasReference&&hasFind)throw std::runtime_error("Pass reference or find");
            if(hasFind&&!hasId)throw std::runtime_error("reference_inspect find needs an id");
            bool compact=false;if(const auto* detail=args.find("detail")){if(detail->t!=V::Str||(detail->str!="full"&&detail->str!="frame"))throw std::runtime_error("Reference detail must be full or frame");compact=detail->str=="frame";if(compact&&!hasReference&&!hasFind)throw std::runtime_error("Reference detail frame needs reference or find");}
            V r=V::makeObj();r.set("revision",V::makeNum((double)revision));
            if(hasSketch){
                auto* sketch=document.findSketch(str(args,"sketch"));if(!sketch)throw std::runtime_error("Sketch not found");
                const Mat4 frame=resolvePlane(document,sketch->sk.plane);V points=V::makeArr();const std::string requested=hasReference?str(args,"reference"):"";bool matched=false;
                for(const auto& entity:sketch->sk.ents)if(entity.kind==SketchEnt::Pnt&&entity.dof.size()>=2){
                    const Vec3 world=frame.transformPoint({entity.dof[0],entity.dof[1],0});V row=V::makeObj(),origin=V::makeArr();
                    for(double n:{world.x,world.y,world.z})origin.arr.push_back(V::makeNum(n));
                    const std::string name="point."+entity.name;row.set("name",V::makeStr(name));row.set("origin",origin);points.arr.push_back(row);
                    if(requested==name){Mat4 placed=frame;placed.m[12]=world.x;placed.m[13]=world.y;placed.m[14]=world.z;V matrix=V::makeArr();for(double n:placed.m)matrix.arr.push_back(V::makeNum(n));r.set("world_column_major",matrix);r.set("kind",V::makeStr("point"));r.set("reference",V::makeStr(name));matched=true;}
                }
                r.set("points",points);r.set("sketch",V::makeStr(sketch->name));
                if(hasReference&&!matched)throw std::runtime_error("Sketch entity not found");
            }
            if(hasId){
                const auto feature=id(field(args,"id"));ensureNativeBody(document,feature,document.evalBodies);
                for(auto& b:document.evalBodies)if(b.id==feature)if(auto topology=b.nativeData.find("topology"))r.set("topology",*topology);
                if(hasFind){
                    const V& spec=field(args,"find");if(spec.t!=V::Obj)throw std::runtime_error("find must be an object");
                    if(str(spec,"kind")!="planar")throw std::runtime_error("find kind must be planar");
                    const Vec3 axis=findAxis(str(spec,"normal"));std::string matched;int count=0;
                    if(const V* topology=r.find("topology"))if(const V* rows=topology->find("references"))if(rows->t==V::Arr)for(const auto& row:rows->arr){
                        const V* kind=row.find("kind");const V* status=row.find("status");const V* name=row.find("name");const V* descriptor=row.find("descriptor");
                        if(!kind||kind->str!="face"||!status||(status->str!="bound"&&status->str!="merged")||!name||name->t!=V::Str)continue;
                        const V* normal=descriptor?descriptor->find("normal"):nullptr;if(!normal||normal->t!=V::Arr||normal->arr.size()!=3)continue;
                        bool numeric=true;for(const auto& coordinate:normal->arr)numeric&=coordinate.t==V::Num&&std::isfinite(coordinate.num);if(!numeric)continue;
                        if(Vec3{normal->arr[0].num,normal->arr[1].num,normal->arr[2].num}.dot(axis)>0.99){matched=name->str;++count;}
                    }
                    if(count==0)throw std::runtime_error("No reference matched");
                    if(count!=1)throw std::runtime_error("Ambiguous reference find");
                    r.set("reference",V::makeStr(matched));
                    auto ref=resolveGeometryReference(document,feature,matched,.5);V matrix=V::makeArr();for(double n:ref.frame.m)matrix.arr.push_back(V::makeNum(n));r.set("world_column_major",matrix);r.set("kind",V::makeStr(ref.kind));
                }else if(hasReference&&!hasSketch){
                    double fraction=.5;if(auto p=args.find("fraction")){if(p->t!=V::Num)throw std::runtime_error("fraction must be numeric");fraction=p->num;}
                    auto ref=resolveGeometryReference(document,feature,str(args,"reference"),fraction);V matrix=V::makeArr();for(double n:ref.frame.m)matrix.arr.push_back(V::makeNum(n));r.set("world_column_major",matrix);r.set("kind",V::makeStr(ref.kind));
                }
                r.set("id",V::makeNum((double)feature));
            }
            if(compact)r.obj.erase(std::remove_if(r.obj.begin(),r.obj.end(),[](const auto& kv){return kv.first=="topology"||kv.first=="points";}),r.obj.end());
            return result(r);
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
            const auto detail=args.find("detail")?str(args,"detail"):"full";if(detail!="full"&&detail!="report"&&detail!="properties")throw std::runtime_error("native_inspect detail must be full, report or properties");
            const double gravityMs2=gravity(args);
            // A preview check can only name a feature it adds: accept a unique name.
            u64 selected=field(args,"id").t==V::Str?namedFeature(document,field(args,"id").str):id(field(args,"id"));
            auto* feature=document.find(selected);
            if(!feature)throw std::runtime_error("Select a modeling feature");
            if(feature->type==FeatureType::Suspension)throw std::runtime_error("Suspension is a kinematic display model; use suspension_inspect");
            if(isMeshModelFeature(feature->type))throw std::runtime_error("Mesh features have no exact B-rep; use mesh_inspect");
            const DensityValue density=featureDensity(document,*feature,document.buildEnv());
            if(density.present&&!density.ok)throw std::runtime_error(density.reason);
            try{ensureNativeBody(document,selected,document.evalBodies);}
            catch(const std::exception&){if(density.present)throw std::runtime_error("Density needs an exact solid");throw;}
            for(const Body& body:document.evalBodies)if(body.id==selected){
                V r=V::makeObj();r.set("id",V::makeNum((double)selected));
                if(auto report=body.nativeData.find("report")){
                    if(detail=="properties"){
                        V properties=V::makeObj();for(const char* key:{"kernel","kernel_version","brep_valid","solids","shells","faces","edges","vertices","volume_mm3","area_mm2","bounds_mm","closed","connected","display_mesh_authoritative","self_intersections_checked","manufacturing_authoritative"})if(const auto* value=report->find(key))properties.set(key,*value);
                        r.set("report",std::move(properties));r.set("report_scope",V::makeStr("current_native_validity_and_properties; operation diagnostics omitted"));
                    }else r.set("report",*report);
                }
                if(detail!="properties")if(auto holes=body.nativeData.find("holes"))r.set("holes",*holes);
                r.set("revision",V::makeNum((double)revision));r.set("geometry_authority",V::makeStr("native_brep"));
                if(detail=="full")for(auto key:{"panels","bends","topology"})if(auto data=body.nativeData.find(key))r.set(key,*data);
                r.set("detail",V::makeStr(detail));r.set("topology_included",V::makeBool(detail=="full"));
                r.set("read_only_geometry",V::makeBool(feature->type==FeatureType::SourceSheet));
                r.set("has_flat",V::makeBool(body.nativeData.find("flat_brep_base64")!=nullptr));
                if(const V* point=args.find("point")){
                    if(point->t!=V::Arr||point->arr.size()!=3)throw std::runtime_error("point must be xyz");
                    for(const auto& coordinate:point->arr)if(coordinate.t!=V::Num||!std::isfinite(coordinate.num))throw std::runtime_error("point must be xyz");
                    const V* brep=body.nativeData.find("brep_base64");if(!brep||brep->t!=V::Str)throw std::runtime_error("Feature has no exact B-rep");
                    V model=V::makeObj();model.set("brep",*brep);model.set("point",*point);if(const V* topology=body.nativeData.find("topology"))model.set("topology",*topology);
                    V request=V::makeObj();request.set("operation",V::makeStr("point_distance"));request.set("request",std::move(model));
                    V response=nativeWorkerRequest(request);
                    if(const V* distance=response.find("distance_mm"))r.set("distance_mm",*distance);
                    if(const V* face=response.find("face"))r.set("face",*face);
                }
                if(feature->type==FeatureType::PCB){
                    r.set("mass_checked",V::makeBool(false));r.set("physical_mass_supported",V::makeBool(false));
                    r.set("mass_reason",V::makeStr("PCB contains multiple materials; assign masses per substrate, copper and component in a physical BOM"));
                }else if(feature->type==FeatureType::ConstructionFrame){
                    const V* frame=body.nativeData.find("frame_report");
                    r.set("mass_checked",V::makeBool(frame&&frame->find("mass_checked")&&frame->find("mass_checked")->b));
                    if(frame){if(detail!="properties")r.set("frame_report",*frame);if(auto mass=frame->find("mass_g")){setMass(r,mass->num,gravityMs2);if(auto center=frame->find("center_mm"))r.set("center_mm",*center);}else r.set("mass_reason",V::makeStr("Assign density to each frame profile"));}
                }else if(density.present){
                    V response;try{response=centroidOf(document,selected);}catch(const std::exception& error){throw std::runtime_error(exactSolidFailure(error));}
                    const V* volume=response.find("volume_mm3");const V* center=response.find("center_mm");
                    if(!volume||volume->t!=V::Num||!center||center->t!=V::Arr||center->arr.size()!=3)throw std::runtime_error("Density needs an exact solid");
                    for(const auto& coordinate:center->arr)if(coordinate.t!=V::Num||!std::isfinite(coordinate.num))throw std::runtime_error("Density needs a finite exact solid centroid");
                    if(!std::isfinite(volume->num)||volume->num<=0)throw std::runtime_error("Density needs a positive exact solid volume");
                    setMass(r,density.gramsPerCm3*volume->num/1000.0,gravityMs2);r.set("mass_checked",V::makeBool(true));r.set("density_g_cm3",V::makeNum(density.gramsPerCm3));
                    r.set("center_mm",*center);
                }else{r.set("mass_checked",V::makeBool(false));r.set("mass_reason",V::makeStr("No density assigned"));}
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
            Document next;std::string error;if(!replace&&!documentFromJson(documentToJson(document,true,false),next,&error))throw std::runtime_error(error);
            Feature f;f.type=FeatureType::SourceTube;f.name="Source tube";if(auto p=package.find("source_name"))f.name=p->asStr();f.sourceData=std::move(package);auto id=next.addFeature(std::move(f));validated(next);
            if(documentToJson(next).size()>4*1024*1024)throw std::runtime_error("Imported document exceeds current 4 MiB persistence limit; use replace:true for a standalone source document");
            if(replace)prepared_.clear();remember();document=std::move(next);++revision;V r=info();r.set("created_id",V::makeNum((double)id));r.set("report",*document.find(id)->sourceData.find("report"));return result(r);
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
            Document next;std::string error;if(!replace&&!documentFromJson(documentToJson(document,true,false),next,&error))throw std::runtime_error(error);
            Feature feature;feature.type=FeatureType::SourceSheet;feature.name="Source sheet";if(auto name=package.find("source_name"))feature.name=name->asStr();feature.sourceData=std::move(package);u64 created=next.addFeature(std::move(feature));validated(next);
            if(documentToJson(next).size()>4*1024*1024)throw std::runtime_error("Imported sheet exceeds current 4 MiB document limit");
            if(replace)prepared_.clear();remember();document=std::move(next);++revision;V answer=info();answer.set("created_id",V::makeNum((double)created));if(auto report=document.find(created)->sourceData.find("report"))answer.set("report",*report);return result(answer);
        }
        if(tool=="import_mesh"){
            if(auto expected=args.find("expected_revision")){if(expected->t!=V::Num||expected->num!=(double)revision)throw std::runtime_error("Revision conflict; the current revision is in this result. Inspect only what your edit depends on and retry");}
            double weld=0;if(auto value=args.find("weld_mm")){if(value->t!=V::Num||!(value->num>0&&value->num<=1))throw std::runtime_error("weld_mm must be in (0, 1]");weld=value->num;}
            Mesh imported=importMeshPath(str(args,"path"));const double rawVertices=(double)imported.vertCount(),rawTriangles=(double)imported.triCount();
            if(weld){imported.weld(weld);imported.computeNormals();if(!imported.triCount())throw std::runtime_error("weld removed every imported triangle");}
            Document next;std::string error;if(!documentFromJson(documentToJson(document,true,false),next,&error))throw std::runtime_error(error);
            Feature feature;feature.type=FeatureType::MeshBody;feature.name=args.find("name")?str(args,"name"):"Imported mesh";feature.sourceData=meshToPayload(imported);
            const u64 created=next.addFeature(std::move(feature));validated(next);
            if(documentToJson(next).size()>4*1024*1024)throw std::runtime_error("Imported mesh exceeds the 4 MiB document limit");
            remember();document=std::move(next);++revision;V answer=info();answer.set("created_id",V::makeNum((double)created));
            answer.set("input_vertices",V::makeNum(rawVertices));answer.set("input_triangles",V::makeNum(rawTriangles));
            answer.set("vertices",V::makeNum((double)imported.vertCount()));answer.set("triangles",V::makeNum((double)imported.triCount()));
            answer.set("welded",V::makeBool(weld>0));answer.set("mesh_authoritative",V::makeBool(true));answer.set("exact_geometry",V::makeBool(false));
            answer.set("file_linked",V::makeBool(false));return result(answer);
        }
        if(tool=="suspension_inspect"){
            const u64 selected=featureRef(document,field(args,"id"));const auto* feature=document.find(selected);
            if(!feature||feature->type!=FeatureType::Suspension)throw std::runtime_error("Select a suspension feature");
            auto value=[&](const char* key){const V* v=args.find(key);if(!v)return 0.;if(v->t!=V::Num||!std::isfinite(v->num))throw std::runtime_error("Motion controls must be finite numbers");return v->num;};
            auto preview=buildSuspensionPreview(*feature,document.buildEnv(),{value("jounce_mm"),value("rack_mm"),value("wheel_spin_deg")});
            V r=preview.report;r.set("id",V::makeNum((double)selected));r.set("revision",V::makeNum((double)revision));return result(r);
        }
        if(tool=="usage_info"){
            std::string detail="summary";if(const V* v=args.find("detail")){if(v->t!=V::Str||(v->str!="summary"&&v->str!="receipts"))throw std::runtime_error("Usage detail must be summary or receipts");detail=v->str;}
            V r=usageInfo(document,detail=="receipts");r.set("revision",V::makeNum((double)revision));return result(r);
        }
        if(tool=="usage_record"){
            UsageReceipt receipt;receipt.id=str(args,"id");receipt.source="external_client";receipt.provenance="client_supplied_provider_usage";
            if(auto model=args.find("model")){if(model->t!=V::Str)throw std::runtime_error("model must be text");receipt.model=model->str;}
            auto take=[&](const char* key,u64 fallback,bool required=false){const V* n=args.find(key);if(!n){if(required)throw std::runtime_error(std::string("Missing ")+key);return fallback;}if(n->t!=V::Num||!std::isfinite(n->num)||n->num<0||n->num>9007199254740991.||n->num!=std::floor(n->num))throw std::runtime_error("Usage counts must be exact nonnegative integers");return (u64)n->num;};
            receipt.inputTokens=take("input_tokens",0,true);receipt.outputTokens=take("output_tokens",0,true);receipt.totalTokens=take("total_tokens",0,true);
            receipt.cachedInputTokens=take("cached_input_tokens",0);receipt.reasoningTokens=take("reasoning_tokens",0);receipt.reportedResponses=take("reported_responses",1);receipt.missingResponses=take("missing_responses",0);
            std::string error;const auto recorded=addUsageReceipt(document,receipt,&error);if(recorded==UsageRecordResult::Rejected)throw std::runtime_error(error);
            V r=usageInfo(document);r.set("revision",V::makeNum((double)revision));r.set("changed",V::makeBool(recorded!=UsageRecordResult::Unchanged));return result(r);
        }
        if(tool=="sheet_inspect"||tool=="export_flat_dxf"){
            const auto selected=featureRef(document,field(args,"id"));const auto* feature=document.find(selected);
            const auto lineage=feature?featureManufacturingLineage(document,*feature):ManufacturingLineage{};
            if(lineage.geometryRoute!="sheet_metal"||!lineage.editableSheet)throw std::runtime_error("Select an authored sheet_metal feature or its rigid placement; inspect doc_info detail design for sheet_origin");
            for(const auto& body:document.evalBodies)if(body.id==lineage.origin){
                if(tool=="sheet_inspect"){V r=inspectSheetNativeData(body.nativeData);r.set("id",V::makeNum((double)selected));r.set("sheet_origin",V::makeNum((double)lineage.origin));r.set("local_authoring_coordinates",V::makeBool(true));r.set("revision",V::makeNum((double)revision));return result(r);}
                V request=V::makeObj();request.set("operation",V::makeStr("export_sheet_flat_dxf"));request.set("package",body.nativeData);request.set("path",V::makeStr(absolutePath(str(args,"path"))));return result(nativeWorkerRequest(request));
            }throw std::runtime_error("Sheet feature is not evaluated");
        }
        if(tool=="stock_preview"){
            const auto selected=featureRef(document,field(args,"id"));const auto* feature=document.find(selected);if(!feature)throw std::runtime_error("Feature not found");
            bool includeMesh=false;if(const auto* value=args.find("mesh")){if(value->t!=V::Bool)throw std::runtime_error("mesh must be boolean");includeMesh=value->b;}
            for(const auto& body:document.evalBodies)if(body.id==selected){auto stock=buildStockPreview(*feature,document.buildEnv(),body.nativeData);V r=stock.report;r.set("id",V::makeNum((double)selected));r.set("revision",V::makeNum((double)revision));
                if(includeMesh){V positions=V::makeArr(),indices=V::makeArr();for(float p:stock.mesh.pos)positions.arr.push_back(V::makeNum(p));for(u32 i:stock.mesh.idx)indices.arr.push_back(V::makeNum(i));r.set("positions",std::move(positions));r.set("indices",std::move(indices));}return result(r);
            }throw std::runtime_error("Stock preview feature is not evaluated");
        }
        if(tool=="frame_inspect"||tool=="electronics_inspect"){
            const auto selected=featureRef(document,field(args,"id"));const auto* feature=document.find(selected);
            bool frame=tool=="frame_inspect";
            if(!feature||(frame?feature->type!=FeatureType::ConstructionFrame:(feature->type!=FeatureType::PCB&&feature->type!=FeatureType::CableRoute)))throw std::runtime_error(frame?"Select a construction_frame feature":"Select a pcb or cable_route feature");
            for(const auto& body:document.evalBodies)if(body.id==selected){
                const auto* report=body.nativeData.find(frame?"frame_report":"electronics");
                if(!report)throw std::runtime_error("Module inspection report unavailable");
                V r=*report;r.set("id",V::makeNum((double)selected));r.set("revision",V::makeNum((double)revision));
                if(auto shape=body.nativeData.find("report"))r.set("geometry",*shape);return result(r);
            }
            throw std::runtime_error("Module body is not evaluated");
        }
        if(tool=="pcb_export"){
            const auto* feature=document.find(featureRef(document,field(args,"id")));
            if(!feature||feature->type!=FeatureType::PCB)throw std::runtime_error("Select a pcb feature");
            return result(exportPcbFabrication(*feature,document.buildEnv(),str(args,"path_prefix")));
        }
        if(tool=="export_iges"){
            const auto selected=featureRef(document,field(args,"id"));const auto* feature=document.find(selected);
            if(!feature)throw std::runtime_error("Feature not found");
            ensureNativeBody(document,selected,document.evalBodies);
            std::string pose="formed";if(const auto* value=args.find("pose")){if(value->t!=V::Str||(value->str!="flat"&&value->str!="formed"))throw std::runtime_error("pose must be flat or formed");pose=value->str;}
            const bool sheet=feature->type==FeatureType::SheetMetal||feature->type==FeatureType::SourceSheet;
            if(!sheet&&pose=="flat")throw std::runtime_error("Flat pose requires a sheet feature");
            for(const auto& body:document.evalBodies)if(body.id==selected){
                if(sheet&&pose=="flat")if(const auto* report=body.nativeData.find("report"))if(const auto* qualified=report->find("flat_pattern_qualified"))if(qualified->t==V::Bool&&!qualified->b)throw std::runtime_error("Stamped sheet flat export is unavailable: the nominal blank is not a qualified developed pattern; export pose formed");
                const auto* brep=body.nativeData.find(sheet&&pose=="flat"?"flat_brep_base64":"brep_base64");
                if(!brep||brep->t!=V::Str)throw std::runtime_error("Selected feature has no exact IGES B-rep");
                V request=V::makeObj();request.set("operation",V::makeStr("export_iges"));request.set("brep_base64",*brep);request.set("path",V::makeStr(absolutePath(str(args,"path"))));
                return result(nativeWorkerRequest(request));
            }throw std::runtime_error("Feature has no evaluated body");
        }
        if(tool=="export_step"){
            auto* feature=document.find(id(field(args,"id")));if(!feature)throw std::runtime_error("Feature not found");
            if(feature->type==FeatureType::SourceTube)return result(exportSourceTubeStep(feature->sourceData,str(args,"path")));
            if(feature->type==FeatureType::NotchedTube)return result(exportNotchStep(notchRequestForFeature(*feature,document.buildEnv()),str(args,"path")));
            if(feature->type==FeatureType::Tube){
                ensureNativeBody(document,feature->id,document.evalBodies);
                const Body* tube=nullptr;for(const Body& candidate:document.evalBodies)if(candidate.id==feature->id)tube=&candidate;
                if(!tube)throw std::runtime_error("Tube has no evaluated body");
                auto payload=tube->nativeData.find("brep_base64");if(!payload||payload->t!=V::Str)throw std::runtime_error("Tube has no exact STEP B-rep");
                return result(exportNativeBrep(payload->str,str(args,"path")));
            }
            if(!isNativeFeature(feature->type))throw std::runtime_error("STEP export requires a native or supported tube feature");
            validateNativeComponentCustody(document);
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
            if(sheet&&pose!="formed")if(const auto* report=body->nativeData.find("report"))if(const auto* qualified=report->find("flat_pattern_qualified"))if(qualified->t==V::Bool&&!qualified->b)throw std::runtime_error("Stamped sheet flat export is unavailable: the nominal blank is not a qualified developed pattern; export pose formed");
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
        if(tool=="assembly_motion"){
            const u64 driver=nodeRef(document,field(args,"driver"));
            bool ok=false;const double speed=evalExpr(expression(field(args,"speed_rpm")),document.buildEnv(),&ok);if(!ok||!std::isfinite(speed))throw std::runtime_error("Motion speed_rpm must be a finite expression");
            const V& time=field(args,"time_s");if(time.t!=V::Num)throw std::runtime_error("Motion time_s must be numeric");
            double offset=0;if(const auto* angle=args.find("angle_deg")){if(angle->t!=V::Num)throw std::runtime_error("Motion angle_deg must be numeric");offset=angle->num;}
            if(motionRevision_!=revision){std::string error;if(!buildAssemblyMotion(document,motionProgram_,error))throw std::runtime_error(error);motionRevision_=revision;}
            V report=inspectAssemblyMotion(motionProgram_,driver,speed,time.num,offset);report.set("revision",V::makeNum((double)revision));return result(report);
        }
        if(tool=="assembly_info"){
            if(const auto* detail=args.find("detail")){
                if(detail->t!=V::Str||(detail->str!="full"&&detail->str!="result"))throw std::runtime_error("assembly_info detail must be full or result");
                if(detail->str=="result"){
                    size_t selected=0;for(const char* key:{"measure","clearance","drive","sweep"})selected+=args.find(key)?1:0;
                    if(selected!=1)throw std::runtime_error("assembly_info detail result requires exactly one of measure, clearance, drive or sweep");
                    for(const auto& field:args.obj)if(field.first!="detail"&&field.first!="measure"&&field.first!="clearance"&&field.first!="drive"&&field.first!="sweep")throw std::runtime_error("assembly_info detail result does not accept: "+field.first);
                    V receipt=V::makeObj();receipt.set("revision",V::makeNum((double)revision));receipt.set("detail",V::makeStr("result"));receipt.set("document_unchanged",V::makeBool(true));
                    if(const auto* drive=args.find("drive"))receipt.set("drive",driveReport(driveAssembly(document,*drive),revision));
                    else if(const auto* sweep=args.find("sweep"))receipt.set("sweep",inspectAssemblySweep031B(document,*sweep));
                    else{
                        std::vector<AssemblyPlacement> placements;std::string error;AssemblyDiagnostics diagnostics;if(!resolveAssembly(document,placements,error,&diagnostics))throw std::runtime_error(error);
                        receipt.set("diagnostics",assemblyDiagnosticsJson(diagnostics));
                        if(const auto* measure=args.find("measure"))attachMeasure(document,receipt,placements,*measure);
                        else attachClearance(document,receipt,placements,*args.find("clearance"));
                    }
                    return result(receipt);
                }
            }
            const double gravityMs2=gravity(args);
            std::vector<AssemblyPlacement> placements;std::string error;AssemblyDiagnostics diagnostics;if(!resolveAssembly(document,placements,error,&diagnostics))throw std::runtime_error(error);
            V r=info(),nodes=V::makeArr(),bom=V::makeArr();std::map<u64,size_t> quantities;
            if(const auto* drive=args.find("drive"))r.set("drive",driveReport(driveAssembly(document,*drive),revision));
            if(const auto* sweep=args.find("sweep"))r.set("sweep",inspectAssemblySweep031B(document,*sweep));
            for(auto& p:placements){V j=V::makeObj(),matrix=V::makeArr();j.set("node",V::makeNum((double)p.node));j.set("feature",V::makeNum((double)p.feature));j.set("name",V::makeStr(p.name));j.set("visible",V::makeBool(p.visible));j.set("copy",V::makeNum(p.copy));
                for(auto x:p.world.m)matrix.arr.push_back(V::makeNum(x));
                j.set("world_column_major",matrix);nodes.arr.push_back(j);++quantities[p.feature];}
            for(auto& kv:quantities){V j=V::makeObj();j.set("feature",V::makeNum((double)kv.first));j.set("name",V::makeStr(document.find(kv.first)->name));j.set("quantity",V::makeNum(kv.second));bom.arr.push_back(j);}
            const auto richBom=inspectBom(document,revision);r.set("bom_schema",*richBom.find("schema"));r.set("bom_document_id",*richBom.find("document_id"));r.set("instances",nodes);r.set("bom",*richBom.find("items"));r.set("mates_solved",V::makeBool(!document.mates.empty()));r.set("diagnostics",assemblyDiagnosticsJson(diagnostics));r.set("mass_scope",V::makeStr("visible_placed_parts"));V mates=V::makeArr();for(auto& m:document.mates)mates.arr.push_back(mateToJson(m));r.set("mates",mates);
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
                block.set("axis_convention",V::makeStr(axisConvention()));
                r.set("candidate",block);
            }
            if(auto clearance=args.find("clearance"))attachClearance(document,r,placements,*clearance);
            if(auto measure=args.find("measure"))attachMeasure(document,r,placements,*measure);
            {
                const ExprEnv env=document.buildEnv();bool any=false;
                for(const auto& placement:placements){if(!placement.visible)continue;any=true;break;}
                if(any){
                    struct Local{bool ok=false;double volume=0;Vec3 center;std::string reason;};
                    std::map<u64,Local> cache;double mass=0;Vec3 moment;std::string reason;
                    for(const auto& placement:placements){
                        if(!placement.visible)continue;
                        const Feature* feature=document.find(placement.feature);
                        const std::string name=placement.name.empty()?(feature?feature->name:std::string("instance")):placement.name;
                        if(!feature){if(reason.empty())reason=name+" has no density";continue;}
                        if(feature->type==FeatureType::PCB){if(reason.empty())reason=name+": PCB mass requires a material-resolved BOM";continue;}
                        if(feature->type==FeatureType::ConstructionFrame){
                            const V* report=nullptr;for(const auto& body:document.evalBodies)if(body.id==feature->id)report=body.nativeData.find("frame_report");
                            const V* frameMass=report?report->find("mass_g"):nullptr;const V* frameCenter=report?report->find("center_mm"):nullptr;
                            if(!report||!report->find("mass_checked")||!report->find("mass_checked")->b||!frameMass||!frameCenter||frameCenter->t!=V::Arr||frameCenter->arr.size()!=3){if(reason.empty())reason=name+": frame profile densities are incomplete";continue;}
                            const double grams=frameMass->num;
                            Vec3 center{frameCenter->arr[0].num,frameCenter->arr[1].num,frameCenter->arr[2].num};
                            mass+=grams;moment+=placement.world.transformPoint(center)*grams;continue;
                        }
                        const DensityValue density=featureDensity(document,*feature,env);
                        if(!density.present){if(reason.empty())reason=name+" has no density";continue;}
                        if(!density.ok){if(reason.empty())reason=density.reason;continue;}
                        auto cached=cache.find(feature->id);
                        if(cached==cache.end()){
                            Local local;
                            try{V response=centroidOf(document,feature->id);const V* volume=response.find("volume_mm3");const V* center=response.find("center_mm");
                                if(!volume||volume->t!=V::Num||!center||center->t!=V::Arr||center->arr.size()!=3)local.reason="Density needs an exact solid";
                                else if(!std::isfinite(volume->num)||volume->num<=0||!std::isfinite(center->arr[0].num)||!std::isfinite(center->arr[1].num)||!std::isfinite(center->arr[2].num))local.reason="Density needs a positive exact solid volume and finite centroid";
                                else{local.ok=true;local.volume=volume->num;local.center={center->arr[0].num,center->arr[1].num,center->arr[2].num};}
                            }catch(const std::exception& error){local.reason=exactSolidFailure(error);}
                            cached=cache.emplace(feature->id,local).first;
                        }
                        if(!cached->second.ok){if(reason.empty())reason=name+": "+cached->second.reason;continue;}
                        const double grams=density.gramsPerCm3*cached->second.volume/1000.0;
                        mass+=grams;moment=moment+placement.world.transformPoint(cached->second.center)*grams;
                    }
                    if(!reason.empty()){r.set("mass_checked",V::makeBool(false));r.set("mass_reason",V::makeStr(reason));}
                    else{r.set("mass_checked",V::makeBool(true));setMass(r,mass,gravityMs2);V center=V::makeArr();const Vec3 cog=mass>0?moment*(1.0/mass):Vec3{};for(double value:{cog.x,cog.y,cog.z})center.arr.push_back(V::makeNum(value));r.set("center_mm",center);}
                }else{r.set("mass_checked",V::makeBool(false));r.set("mass_reason",V::makeStr("No visible placed parts"));}
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
        if(tool=="drawing_inspect")return result(inspectDrawing(document,args,revision));
        if(tool=="drawing_export")return result(exportDrawing(document,args,revision));
        if(tool=="doc_apply"){
            if(const auto* detail=args.find("detail"))if(detail->t!=V::Str||(detail->str!="full"&&detail->str!="design"&&detail->str!="changes"))throw std::runtime_error("doc_apply detail must be full, design or changes");
            if(args.find("candidate_id"))return result(commitPrepared(args));
            if(args.find("refresh_candidate"))throw std::runtime_error("refresh_candidate requires an owned retained candidate_id");
            if(args.find("accept_dfm"))throw std::runtime_error("DFM_ACCEPTANCE_INVALID: acceptance requires a retained manufacturing proposal candidate");
            if(auto expected=args.find("expected_revision")){
                if(expected->t!=V::Num||expected->num!=(double)revision)throw std::runtime_error("Revision conflict; the current revision is in this result. Inspect only what your edit depends on and retry");
            }
            const auto& commands=field(args,"commands");
            if(commands.t!=V::Arr||commands.arr.empty()||commands.arr.size()>128)throw std::runtime_error("commands must contain 1..128 operations");
            for(const auto& command:commands.arr)if(const auto* op=command.find("op");op&&op->t==V::Str&&op->str.rfind("drawing.",0)==0&&!args.find("expected_revision"))throw std::runtime_error("Technical drawing authoring requires expected_revision from current doc_info");
            for(const auto& command:commands.arr)if(const auto* op=command.find("op");op&&op->asStr()=="assembly.drive"&&!args.find("expected_revision"))throw std::runtime_error("assembly.drive requires expected_revision from the inspected document");
            for(const auto& command:commands.arr)if(const auto* op=command.find("op");op&&op->t==V::Str&&op->str.rfind("motion.",0)==0&&!args.find("expected_revision"))throw std::runtime_error("Motion authoring requires expected_revision from current doc_info");
            for(const auto& command:commands.arr)if(const auto* op=command.find("op");op&&op->t==V::Str&&op->str.rfind("explode.",0)==0&&!args.find("expected_revision"))throw std::runtime_error("Exploded view authoring requires expected_revision from current doc_info");
            for(const auto& command:commands.arr)if((command.find("part")||(command.find("op")&&command.find("op")->asStr()=="bom.initialize"))&&!args.find("expected_revision"))throw std::runtime_error("BOM authoring requires expected_revision from current doc_info");
            for(const auto& command:commands.arr)if(!args.find("expected_revision")){
                const auto* op=command.find("op"),*type=command.find("type");
                if(op&&op->t==V::Str&&(op->str=="feature.move"||op->str=="feature.rollback"||op->str.rfind("configuration.",0)==0||op->str=="assembly.load"||op->str.rfind("assembly.external.",0)==0))throw std::runtime_error("History and configuration authoring require expected_revision from current doc_info");
                if((op&&op->t==V::Str&&op->str.rfind("skeleton.",0)==0)||(type&&type->t==V::Str&&type->str=="mesh_edit"))throw std::runtime_error("Skeleton and indexed mesh authoring require expected_revision from current doc_info");
            }
            for(const auto& command:commands.arr)if(const auto* op=command.find("op");op&&op->t==V::Str&&(op->str=="light_up"||op->str=="sheet.vent"||op->str.rfind("stamp.",0)==0||op->str.rfind("image_asset.",0)==0||op->str.rfind("face_appearance.",0)==0)&&!args.find("expected_revision"))throw std::runtime_error("Image and face authoring require expected_revision");
            const bool appearanceOnly=metadataOnlyApply(document,args);
            Document next;std::string error;
            if(appearanceOnly)next=document.authoringCopy();
            else {if(!documentFromJson(documentToJson(document,true,false),next,&error))throw std::runtime_error(error);
                next.evaluationCache=document.evaluationCache;next.evaluationGeneration=document.evaluationGeneration;next.referenceFrameCache=document.referenceFrameCache;next.centroidCache=document.centroidCache;}

            V created=V::makeArr(),createdNodes=V::makeArr(),createdMates=V::makeArr(),sheetEdits=V::makeArr();
            for(auto& c:commands.arr){
                applying=true;commandIndex=(size_t)(&c-commands.arr.data());commandOp=c.find("op")?c.find("op")->asStr():"";
                auto op=str(c,"op");
                if(next.historyTip&&(op=="feature.add"||op=="sheet.create"||op=="skeleton.set"))throw std::runtime_error("Resume full feature history before adding features or changing Skeleton source");
                if(op=="feature.move"&&skeletonOwnsFeature(next,featureRef(next,field(c,"id"))))throw std::runtime_error("Skeleton pattern is source-owned; change its source instead of reordering generated features");
                if(applyFeatureHistoryCommand(next,c)){}
                else if(applyAssemblyAuthoringCommand(next,c,error)){if(!error.empty())throw std::runtime_error(error);}
                else if(op.rfind("motion.",0)==0){if(op=="motion.graph.set"||op=="motion.bake")validated(next);if(!applyMotionCommand(next,c,revision,error))throw std::runtime_error("Unknown operation: "+op);if(!error.empty())throw std::runtime_error(error);}
                else if(op.rfind("explode.",0)==0){if(!applyExplodedViewCommand(next,c,error))throw std::runtime_error("Unknown operation: "+op);if(!error.empty())throw std::runtime_error(error);}
                else if(op=="bom.initialize"){if(c.obj.size()!=1)throw std::runtime_error("bom.initialize accepts only op");if(next.bomDocumentId.empty())next.bomDocumentId=newBomDocumentId();}
                else if(applyAppearance024BCommand(next,c)){}
                else if(op.rfind("drawing.",0)==0)applyDrawingCommand(next,c);
                else if(op=="skeleton.set")setSkeletonSource(next,skeletonSource(c));
                else if(op=="skeleton.remove"){setSkeletonSource(next,"");next.skeleton=V{};}
                else if(op=="param.set"){const auto name=str(c,"name");if(c.find("expr")||!c.find("hud")){const auto expr=expression(field(c,"expr"));setSkeletonInput(next,name,expr);next.setParam(name,expr);}if(const auto* hud=c.find("hud"))applyParameterHud(next,name,*hud);}
                else if(op.rfind("sheet.",0)==0){V bound=c;if(op!="sheet.create")bound.set("id",V::makeNum((double)featureRef(next,field(c,"id"))));auto edited=applySheetCommand(next,bound);if(op=="sheet.create")created.arr.push_back(*edited.find("feature_id"));sheetEdits.arr.push_back(edited);}
                else if(op.rfind("frame.",0)==0){V bound=c;bound.set("id",V::makeNum((double)featureRef(next,field(c,"id"))));auto edited=applyFrameCommand(next,bound);sheetEdits.arr.push_back(edited);}
                else if(op=="plane.set"||op=="plane.remove"){
                    auto name=str(c,"name");if(name=="XY"||name=="XZ"||name=="YZ"||name.size()>128)throw std::runtime_error("Built-in datums are immutable; custom names must be 1..128 characters");
                    auto it=std::find_if(next.planes.begin(),next.planes.end(),[&](const DatumPlane& p){return p.name==name;});
                    if(name=="Origin"&&it==next.planes.end())throw std::runtime_error("Built-in datums are immutable; cannot create a new authored Origin");
                    if(op=="plane.remove"){if(it==next.planes.end())throw std::runtime_error("Plane not found");next.planes.erase(it);}
                    else{
                        if(it==next.planes.end()){next.planes.push_back({name});it=next.planes.end()-1;}
                        if(c.find("parent"))it->parent=str(c,"parent");
                        if(c.find("kind")){auto kind=str(c,"kind");if(kind!="plane"&&kind!="axis"&&kind!="frame"&&kind!="point")throw std::runtime_error("Datum kind must be plane, axis, frame or point");it->kind=kind;}
                        if(auto support=c.find("support")){
                            if(support->t!=V::Obj&&support->t!=V::Null)throw std::runtime_error("Datum support must be object or null");it->support=*support;
                            if(it->support.t==V::Obj)bindDatumSupportProducers(next,it->support);
                        }
                        if(auto p=c.find("p")){if(p->t!=V::Obj)throw std::runtime_error("plane p must be object");for(auto& kv:p->obj)it->p[kv.first]=expression(kv.second);}
                    }
                }
                else if(op=="material.set"||op=="material.remove"){
                    const std::string name=str(c,"name");if(name.size()>128)throw std::runtime_error("Material name must be 1..128 characters");
                    auto found=std::find_if(next.materials.begin(),next.materials.end(),[&](const Material& material){return material.name==name;});
                    if(op=="material.remove"){
                        if(found==next.materials.end())throw std::runtime_error("Material not found");next.materials.erase(found);
                    }else{
                        if(found==next.materials.end()){if(next.materials.size()>=256)throw std::runtime_error("Material capacity exceeded");next.materials.push_back({});found=next.materials.end()-1;found->name=name;}
                        if(const V* density=c.find("density")){if(density->t!=V::Num||!std::isfinite(density->num)||density->num<0||density->num>100)throw std::runtime_error("Material density must be 0..100 g/cm3");found->densityGcm3=density->num;}
                        if(const V* density=c.find("density_kg_m3")){if(c.find("density"))throw std::runtime_error("Choose density or density_kg_m3");if(density->t!=V::Num||!std::isfinite(density->num)||density->num<0||density->num>100000)throw std::runtime_error("Material density must be 0..100000 kg/m3");found->densityGcm3=density->num/1000;}
                        if(const V* physical=c.find("physical"))found->physical=*physical;
                        if(const V* patch=c.find("appearance")){std::string reason;if(!applyAppearancePatch(found->appearance,*patch,reason))throw std::runtime_error(reason);}
                    }
                }
                else if(op=="feature.add"){
                    Feature f;auto type=str(c,"type");
                    if(!featureTypeFromName(type,f.type))throw std::runtime_error(unknownFeatureType(type));
                    if(f.type==FeatureType::SourceTube||f.type==FeatureType::SourceSheet)throw std::runtime_error("Use the source STEP import tool to create verified source geometry");
                    f.name=c.find("name")?str(c,"name"):type;
                    if(auto p=c.find("p"))params(f,*p);
                    if(auto b=c.find("bends"))bends(f,*b);
                    if(auto n=c.find("notches"))notches(f,*n);
                    if(auto spec=c.find("native")){nativeSpecAllowed(f.type,*spec);f.nativeSpec=*spec;}
                    else if(isNativeFeature(f.type)||f.type==FeatureType::Suspension||f.type==FeatureType::MeshEdit)f.nativeSpec=V::makeObj();
                    if(auto mesh=c.find("mesh")){if(f.type!=FeatureType::MeshBody||mesh->t!=V::Obj)throw std::runtime_error("mesh is only valid as an object on mesh_body");f.sourceData=*mesh;}
                    if(const auto* edit=c.find("mesh_edit")){if(c.find("native")||f.type!=FeatureType::MeshEdit||edit->t!=V::Obj)throw std::runtime_error("mesh_edit selection must be an object on mesh_edit, without another native field");f.nativeSpec=*edit;}
                    if(f.type==FeatureType::MeshBody&&f.sourceData.t!=V::Obj)throw std::runtime_error("mesh_body requires mesh:{positions,indices}");
                    if(auto in=c.find("inputs")){if(in->t!=V::Arr)throw std::runtime_error("inputs must be an array");for(auto& x:in->arr)f.inputs.push_back(featureRef(next,x));}
                    if(auto mode=c.find("mode")){if(mode->t!=V::Num||mode->num<0||mode->num>2||mode->num!=std::floor(mode->num))throw std::runtime_error("mode must be 0, 1 or 2");f.opInt=(int)mode->num;}
                    featureAppearance(f,c);
                    if(const auto* part=c.find("part")){f.part=part->isNull()?V{}:validatePartMetadata(*part);if(f.part.t==V::Obj&&next.bomDocumentId.empty())next.bomDocumentId=newBomDocumentId();}
                    if(f.type==FeatureType::NativeSolid&&f.inputs.size()==2){
                        const auto* operation=f.nativeSpec.find("op");
                        if(operation&&operation->t==V::Str&&operation->str=="tube_end_fit"){
                            const auto* source=next.find(f.inputs[0]);
                            if(source){
                                if(!c.find("material"))f.material=source->material;
                                if(!c.find("appearance"))f.appearance=source->appearance;
                            }
                        }
                    }
                    created.arr.push_back(V::makeNum((double)next.addFeature(std::move(f))));
                }
                else if(op=="feature.edit"){
                    auto* f=next.find(featureRef(next,field(c,"id")));if(!f)throw std::runtime_error("Feature not found");
                    if(skeletonOwnsFeature(next,f->id)&&(c.find("p_remove")||c.find("p")||c.find("native")||c.find("inputs")||c.find("name")))throw std::runtime_error("Skeleton pattern is source-owned; edit its skeleton code or inputs");
                    if(f->type==FeatureType::MeshEdit&&!args.find("expected_revision"))throw std::runtime_error("Indexed mesh edits require expected_revision");
                    if((f->type==FeatureType::SourceTube||f->type==FeatureType::SourceSheet)&&(c.find("p_remove")||c.find("p")||c.find("bends")||c.find("notches")||c.find("native")||c.find("inputs")))throw std::runtime_error("Imported source geometry is read-only; reimport changed STEP");
                    if(const auto* inputs=c.find("inputs")){
                        if(!args.find("expected_revision"))throw std::runtime_error("Feature input retarget requires expected_revision from current doc_info");
                        if(inputs->t!=V::Arr||inputs->arr.size()>128)throw std::runtime_error("Feature inputs must be an array of at most 128 earlier dependencies");
                        std::vector<u64> replacement;std::set<u64> unique;bool earlier=true;
                        for(const auto& value:inputs->arr){const auto input=featureRef(next,value);if(!next.find(input))throw std::runtime_error("Feature input not found: "+std::to_string(input));if(input==f->id||!unique.insert(input).second)throw std::runtime_error("Feature inputs cannot repeat or reference the edited feature");
                            earlier=false;for(const auto& prior:next.features){if(prior.id==f->id)break;if(prior.id==input){earlier=true;break;}}
                            if(!earlier)throw std::runtime_error("Feature inputs must precede the edited feature; forward or cyclic retarget rejected");replacement.push_back(input);}
                        f->inputs=std::move(replacement);
                    }
                    if(const auto* remove=c.find("p_remove")){if(remove->t!=V::Arr||remove->arr.size()>128)throw std::runtime_error("p_remove requires at most 128 parameter names");for(const auto& key:remove->arr){if(key.t!=V::Str||key.str.empty())throw std::runtime_error("p_remove names must be nonempty text");f->p.erase(key.str);}}
                    if(auto p=c.find("p"))params(*f,*p);
                    if(auto b=c.find("bends"))bends(*f,*b);
                    if(auto n=c.find("notches"))notches(*f,*n);
                    if(auto spec=c.find("native")){nativeSpecAllowed(f->type,*spec);f->nativeSpec=*spec;}
                    if(auto mesh=c.find("mesh")){if(f->type!=FeatureType::MeshBody||mesh->t!=V::Obj)throw std::runtime_error("mesh is only valid as an object on mesh_body");f->sourceData=*mesh;}
                    if(const auto* edit=c.find("mesh_edit")){if(c.find("native")||f->type!=FeatureType::MeshEdit||edit->t!=V::Obj)throw std::runtime_error("mesh_edit selection must be an object on mesh_edit, without another native field");f->nativeSpec=*edit;}
                    if(c.find("name"))f->name=str(c,"name");
                    featureAppearance(*f,c);
                    if(const auto* part=c.find("part")){f->part=part->isNull()?V{}:validatePartMetadata(*part);if(f->part.t==V::Obj&&next.bomDocumentId.empty())next.bomDocumentId=newBomDocumentId();}
                    if(auto flag=c.find("suppressed")){if(flag->t!=V::Bool)throw std::runtime_error("suppressed must be boolean");f->suppressed=flag->b;}
                }
                else if(op=="feature.remove"){const auto target=featureRef(next,field(c,"id"));if(target==next.historyTip)throw std::runtime_error("Resume full history before removing its rollback tip");if(!next.remove(target))throw std::runtime_error("Feature not found");}
                else if(op=="canvas.set"||op=="canvas.remove"){
                    auto name=str(c,"name");if(name.size()>128)throw std::runtime_error("Canvas name must be 1..128 characters");
                    auto it=std::find_if(next.canvases.begin(),next.canvases.end(),[&](const PlaneCanvas& canvas){return canvas.name==name;});
                    if(op=="canvas.remove"){if(it==next.canvases.end())throw std::runtime_error("Canvas not found");next.canvases.erase(it);}
                    else{
                        if(it==next.canvases.end()){if(next.canvases.size()>=8)throw std::runtime_error("Canvas capacity exceeded");next.canvases.push_back({});it=next.canvases.end()-1;it->name=name;}
                        if(!c.find("width")||!c.find("height"))throw std::runtime_error("Canvas needs width and height");
                        if(c.find("asset")){it->asset=str(c,"asset");it->path.clear();if(c.find("path"))throw std::runtime_error("Choose canvas asset or path, not both");}
                        else if(c.find("path")||(it->path.empty()&&it->asset.empty())){auto path=str(c,"path");if(path.size()>1024)throw std::runtime_error("Canvas path must be 1..1024 characters");it->path=path;it->asset.clear();}
                        if(c.find("plane")){auto plane=str(c,"plane");if(plane.size()>128)throw std::runtime_error("Canvas plane must be a name");it->plane=plane;}
                        it->width=expression(field(c,"width"));it->height=expression(field(c,"height"));
                        if(auto opacity=c.find("opacity")){if(opacity->t!=V::Num||!std::isfinite(opacity->num)||opacity->num<0||opacity->num>1)throw std::runtime_error("Canvas opacity must be 0..1");it->opacity=opacity->num;}
                    }
                }
                else if(op=="sketch.set"){
                    V sketch=field(c,"sketch");bindSketchProjects(next,sketch);
                    Sketch sk;if(!sketchFromJson(sketch.dump(),sk,&error))throw std::runtime_error(error);
                    next.setSketch(str(c,"name"),std::move(sk));
                }
                else if(op=="sketch.remove"){if(!next.removeSketch(str(c,"name")))throw std::runtime_error("Sketch not found");}
                else if(op=="sketch.patch"){auto* sketch=next.findSketch(str(c,"name"));if(!sketch)throw std::runtime_error("Sketch not found");patchSketch(*sketch,c,error);}
                else if(op=="sketch.entity.set"){
                    auto* sk=next.findSketch(str(c,"name"));if(!sk)throw std::runtime_error("Sketch not found");
                    V entity=field(c,"entity");bindProjectFeature(next,entity);auto name=str(entity,"name");V wrapper=V::makeObj(),entities=V::makeArr();entities.arr.push_back(entity);wrapper.set("entities",entities);Sketch single;
                    if(!sketchFromJson(wrapper.dump(),single,&error))throw std::runtime_error(error);
                    auto it=std::find_if(sk->sk.ents.begin(),sk->sk.ents.end(),[&](const SketchEnt& e){return e.name==name;});
                    if(it==sk->sk.ents.end())sk->sk.ents.push_back(std::move(single.ents.front()));else *it=std::move(single.ents.front());
                    Sketch checked;if(!sketchFromJson(sketchToJson(sk->sk),checked,&error))throw std::runtime_error(error);sk->sk=std::move(checked);
                }
                else if(op=="sketch.fillet"||op=="sketch.chamfer"){
                    auto* sk=next.findSketch(str(c,"name"));if(!sk)throw std::runtime_error("Sketch not found");
                    const bool chamfer=op=="sketch.chamfer";
                    const std::string entity=str(c,"entity");
                    const V& index=field(c,"corner");
                    if(index.t!=V::Num||index.num<0||index.num>4096||index.num!=std::floor(index.num))throw std::runtime_error("Sketch corner index must be an integer");
                    const std::string size=expression(field(c,chamfer?"distance":"radius"));
                    bool replaced=false;
                    for(auto& item:sk->sk.corners)if(item.entity==entity&&item.corner==(int)index.num){item.chamfer=chamfer;item.size=size;replaced=true;break;}
                    if(!replaced){if(sk->sk.corners.size()>=64)throw std::runtime_error("Sketch corner capacity exceeded");sk->sk.corners.push_back({entity,(int)index.num,chamfer,size});}
                    Sketch checked;if(!sketchFromJson(sketchToJson(sk->sk),checked,&error))throw std::runtime_error(error);sk->sk=std::move(checked);
                }
                else if(op=="assembly.add"){
                    AssemblyNode n;n.id=next.nextAssemblyId_++;n.name="Component";editNode(n,c,next);next.assembly.push_back(n);createdNodes.arr.push_back(V::makeNum((double)n.id));
                }
                else if(op=="assembly.drive"){
                    // Earlier commands in this isolated batch may have changed geometry or references.
                    validated(next);const auto preview=driveAssembly(next,c);
                    for(const auto& command:preview.commands.arr){const u64 key=nodeRef(next,field(command,"id"));auto it=std::find_if(next.assembly.begin(),next.assembly.end(),[&](const AssemblyNode& node){return node.id==key;});if(it==next.assembly.end())throw std::runtime_error("Assembly drive node not found");editNode(*it,command,next);}
                }
                else if(op=="mate.add"){
                    if(next.mates.size()>=256)throw std::runtime_error("Assembly mate capacity exceeded");AssemblyMate m;m.id=next.nextMateId_++;V mateArgs=c;bindEndpoint(next,mateArgs,"a");bindEndpoint(next,mateArgs,"b");editMate(m,mateArgs);next.mates.push_back(m);createdMates.arr.push_back(V::makeNum((double)m.id));
                }
                else if(op=="mate.edit"||op=="mate.remove"){
                    auto key=mateKey(next,c);auto it=std::find_if(next.mates.begin(),next.mates.end(),[&](const AssemblyMate& m){return m.id==key;});if(it==next.mates.end())throw std::runtime_error("Mate not found");if(op=="mate.edit"){V mateArgs=c;bindEndpoint(next,mateArgs,"a");bindEndpoint(next,mateArgs,"b");editMate(*it,mateArgs);}else next.mates.erase(it);
                }
                else if(op=="assembly.edit"||op=="assembly.remove"){
                    u64 key=nodeRef(next,field(c,"id"));auto it=std::find_if(next.assembly.begin(),next.assembly.end(),[&](const AssemblyNode& n){return n.id==key;});
                    if(it==next.assembly.end())throw std::runtime_error("Assembly node not found");
                    if(op=="assembly.edit")editNode(*it,c,next);else next.assembly.erase(it);
                }
                else throw std::runtime_error("Unknown operation: "+op);
            }
            applying=false;
            validateHudTransition(document,next);
            validateDesignContainerTransition(document,next);
            validateDatumTransition(document,next);
            const bool drawingEdits=std::any_of(commands.arr.begin(),commands.arr.end(),[](const V& c){const auto* op=c.find("op");return op&&op->t==V::Str&&(op->str=="drawing.set"||op->str=="drawing.refresh");});
            if(drawingEdits){validated(next);for(const auto& c:commands.arr){const auto* op=c.find("op");if(op&&op->t==V::Str&&(op->str=="drawing.set"||op->str=="drawing.refresh")){const auto name=op->str=="drawing.set"?c.find("drawing")->find("name")->str:c.find("name")->str;const bool exists=std::any_of(next.drawings.begin(),next.drawings.end(),[&](const V& drawing){return drawing.find("name")->str==name;});if(exists)refreshDrawing(next,name,revision+1);}}}
            V conditionReport;
            if(const V* checks=args.find("checks")){
                if(appearanceOnly){next.evalBodies=document.evalBodies;next.lastResults=document.lastResults;}
                else validated(next);
                AgentSession checking;checking.document=std::move(next);checking.revision=revision;
                conditionReport=evaluatePostconditions(checking,*checks);next=std::move(checking.document);
                if(!conditionReport.find("passed")->b){V failure=V::makeObj();failure.set("error",V::makeStr(failedPostconditionSummary(conditionReport)));failure.set("error_code",V::makeStr("POSTCONDITION_FAILED"));failure.set("stage",V::makeStr("postconditions"));failure.set("revision",V::makeNum((double)revision));failure.set("document_unchanged",V::makeBool(true));failure.set("conditions",conditionReport);return result(failure,true);}
            }
            if(appearanceOnly){
                next.evalBodies=document.evalBodies;next.lastResults=document.lastResults;
                validatedMaterials(next);validateAppearanceAuthoring024B(next,true);
                if(appearance024BNeedsVersion39(next)&&documentToJson(next,true,false).size()>maxDocumentBytes)throw std::runtime_error("Embedded image document exceeds 4 MiB; reduce image dimensions/size");
                remember();document.materials=std::move(next.materials);document.bomDocumentId=std::move(next.bomDocumentId);document.imageAssets=std::move(next.imageAssets);document.stamps=std::move(next.stamps);
                for(size_t i=0;i<document.features.size();++i){document.features[i].material=std::move(next.features[i].material);document.features[i].appearance=std::move(next.features[i].appearance);document.features[i].part=std::move(next.features[i].part);document.features[i].faceAppearances=std::move(next.features[i].faceAppearances);}
            }else{if(!args.find("checks")&&!drawingEdits)validated(next);remember();document=std::move(next);}
            ++revision;
            V r=args.find("detail")&&args.find("detail")->str=="changes"?revisionReceipt032D(document,revision):info();r.set("created_ids",created);r.set("created_node_ids",createdNodes);r.set("created_mate_ids",createdMates);r.set("sheet_edits",sheetEdits);r.set("geometry_recomputed",V::makeBool(!appearanceOnly));if(args.find("checks"))r.set("conditions",conditionReport);return result(r);
        }
        if(tool=="doc_undo"||tool=="doc_redo"){
            if(document.iterationHistory.t==V::Obj){Document next;iterationRestore(document,iterationUndoTarget(document,tool=="doc_redo"),next);validated(next);document=std::move(next);++revision;prepared_.clear();return result(info());}
            auto& source=tool=="doc_undo"?undo_:redo_;auto& destination=tool=="doc_undo"?redo_:undo_;
            if(source.empty())throw std::runtime_error("No history available");
            Document next;std::string error;if(!documentFromJson(source.back(),next,&error))throw std::runtime_error(error);
            next.usage=document.usage;next.agentActivity=document.agentActivity; // billed provider receipts survive geometry undo/redo
            validated(next);destination.push_back(documentToJson(document));source.pop_back();document=std::move(next);++revision;prepared_.clear();return result(info());
        }
        if(tool=="doc_save"){
            if(documentToJson(document).size()>maxDocumentBytes)throw std::runtime_error("Document capacity exceeds the total 4 MiB limit; save rejected and target preserved");
            auto path=str(args,"path");DocumentSaveReport saved;if(!saveDocument(document,path.c_str(),&saved))throw std::runtime_error("Could not save document"+(saved.error.empty()?std::string():": "+saved.error));
            V r=info();r.set("path",V::makeStr(path));
            if(saved.derivedGeometry){
                // Optional geometry cache receipt; omitted bodies recompute from authoring on open.
                V geometry=V::makeObj();geometry.set("status",V::makeStr(saved.derivedOmittedEntries?"partial":"saved"));geometry.set("entries_saved",V::makeNum((double)saved.derivedEntries));geometry.set("bytes_saved",V::makeNum((double)saved.derivedBytes));geometry.set("entries_omitted",V::makeNum((double)saved.derivedOmittedEntries));geometry.set("bytes_omitted",V::makeNum((double)saved.derivedOmittedBytes));geometry.set("capacity_bytes",V::makeNum((double)saved.derivedCapacityBytes));
                if(saved.derivedOmittedEntries)geometry.set("omitted_reason",V::makeStr("Saved geometry is an optional cache with a 128 MiB capacity. Authoring was saved in full; omitted bodies are evaluated from authoring on the next open."));
                r.set("saved_geometry",geometry);
            }
            return result(r);
        }
        if(tool=="doc_load"){
            Document next;std::string error;
            if(!loadDocument(str(args,"path").c_str(),next,&error))throw std::runtime_error(error);
            validated(next);remember(true);document=std::move(next);memorySessionScope.clear();++revision;prepared_.clear();return result(info());
        }
        if(tool=="mesh_inspect"){
            auto& cached=meshCache(args);V report=cached.report.json();report.set("topology_fingerprint",V::makeStr(meshTopologyFingerprint(cached.mesh)));
            if(const auto* check=args.find("self_intersections")){if(check->t!=V::Bool)throw std::runtime_error("self_intersections must be boolean");if(check->b)report.set("self_intersection_diagnosis",inspectMeshSelfIntersections(cached.mesh));}
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
            if(m.nrm.size()!=m.pos.size())m.computeNormals();
            if(format=="stl")ok=exportSTL(path.c_str(),m);
            else if(format=="obj")ok=exportOBJ(path.c_str(),m);
            else if(format=="fbx")ok=exportFBX(path.c_str(),m);
            else throw std::runtime_error("format must be stl, obj or fbx");
            if(!ok)throw std::runtime_error("Export failed");
            V r=V::makeObj();r.set("path",V::makeStr(path));r.set("triangles",V::makeNum(m.triCount()));r.set("units",V::makeStr("mm"));r.set("authority",V::makeStr("display_mesh"));r.set("parametric_history_preserved",V::makeBool(false));return result(r);
        }
        if(tool=="snapshot"){
            std::string path;if(args.find("path"))path=str(args,"path");std::string view="iso";
            if(const auto* expected=args.find("expected_revision");expected&&(expected->t!=V::Num||expected->num!=(double)revision))throw std::runtime_error("Revision conflict; the current revision is in this result. Inspect only what your edit depends on and retry");
            if(args.find("candidate_id"))return snapshotCandidate(args);
            bool inlineImage=true;if(const auto* flag=args.find("inline_image")){if(flag->t!=V::Bool)throw std::runtime_error("inline_image must be boolean");inlineImage=flag->b;}
            if(!inlineImage&&path.empty())throw std::runtime_error("A path is required when inline_image is false");
            SnapshotAppearanceOptions044B appearance;V appearanceEvidence=V::makeObj();appearance.receipt=&appearanceEvidence;if(const auto* time=args.find("media_time_s")){if(time->t!=V::Num||!std::isfinite(time->num)||time->num<0||time->num>86400)throw std::runtime_error("media_time_s must be 0..86400 seconds");appearance.mediaTimeS=time->num;}
            std::vector<AssemblyPlacement> captured;std::vector<SnapshotMaterial> materials;Mesh m=selectedMesh(document,args,&captured,&materials,&appearance);SnapCam camera=configuredSnapshotCamera(args,m.computeBBox(),view);
            const int width=inlineImage?1024:1280,height=inlineImage?640:800;const Image image=renderSnapshot(m,width,height,camera,materials);
            if(!inlineImage&&!path.empty()&&!writePNG(path.c_str(),image))throw std::runtime_error("Snapshot failed");
            MeshSelection sel=meshSelection(document,args);
            V r=V::makeObj();r.set("path",V::makeStr(path));r.set("view",V::makeStr(view));r.set("display_mesh_authoritative",V::makeBool(false));r.set("authority",V::makeStr("display_mesh"));
            if(sel.useNode){r.set("node",V::makeNum((double)sel.node));if(!sel.nodeName.empty())r.set("name",V::makeStr(sel.nodeName));}
            if(sel.copy>=0)r.set("copy",V::makeNum(sel.copy));
            V evidence=V::makeObj();evidence.set("revision",V::makeNum((double)revision));evidence.set("current_revision",V::makeNum((double)revision));evidence.set("view",V::makeStr(view));evidence.set("width",V::makeNum(width));evidence.set("height",V::makeNum(height));evidence.set("authority",V::makeStr("display_mesh"));evidence.set("capture_source",V::makeStr("software_snapshot"));
            V viewState=V::makeObj();viewState.set("target_mm",snapshotVectorValue(camera.target));viewState.set("direction_target_to_eye",snapshotVectorValue(camera.direction));viewState.set("up",snapshotVectorValue(camera.up));viewState.set("distance_mm",V::makeNum(camera.dist));viewState.set("fov_deg",V::makeNum(camera.fovDeg));evidence.set("view_state",viewState);
            evidence.set("appearance",V::makeStr("authored_body_and_face_color_opacity_emission"));evidence.set("emission_sample",V::makeStr("steady"));evidence.set("transparency",V::makeStr("sorted_triangles"));evidence.set("textures_and_stamps",V::makeBool(true));evidence.set("media_time_s",V::makeNum(appearance.mediaTimeS));evidence.set("appearance_surfaces",appearanceEvidence);evidence.set("appearance_scope",V::makeStr("triplanar_textures_procedural_patterns_surface_stamps_flat_lit_no_bump_no_plane_stamps"));
            if(sel.feature)evidence.set("feature_id",V::makeNum((double)sel.feature));if(sel.useNode){evidence.set("occurrence_id",V::makeNum((double)sel.node));if(!sel.nodeName.empty())evidence.set("name",V::makeStr(sel.nodeName));if(sel.copy>=0)evidence.set("copy",V::makeNum(sel.copy));std::set<u64> definitions;for(const auto& occurrence:captured)definitions.insert(occurrence.feature);if(definitions.size()==1)evidence.set("feature_id",V::makeNum((double)*definitions.begin()));else{V ids=V::makeArr();for(u64 feature:definitions)ids.arr.push_back(V::makeNum((double)feature));evidence.set("feature_ids",ids);}}
            for(const char* key:{"label","reference","stage"})if(const auto* value=args.find(key)){if(value->t!=V::Str||value->str.size()>128)throw std::runtime_error("Snapshot label/reference/stage must be a bounded string");evidence.set(key,*value);}
            evidence.set("committed",V::makeBool(true));r.set("revision",V::makeNum((double)revision));r.set("visual_evidence",evidence);V output=result(r);
            if(inlineImage)return snapshotImageResult(image,evidence,path);
            return output;
        }
        throw std::runtime_error("Unknown tool: "+tool);
    }catch(const std::exception& e){
        V r=V::makeObj();const std::string message=e.what();
        r.set("error",V::makeStr(message));r.set("revision",V::makeNum((double)revision));r.set("tool",V::makeStr(tool));
        const bool resource=message.find("budget")!=std::string::npos||message.find("worker limit")!=std::string::npos||message.find("capacity")!=std::string::npos;
        r.set("error_code",V::makeStr(message.find("Revision conflict")!=std::string::npos?"REVISION_CONFLICT":message.find("Ambiguous")!=std::string::npos?"AMBIGUOUS_REFERENCE":message.find("Prepared candidate")!=std::string::npos?"CANDIDATE_INVALID":resource?"RESOURCE_LIMIT":applying?"INVALID_COMMAND":"EVALUATION_FAILED"));
        if(tool=="tool_feedback"||tool=="activity_info"){r.set("error_code",V::makeStr("ACTIVITY_VALIDATION_FAILED"));}
        if(tool=="object_context"&&(message.find("OBJECT_CONTEXT_REFERENCE_MISSING")!=std::string::npos||message.find("name not found")!=std::string::npos))r.set("error_code",V::makeStr("REFERENCE_MISSING"));
        for(const char* prefix:{"MECHANICAL_","ASSEMBLY_","MANIPULATION_"})if(const auto start=message.find(prefix);start!=std::string::npos){const auto end=message.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ_",start);r.set("error_code",V::makeStr(message.substr(start,end==std::string::npos?end:end-start)));break;}
        if(message.rfind("DFM_ACCEPTANCE_",0)==0||message.rfind("HUD_",0)==0){const auto end=message.find(':');r.set("error_code",V::makeStr(message.substr(0,end)));}
        if(message.rfind("EXPLODE_",0)==0||message.rfind("VISUALIZE_",0)==0)r.set("error_code",V::makeStr(message.substr(0,message.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ_"))));
        if(r.find("error_code")->str=="REVISION_CONFLICT"){
            // The current revision is already in this result. Point at the cheap
            // explicit path instead of a full design read; never rebase silently.
            if(const auto* expected=args.find("expected_revision"))r.set("expected_revision",*expected);
            V recovery=V::makeObj(),steps=V::makeArr();recovery.set("current_revision",V::makeNum((double)revision));
            steps.arr.push_back(V::makeStr("Use this result's revision as the current revision; doc_info detail:revision confirms it without a full design read."));
            steps.arr.push_back(V::makeStr("Inspect only the objects your edit depends on (object_context or doc_info ids) to see whether another writer changed them."));
            steps.arr.push_back(V::makeStr(args.find("candidate_id")?"Retry this owned candidate with candidate_id, expected_revision set to the current revision and refresh_candidate:true; the host rechecks it atomically or rejects it.":"Resubmit with expected_revision set to the current revision after confirming your inputs are unchanged."));
            recovery.set("steps",steps);recovery.set("avoid",V::makeStr("Do not reread the full design or broadcast this conflict; it is a concurrency outcome, not a geometry failure."));r.set("recovery",recovery);
        }
        r.set("stage",V::makeStr(args.find("candidate_id")?"prepared_apply":applying?"command":"evaluation"));
        if(tool=="tool_feedback"||tool=="activity_info")r.set("stage",V::makeStr("audit_arguments"));
        if(applying){r.set("command_index",V::makeNum(commandIndex));r.set("operation",V::makeStr(commandOp));}
        if(message.rfind("Missing field: ",0)==0)r.set("field",V::makeStr(message.substr(15)));
        if(message.rfind("Feature #",0)==0){const auto end=message.find(':',9);if(end!=std::string::npos){auto digits=message.substr(9,end-9);if(!digits.empty()&&digits.find_first_not_of("0123456789")==std::string::npos)r.set("feature_id",V::makeNum(std::strtod(digits.c_str(),nullptr)));}}
        if(message.find("Native operation must produce one positive-volume solid;")!=std::string::npos){
            r.set("error_code",V::makeStr("NATIVE_SINGLE_SOLID_REQUIRED"));
            const std::string marker="result contains ";auto start=message.find(marker);
            if(start!=std::string::npos){start+=marker.size();auto end=message.find(" solids",start);if(end!=std::string::npos){auto count=message.substr(start,end-start);if(!count.empty()&&count.find_first_not_of("0123456789")==std::string::npos)r.set("observed_solid_count",V::makeNum(std::strtod(count.c_str(),nullptr)));}}
            V recovery=V::makeObj(),checks=V::makeArr(),options=V::makeArr();
            recovery.set("inspect_before_retry",V::makeBool(true));
            recovery.set("rejected_feature_may_not_exist",V::makeBool(true));
            checks.arr.push_back(V::makeStr("Read current doc_info detail design; inspect the existing input features, not an uncommitted output ID."));
            checks.arr.push_back(V::makeStr("Use native_inspect and geometry_query to check input solids, contact/overlap, cutter extent and closed profiles."));
            options.arr.push_back(V::makeStr("If these are separate physical parts, keep their definitions separate and ask the orchestrator to place assembly instances."));
            options.arr.push_back(V::makeStr("If the design requires one body, revise the intended contact or cut geometry after inspecting the inputs."));
            recovery.set("checks",checks);recovery.set("design_options",options);
            recovery.set("avoid",V::makeStr("Do not repeat the unchanged operation, discard extra solids, widen tolerances or claim the rejected output was committed."));r.set("recovery",recovery);
        }
        r.set("document_unchanged",V::makeBool(true));return result(r,true);
    }
}
V AgentSession::tools(bool detailed){
    struct Def{const char* name;const char* desc;const char* schema;bool read;};
    const Def defs[]={
        {"history_info","Inspect persistent immutable iteration metadata and branch heads. limit 1..64, newest first. before_id excludes that anchor and pages older nodes; has_more/next_before_id indicate continuation. Baseline represents only the first observed authoring state. Actor identity is caller-declared. No geometry or token ledger snapshots are returned.",R"({"type":"object","properties":{"limit":{"type":"integer","minimum":1,"maximum":64},"before_id":{"type":"string"}}})",true},
        {"iteration_restore","Restore saved authoring as a new branch iteration, preserving all prior nodes, current usage receipts and monotonic revision. Exact geometry is re-evaluated; any failure or total 4 MiB capacity violation rejects atomically. Actor is declared provenance.",R"({"type":"object","properties":{"id":{"type":"string"},"expected_revision":{"type":"integer","minimum":0},"motivation":{"type":"string","maxLength":4096},"actor":{"type":"object","properties":{"kind":{"type":"string"},"id":{"type":"string"},"role":{"type":"string"}},"required":["kind","id"]}},"required":["id","expected_revision"]})",false},
        {"suspension_inspect","Solve a double-wishbone suspension preview at jounce_mm, rack_mm and wheel_spin_deg, returning hardpoints, link closure residuals, toe/camber, spring stroke and declared-rate force. Read-only kinematics, not dynamic/inertia/contact simulation or native B-rep authority.",R"({"type":"object","properties":{"id":{"type":["integer","string"]},"jounce_mm":{"type":"number"},"rack_mm":{"type":"number"},"wheel_spin_deg":{"type":"number"}},"required":["id"]})",true},
        {"usage_info","Read persisted drawing token receipts and reported lower-bound totals. External MCP consumption may remain unknown; client receipts cover only reported calls. detail receipts includes provenance; no estimate is invented.",R"({"type":"object","properties":{"detail":{"type":"string","enum":["summary","receipts"]}}})",true},
        {"usage_record","Record an externally supplied provider token receipt on this drawing. Stable id deduplicates retries; changed duplicates reject. Metadata serial changes without a geometry revision/recompute. Never invent counts. Source is explicitly client-supplied and does not certify complete external coverage.",R"({"type":"object","properties":{"id":{"type":"string"},"model":{"type":"string"},"input_tokens":{"type":"integer","minimum":0},"output_tokens":{"type":"integer","minimum":0},"total_tokens":{"type":"integer","minimum":0},"cached_input_tokens":{"type":"integer","minimum":0},"reasoning_tokens":{"type":"integer","minimum":0},"reported_responses":{"type":"integer","minimum":0},"missing_responses":{"type":"integer","minimum":0}},"required":["id","input_tokens","output_tokens","total_tokens"]})",false},
        {"sheet_inspect","Read authored sheet panel/opening/hem/stamp inventory, bend table, flat/refold limits. Rigid placements resolve sheet_origin; results and DXF are in original authoring coordinates.",R"({"type":"object","properties":{"id":{"type":["integer","string"]}},"required":["id"]})",true},
        {"export_flat_dxf","Export analytic LINE/ARC/CIRCLE flat-sheet contours and bend lines to DXF in mm from the checked authored native sheet. Does not certify fabrication or forming.",R"({"type":"object","properties":{"id":{"type":["integer","string"]},"path":{"type":"string"}},"required":["id","path"]})",false},
        {"stock_preview","Read a nominal straight-stock preview for tube or construction_frame. Centerline lengths are retained; copes are not represented in uncut stock. Display-only, no geometry edit. mesh:true explicitly requests positions and indices.",R"({"type":"object","properties":{"id":{"type":["integer","string"]},"mesh":{"type":"boolean"}},"required":["id"]})",true},
        {"frame_inspect","Read the evaluated construction frame cutlist, exact member lengths/volumes, joints, named member frames and optional profile-density mass and center. Overlapping members are counted independently.",R"({"type":"object","properties":{"id":{"type":["integer","string"]}},"required":["id"]})",true},
        {"electronics_inspect","Read PCB layers, net connectivity, clearance DRC and BOM, or a cable route's bend radius, exact length and independently checked obstacle clearance. Does not certify electrical or manufacturing performance.",R"({"type":"object","properties":{"id":{"type":["integer","string"]}},"required":["id"]})",true},
        {"pcb_export","Export PCB copper and outline Gerbers, plated/non-plated Excellon drill files and JSON netlist as a fabrication preview. Run electronics_inspect for incomplete nets and DRC scope first.",R"({"type":"object","properties":{"id":{"type":["integer","string"]},"path_prefix":{"type":"string"}},"required":["id","path_prefix"]})",false},
        {"export_iges","Export a selected exact B-rep as IGES in millimetres, with independent readback bounds/area checks and solid-topology status. Default pose formed; flat is available only for sheet features.",R"({"type":"object","properties":{"id":{"type":["integer","string"]},"path":{"type":"string"},"pose":{"type":"string","enum":["flat","formed"]}},"required":["id","path"]})",false},
        {"agent_onboard","START HERE. Discover current document/revision, supported modeling workflows, reusable component library, examples and validation limits. Read-only; choose a topic before modeling.",R"({"type":"object","properties":{"topic":{"type":"string","enum":["bom","fasteners","components","vendors","plastics","motion","manufacturing","physics","automation","overview","sketch","tube","assembly","surfacing","sheet","solids","mesh","materials","frames","electronics","routing","suspension","skeleton","drawings","holes","cad_import","containers","stamps","planes","history","configurations","wood","networks","lighting","hud"]}}})",true},
        {"doc_info","Document summary/inventory. detail design returns feature operations/dependencies/roles, sketch names, origins, manufacturing declarations and measured volume/bounds. ids/sections require detail design or omitted detail (inferred design); explicit summary cannot use either filter. Examples: {detail:summary}, {detail:design,sections:[features,assembly]}. Optional ids selects 1..64 feature ids or unique names (default section features); sections selects features|sketches|parameters|assembly|materials. With ids, sketches/assembly/materials are direct associations; parameters stay document-wide. Selected reads expose partial, scope, section_scopes and total_counts. detail digest returns compact feature/sketch/node rows, parameters and a fingerprints map; send that map back as known_fingerprints to receive only changed rows plus removed keys. No meshes/native payloads; read-only.",R"({"type":"object","anyOf":[{"properties":{"detail":{"enum":["design"]}}},{"not":{"anyOf":[{"required":["ids"]},{"required":["sections"]}]}}],"properties":{"detail":{"type":"string","enum":["summary","design"]},"ids":{"type":"array","minItems":1,"maxItems":64,"items":{"oneOf":[{"type":"integer","minimum":1},{"type":"string","minLength":1,"maxLength":128}]}},"sections":{"type":"array","minItems":1,"maxItems":5,"uniqueItems":true,"items":{"type":"string","enum":["features","sketches","parameters","assembly","materials"]}}}})",true},
        {"doc_new","Start an empty document only when requested. Clears the active model with undo and a monotonically increasing revision; does not overwrite saved files.",R"({"type":"object","properties":{"expected_revision":{"type":"integer","minimum":0}},"required":["expected_revision"]})",false},
        {"doc_get","Read full parametric document, including expressions and revision.",R"({"type":"object"})",true},
        {"object_context","Compact existing-object CAD context and change tokens. Select ids (feature ids/unique names), or node/copy for one part occurrence; otherwise discover by query substring/type and after_id. expected_revision rejects stale reads. depth 0..2 expands dependencies/dependents; limit 1..64 bounds rows. known_fingerprints maps returned document-scoped keys to change tokens; unchanged rows keep identity but omit details. Covers authored recipes, dependency/parameter/sketch/datum resources, connected assembly and available measured evidence. Source/mesh/B-rep buffers stay local; fingerprints are change detection, never identity/security/geometry equivalence. Read-only; partial/truncated and omitted fields are explicit.",R"({"type":"object","properties":{"ids":{"type":"array","minItems":1,"maxItems":64,"items":{"type":["integer","string"]}},"node":{"type":["integer","string"]},"copy":{"type":"integer","minimum":0,"maximum":31},"query":{"type":"string","minLength":1,"maxLength":128},"type":{"type":"string"},"expected_revision":{"type":"integer","minimum":0},"known_fingerprints":{"type":"object","maxProperties":1024,"additionalProperties":{"type":"string","maxLength":64}},"depth":{"type":"integer","minimum":0,"maximum":2},"limit":{"type":"integer","minimum":1,"maximum":64},"after_id":{"type":"integer","minimum":1}},"additionalProperties":false})",true},
        {"doc_apply","Atomic batch; evaluate once, roll back on any failure. BOM: bom.initialize{} assigns persistent document identity; feature.add/edit part:{part_number,description,part_revision,category,procurement:make|buy|unspecified,manufacturer,manufacturer_part_number,supplier,supplier_part_number,source_url,datasheet_url,standard,specification,representation,sourcing_status} replaces declared metadata; null clears it. Requires expected_revision; metadata-only edits do not recompute geometry. Use bom_inspect to check quantities/missing fields. Technical drawings: drawing.set{drawing:{name,feature,units:mm|in,scale,paper_mm:[width,height],views:[{name,type:orthographic|section,direction:front|back|top|bottom|right|left,offset_mm:[x,y],origin_mm:[x,y,z] for section}],dimensions:[{name,view,kind:linear|horizontal|vertical|radius|diameter,reference_a,reference_b? (linear only),fraction_a?,fraction_b?,position_mm:[x,y],tolerance?:{lower,upper,units}}]}}, drawing.refresh{name}, drawing.remove{name}. Requires expected_revision. One feature-local exact solid; changed B-rep invalidates the binding. Radius/diameter use an exact circular axis viewed along that axis. Operations: param.set{name,expr}, material.set{name,density?,appearance?}, material.remove{name}, feature.add{type,name,p,native,inputs,mode,material?,appearance?}, feature.edit{id,p,native,name,material?,appearance?}, feature.remove{id} where id is a number or a unique feature name, plane.set{name,kind?:plane|axis|frame|point,parent,p:{ox,oy,oz,rx,ry,rz},support?:{kind:face|midplane|normal_to_curve|tube_path|on_curve|on_face|offset_plane|angle_plane|plane_three_points|plane_axis_point|plane_point_normal|axis_points|axis_planes|point|between_points,feature,reference,second_feature?,second_reference?,fraction?,station?,u?,v?}}, plane.remove{name}, sketch.set{name,sketch:{plane,tolerance,entities,constraints}}, sketch.entity.set{name,entity}, sketch.patch{name,entity,anchor?,x?,y?,r?,a0?,a1?,segment?,role?}, sketch.remove{name}, sketch.fillet{name,entity,corner,radius}, sketch.chamfer{name,entity,corner,distance}, assembly.add{name,parent,feature,p,visible}, assembly.edit{id,parent,feature,p,visible}, assembly.remove{id}, assembly.drive{driver,mate?,mode?:rotate|translate,delta}. Drive requires expected_revision; delta is degrees for rotate or mm for translate and accepts expressions. It moves the connected side of a grounded joint, retains internal passive coordinates and rechecks every mate; unsupported, ambiguous or unsatisfied motion rejects atomically. Grounded closed linkages and transmission boundaries use measured constrained continuation in the verified domain. id and parent accept a unique node name or a number; parent 0 stays the root. feature=0 is a group; feature and feature inputs accept a unique name or an id, and the saved document stores the id. placements are rigid tx/ty/tz/rx/ry/rz. assembly pattern is type linear or circular with count and dx/dy/dz or step/axis. It repeats one part, a mate moves that pattern rigidly, and degrees of freedom count the node once. Tube feature accepts p{section:round|rect,od,width,height,wall,tail,chord_tol,sides} plus bends:[{feed,radius,angle,roll}] in mm/degrees. A round section is also an exact annular sweep along the centerline, initially +X, and can enter union, cut, common and a concentric axis mate. A rectangular section is an exact hollow sweep along the centerline, initially +X, with width along the normal and height along the binormal, and can enter a boolean. The display mesh remains the viewport tessellation. Sketch entities: kind point|line|circle|spline|arc|wire|recipe (typed recipe source, editable fields; inspect sketch onboarding for defaults), optional unique name, dof numbers or expression strings (strings are driven), construction. Spline dof is 2..64 XY anchor pairs flattened; closed wraps without repeated first point, tension 0..1. Arc entity optionally uses recipe:{type:three_point,start:[x,y],through:[x,y],end:[x,y]} or recipe:{type:tangent,start:[x,y],end:[x,y],tangent:[tx,ty]}; expressions persist and derive driven center/radius/angles, including major/clockwise arcs. Edit the recipe via sketch.entity.set; resolved dof patches reject. Spatial curve arc mode three_point/tangent uses XYZ through/to/tangent; omitted tangent inherits previous exact edge, first requires tangent. Optional normal validates coplanarity; radius arcs allow major:true. Recipes/modes save v30. Constraints val/val2 accept expressions. Entity edits replace one named entity. Mates: revolute aligns origins and +Z, leaving axial rotation; rigid aligns full reference frames and locks all six DOFs (no retain-offset capture). Both save document version29. mate.add{a,b,type,reference_a,reference_b,value}, mate.edit, mate.remove. Mechanical types chain and gear use motion:{teeth_a,teeth_b,phase_deg?}; shaft locks rotational ratio 1:1; rack_pinion uses motion:{pitch_radius,direction?:1|-1}; cam uses harmonic motion:{lift,phase_deg?}. Expressions are persisted in v22. Endpoints are driver a/driven b instance IDs. Mechanical couplings retain independent static geometric positioning; inspect assembly_motion for validated read-only matrices/speeds. Cycles, multiple incoming drivers, fixed/pattern endpoints and incompatible semantic axes reject. Mate ids stay numeric. Pass that id, or omit it and pass endpoints a and b, names or ids, in either order, to select the unique mate. Endpoints a and b accept a unique instance name or an id; the saved mate stores ids. ground instances with assembly.edit{id,fixed:true}. assembly_info candidate probes a mate without saving it. Manufacturing intent: feature.add/edit accepts manufacturing:{route:sheet_metal|machined|frame|purchased|unspecified|injection_molding|additive,process?:object} or null to clear both. This object replaces the full process plan. Injection/additive and nonempty typed process settings save as version23. Call manufacturing onboarding for route-specific field contracts and manufacturing_inspect for exact evidence, blockers and unknowns; declarations do not certify production. Sheet/frame declarations require matching actual authoring lineage, retained by rigid transforms. Declaring purchased identifies a supplier envelope, not supplier validation. Frame endpoint tools: frame.trim_extend{id,joint:{member,end:start|end,against,max_extension}}, frame.trim_plane{id,joint:{member,end,origin,normal}}, frame.joint.remove{id,member,end}, frame.weld.add{id,weld:{id,member_a,member_b,path:[[x,y,z],[x,y,z]],normal:[x,y,z],leg_a,leg_b,material?|density_g_cm3?}}, frame.weld.edit/remove{id,entity,values?}. Exact separate triangular fillet beads are declared geometry; fusion and strength remain unverified. Sheet internal forming: sheet.jog.add{id,jog:{id?,station,height,angle,inner_radius,k_factor?}} and sheet.sketch_bend.add{id,bend:{id?,sketch,entity,angle,inner_radius,k_factor?}}; edit/remove use values. Current domain one original unperforated rectangular blank; full-width XZ sketch line; preserve original flat length. These operations save v25. Sheet edits: sheet.create{name,p:{width,thickness,k_factor},base:{id,length}}, sheet.base.edit{id,values}, sheet.flange.add{id,flange:{id?,parent,edge:u_min|u_max|v_min|v_max,length,width?,offset?,inner_radius,angle,k_factor?}}, sheet.opening.add{id,opening:{id?,panel,kind:circle|slot|window|polygon,x,y,radius,length,width,height,angle,points}}, sheet.opening.pattern{id,opening,count,dx,dy} creates named parametric copies. sheet.flange.edit/sheet.opening.edit{id,entity,values}, sheet.flange.remove/sheet.opening.remove{id,entity,cascade?}. sheet.hem.add{id,hem:{id?,parent,edge,length,inner_radius,direction,width?,offset?,k_factor?}}, sheet.hem.edit/remove{id,entity,values?}. sheet.stamp.add{id,stamp:{id?,panel,kind:circular_emboss,x,y,radius,top_radius,depth}}, sheet.stamp.edit/remove{id,entity,values?}. Hem/stamp and manufacturing intent save as version21. Stamped sheets have an unqualified nominal flat reference; flat STEP/IGES/DXF export rejects. Local expressions: panel_length/panel_width/thickness in openings, parent_length/parent_width/edge_length/thickness in flanges. Native feature types: native_solid{native:{op:box|cylinder|sphere|cone|torus|extrude|union|cut|common|hole|fillet|chamfer|draft|linear_pattern|circular_pattern|mirror|shell|transform,...},inputs,p} exact ops. Fillet/chamfer take native.edges or native.selection sharp|convex|concave|all plus p.min_angle. Draft takes p.angle and native.direction/neutral/faces; freeform faces reject. Patterns take p.count and p.dx/dy/dz or p.step plus native.axis; mirror takes native.plane XY|XZ|YZ or a datum name, or native.face naming one planar face; p.copy_only=1 keeps only the reflected body. split_plane accepts a native.plane or native.face, or native.normal plus optional native.origin, and native.keep positive|negative|both. p.fuse defaults to 0. A Boolean tool may be a compound of those solids. loft{native:{sections:[sketch names],guides?:[1..8 sketch or curve names]},p:{solid,ruled,max_degree?,smooth?,compatible?,continuity?}}. Guides are open curves the sections follow. A one-point sketch is a first or last tip. A smooth loft that fuses sides also lists side.<entity> with status merged and the surviving face; native.faces, native.edges and plane support.reference accept that alias, and the document keeps the authored name. Loft edges are edge.start/end.<entity> and rail.<index>, surface_fill{p:{sketch} or native:{profile}}, bspline_surface{native:{poles,weights?},p:{u_degree,v_degree}}, thicken{inputs:[native surface],p:{thickness}}, sheet_metal{p:{width,leg_a,leg_b,thickness,inner_radius,angle,k_factor},native:{openings?}}. Feature types: sketch_cut{inputs:[base],p:{sketch,depth,offset}}, notched_tube{p:{width_mm,height_mm,wall_mm,length_mm,hinge_offset_mm},notches:[{station,angle,face:bottom|top|left|right}]}, tube, box{dx,dy,dz}, cylinder{r,h}, sphere{r}, cone{rb,rt,h}, torus{R,r}, extrude{shape,w,d,height,sketch}, revolve{shape,w,d,angle,sketch}, transform{tx,ty,tz,rx,ry,rz}, boolean{mode:0 union/1 cut/2 intersect}, pattern{count,dx,dy,dz}, surface{x,y,z,u0,u1,v0,v1,nu,nv,wrap_u,wrap_v}. Geometry parameters belong in p and accept math expressions. Transforms and solid patterns take one input. native_solid union, cut or common accepts 2..16 feature inputs and/or native.instances of placed parts, including every pattern copy, and returns one exact solid. Two-input booleans keep the previous document version. Instance patterns, native.instances, and unions of more than two exact solids save as document version 10. shell, with signed p.thickness and optional native.faces to open, and a loft with native.guides, save as document version 11. A point datum, on_curve or on_face support, a sketch arc or wire, a tangent constraint, and curve, intersection_curve or bridge save as document version 12. feature.edit accepts suppressed: a true value leaves that feature out of recomputation and keeps its history and parameters. Sketch kinds also include project{feature,reference,mode:normal,source_instance?,source_copy?} and offset{source,distance}. Normal projection is an exact associative B-rep; derived display samples are not profile authority. Alternatively project{source_sketch,source_entity,mode:normal} selects one earlier named sketch entity without feature/reference; sources are exclusive. type curve native:{op:project,mode:3d,source_sketch,source_entity} needs no feature inputs. Source-sketch self/future references and cycles reject. Native edge or one planar face boundary sources also supported; curved faces, holes/multiple wires and collapsed geometry reject. Source instance/copy selects solved world placement. Projected entities remain driven; edit their source. type curve with inputs:[source] and native:{op:project,mode:3d,reference,source_instance?,source_copy?} includes the exact spatial edge/wire preserving XYZ, usable as sweep path/loft guide. This is a spatial curve feature, not a general 3D constraint solver. New projection modes/instance fields save version 28. feature is an id or a unique name bound to an id at apply. distance is signed millimetres, positive to the left of the directed curve. Project and offset entities are driven, and sketch.patch rejects them. canvas.set{name,path,plane?,width,height,opacity?} and canvas.remove{name} store a photo rectangle on a plane. The kernel does not decode the image. A plane canvas, a suppressed feature, or a projected or offset sketch entity saves as document version 13. sketch.fillet and sketch.chamfer trim one wire corner; the sharp wire stays the authority and the size stays an expression. A mate across assembly groups moves each immediate parent group and its descendants as one rigid body. Either of those saves document version 14. p.density in grams per cubic centimetre overrides a named material density. material.set assigns density 0..100 g/cm3 (0 means unspecified) and appearance {base_color:[sRGB display values 0..1], metallic:0..1, roughness:0..1, opacity:0..1, texture_path:string, texture_scale:[positive U,V], pattern:twill|weave|carbon|leather|brushed|knurl|perforated|none, pattern_scale_mm, pattern_angle_deg, pattern_contrast, pattern_color (044B display-only procedural patterns, version 47)}; feature appearance is a partial override. Named materials and appearance save as version 15. split_plane and mirror copy_only also save as version 15. An appearance-only batch reports geometry_recomputed:false and retains evaluated geometry. A loft section accepts a point datum. A loft guide and a sweep path accept a curve feature name. Engineering modules construction_frame, pcb and cable_route store editable native programs and save version 16. Call agent_onboard frames or electronics for their contracts. Use expected_revision to reject stale writes. Angles in degrees except trig expressions in radians. Surface coordinates use u,v; periodic seams are checked. Exact surfaces also include sweep{native:{section,path} sketch names or profiles,p:{solid,correction,contact}}, extrude_surface{p:{sketch,distance,solid} or native.profile}, revolve_surface{p:{sketch,angle,solid},native:{axis:X|Y|Z default Y}}, offset_surface{inputs:[one face or shell],p:{distance}}, and sew{inputs:[2..16 faces or shells],p:{solid}}. Mesh features are sampled authority and never become a B-rep: mesh_body{mesh:{positions,indices}}, mesh_weld{inputs,p:{epsilon}}, mesh_smooth{inputs,p:{iterations,lambda,pin_boundary}}, mesh_decimate{inputs,p:{ratio}}, mesh_offset{inputs,p:{distance}}, mesh_fill{inputs}; mesh_edit{inputs,native:{op:displace|extrude,vertices?|triangles?,source_topology},p:{dx,dy,dz}}. Read mesh_inspect.topology_fingerprint before indexed edits. skeleton.set{source} persists native input/relation/array/pattern code; skeleton_compile previews relations and doc_preview validates geometry. An exact B-rep input needs p.sample=1, which opts into the display tessellation. import_mesh reads an STL or OBJ snapshot under the triangle cap.",R"({"type":"object","properties":{"expected_revision":{"type":"integer","minimum":0},"commands":{"type":"array","minItems":1,"maxItems":128,"items":{"type":"object","properties":{"op":{"type":"string","enum":["drawing.set","drawing.refresh","drawing.remove","param.set","material.set","material.remove","feature.add","feature.edit","feature.remove","sketch.set","sketch.entity.set","sketch.patch","sketch.remove","sketch.fillet","sketch.chamfer","canvas.set","canvas.remove","skeleton.set","skeleton.remove","plane.set","plane.remove","assembly.add","assembly.edit","assembly.remove","assembly.drive","mate.add","mate.edit","mate.remove","sheet.create","sheet.base.edit","sheet.flange.add","sheet.flange.edit","sheet.flange.remove","sheet.opening.add","sheet.opening.pattern","sheet.opening.edit","sheet.opening.remove","sheet.hem.add","sheet.hem.edit","sheet.hem.remove","sheet.stamp.add","sheet.stamp.edit","sheet.stamp.remove","frame.trim_extend","frame.trim_plane","frame.joint.remove","frame.weld.add","frame.weld.edit","frame.weld.remove","sheet.jog.add","sheet.jog.edit","sheet.jog.remove","sheet.sketch_bend.add","sheet.sketch_bend.edit","sheet.sketch_bend.remove"]},"entity":{"oneOf":[{"type":"string"},{"type":"object","properties":{"source_sketch":{"type":"string","minLength":1,"maxLength":128},"source_entity":{"type":"string","minLength":1,"maxLength":128},"source_instance":{"type":"integer","minimum":1},"source_copy":{"type":"integer","minimum":0,"maximum":31},"reference":{"type":"string","minLength":1,"maxLength":128},"kind":{"type":"string","enum":["point","line","circle","spline","arc","wire","project","offset"]},"name":{"type":"string","minLength":1,"maxLength":128},"feature":{"oneOf":[{"type":"integer","minimum":1},{"type":"string","minLength":1,"maxLength":128}]},"mode":{"type":"string","enum":["normal"]},"dof":{"type":"array","items":{"oneOf":[{"type":"number"},{"type":"string"}]}},"construction":{"type":"boolean"},"recipe":{"type":"object","properties":{"type":{"type":"string","enum":["three_point","tangent"]},"start":{"type":"array","minItems":2,"maxItems":2,"items":{"type":["number","string"]}},"through":{"type":"array","minItems":2,"maxItems":2,"items":{"type":["number","string"]}},"end":{"type":"array","minItems":2,"maxItems":2,"items":{"type":["number","string"]}},"tangent":{"type":"array","minItems":2,"maxItems":2,"items":{"type":["number","string"]}}},"required":["type","start","end"],"additionalProperties":false}}}]},"native":{"type":"object","properties":{"source_sketch":{"type":"string","minLength":1,"maxLength":128},"source_entity":{"type":"string","minLength":1,"maxLength":128},"source_instance":{"type":"integer","minimum":1},"source_copy":{"type":"integer","minimum":0,"maximum":31},"reference":{"type":"string","minLength":1,"maxLength":128},"op":{"type":"string"},"mode":{"type":"string"}}}},"required":["op"]}}},"required":["commands"]})",false},
        {"plane_inspect","Read a named construction datum world transform. Built-ins: Origin (point at world zero), XY, XZ (normal -Y), YZ (normal +X). Origin stays kind point. Offsets/XYZ Euler rotations are relative to parent.",R"({"type":"object","properties":{"name":{"type":"string"}},"required":["name"]})",true},
        {"sketch_inspect","Read resolved sketch coordinates, local constraint rank/remaining DOFs, profile readiness and sampling counts. Optional samples:true includes local/world polylines. Optional entity returns that entity's length, bbox and end tangents without every coordinate. Spline chord tolerance bounds curve approximation, not manufacturing accuracy.",R"({"type":"object","properties":{"name":{"type":"string"},"samples":{"type":"boolean"},"entity":{"type":"string"}},"required":["name"]})",true},
        {"sketch_diagnose","Diagnose a named sketch or an uncommitted sketch candidate. Evaluates dimensions, attempts solve, reports each constraint residual and local rank/redundancy even if inconsistent. Does not mutate the document; no minimal conflict-set claim.",R"({"type":"object","properties":{"name":{"type":"string"},"sketch":{"type":"object"}},"oneOf":[{"required":["name"]},{"required":["sketch"]}]})",true},
        {"reference_inspect","Discover exact producer semantic face/edge/axis names and resolve a selected reference frame. Derived B-rep cache may be materialized from analytic parameters; document/revision unchanged. Split, deleted, ambiguous or stale references fail; numeric topology indices are not persistent IDs. A smooth loft that fuses authored side.<entity>.<index> faces also lists side.<entity> with status merged, the surviving face and merged_from. native.faces, native.edges and plane support.reference accept that alias. sketch lists point.<name> origins. find returns the unique planar face whose normal matches the named axis.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"sketch":{"type":"string"},"reference":{"type":"string"},"fraction":{"type":"number","minimum":0,"maximum":1},"find":{"type":"object","properties":{"kind":{"type":"string","enum":["planar"]},"normal":{"type":"string","enum":["X","Y","Z","-X","-Y","-Z"]}},"required":["kind","normal"]}},"anyOf":[{"required":["id"]},{"required":["sketch"]}]})",true},
        {"sheet_preview","Read sequential flat-to-formed panel transforms for an authored sheet at progress 0..1. Display only; exact exports use explicit flat/formed B-reps. No trajectory or press-tool collision certification.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"progress":{"type":"number","minimum":0,"maximum":1}},"required":["id","progress"]})",true},
        {"doc_undo","Undo the previous committed transaction; revision increases.",R"({"type":"object"})",false},
        {"doc_redo","Redo a transaction after undo.",R"({"type":"object"})",false},
        {"eval_expr","Evaluate finite arithmetic and trigonometry with named parameters; trig uses radians, deg() converts degrees.",R"({"type":"object","properties":{"expr":{"type":"string"}},"required":["expr"]})",true},
        {"sketch_repair","Propose bounded single-constraint deletions for a named sketch or supplied candidate. Read-only, revision- and source-hash-bound; preserves other authored expressions/projections. Review one alternative, doc_preview its commands and explicitly doc_apply the prepared candidate. No automatic deletion or global conflict-set claim.",R"({"type":"object","properties":{"name":{"type":"string","minLength":1},"sketch":{"type":"object"},"expected_revision":{"type":"integer","minimum":0}},"required":["name","expected_revision"],"additionalProperties":false})",true},
        {"feature_history","Read retained feature order, direct dependencies, suppression and inclusive rollback tip. Author feature.move or feature.rollback with the observed expected_revision; invalid dependency changes reject atomically.",R"({"type":"object","properties":{},"additionalProperties":false})",true},
        {"assembly_authoring_info","Inspect named configurations, selected loading roots and pinned imported source references at the current revision. Source drift rejects instead of substituting geometry.",R"({"type":"object","properties":{},"additionalProperties":false})",true},
        {"skeleton_inspect","Inspect native Skeleton code, editable inputs, source-owned relations/vector arrays and stable pattern ownership. Read-only; lengths mm.",R"({"type":"object","properties":{"expected_revision":{"type":"integer","minimum":0}},"additionalProperties":false})",true},
        {"drawing_inspect","List authored technical drawings, or inspect one name with exact orthographic HLR/section curves and semantic dimensions. Changed source B-reps return stale and omit current measurements; missing references/source return invalid. Use explicit revision-checked drawing.refresh before export. Feature definitions, exact occurrence/copy or root/group assemblies. Empty args returns a passive source catalog with editable suggested_drawing recipes; users must request drawings before agents author them. Original templates, tolerances, exact sections and isometric views share Studio CAD viewport. SVG is display sampling, JSON retains exact derived curves.",R"({"type":"object","properties":{"name":{"type":"string","minLength":1,"maxLength":128},"expected_revision":{"type":"integer","minimum":0}},"additionalProperties":false})",true},
        {"drawing_export","Atomically publish a new local json, svg or dxf technical drawing at expected_revision. Requires a current exact source/occurrence binding and resolved dimensions. Only export when the user requests it. JSON retains derived exact B-rep curves, samples and measurements; SVG is a display preview with dimension/tolerance labels; DXF preserves exact projected curves and section hatches with source metadata and local readback checks. CAD associativity and drafting qualification are not claimed. Existing destinations and stale/invalid drawings reject.",R"({"type":"object","properties":{"name":{"type":"string","minLength":1,"maxLength":128},"expected_revision":{"type":"integer","minimum":0},"path":{"type":"string","minLength":1,"maxLength":4096},"format":{"enum":["json","svg","dxf"]}},"required":["name","expected_revision","path","format"],"additionalProperties":false})",false},
        {"skeleton_compile","Read-only native declarative Skeleton syntax/relation preview. Declarations: input name = expression; relation name = expression; array name[count] = [x,y,z] using index; pattern name from feature count expression step [x,y,z]. No arbitrary code execution. Use doc_preview/doc_apply skeleton.set for geometry-checked atomic changes.",R"({"type":"object","properties":{"source":{"type":"string","maxLength":65536},"expected_revision":{"type":"integer","minimum":0}},"required":["source"],"additionalProperties":false})",true},
        {"collision_inspect","Read-only collision feedback for two feature definitions or placed assembly occurrences. Exact single solids return contact, distance and intersection volume; triangle meshes return real boundary contacts and bounded containment evidence with explicit unknowns. Not dynamics or manufacturing qualification. mode exact rejects mesh inputs; auto uses existing native authority when both operands have B-reps. a/b are id/name or {id} / {node,copy}. Same operand rejects.",R"({"type":"object","properties":{"a":{"type":["integer","string","object"]},"b":{"type":["integer","string","object"]},"mode":{"type":"string","enum":["auto","exact","mesh"]},"expected_revision":{"type":"integer","minimum":0}},"required":["a","b"],"additionalProperties":false})",true},
        {"mesh_inspect","Compact indexed topology audit: bounds, area, edge counts, components and signed volume for closed oriented edges. Does not certify solids or detect self intersections. Omit id to inspect final outputs only. node accepts a unique instance name or an id, and copy frames one pattern placement.",R"({"type":"object","properties":{"node":{"anyOf":[{"type":"integer","minimum":1},{"type":"string"}]},"copy":{"type":"integer","minimum":0,"maximum":31},"id":{"type":"integer","minimum":1}}})",true},
        {"mesh_get","Explicitly retrieve mesh positions and triangle indices; potentially large. Prefer mesh_inspect for reasoning. node accepts a unique instance name or an id, and copy frames one pattern placement.",R"({"type":"object","properties":{"node":{"anyOf":[{"type":"integer","minimum":1},{"type":"string"}]},"copy":{"type":"integer","minimum":0,"maximum":31},"id":{"type":"integer","minimum":1}}})",true},
        {"mesh_raycast","Nearest triangle hit using a cached AABB hierarchy. Direction is normalized; distances in mm. Triangle IDs last for this tessellation/revision only.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"origin":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"direction":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["origin","direction"]})",true},
        {"assembly_motion","Read-only ideal coupled motion from the solved static assembly. Chain/gear teeth expressions, shaft rotation, rack displacement and harmonic cam followers use semantic instance axes. Returns all instance matrices, speeds and coupling/static constraint residuals. Does not edit pose/revision or certify tooth contact, chain collision or dynamics.",R"({"type":"object","properties":{"driver":{"type":["integer","string"]},"speed_rpm":{"type":["number","string"]},"time_s":{"type":"number","minimum":0,"maximum":3600},"angle_deg":{"type":"number"}},"required":["driver","speed_rpm","time_s"]})",true},
        {"assembly_info","Hierarchy placements, pattern copies and BOM quantities by shared feature definition. Each instance includes copy. Hidden instances remain in BOM. A patterned node counts once for degrees of freedom. Optional drive:{driver,mate?,mode?:rotate|translate,delta:number|expression} previews mate-aware connected-component manipulation. Returns drive.joint, delta, instances, diagnostics and assembly.edit commands; document/revision unchanged. Commit assembly.drive through doc_apply with expected_revision only for a pose the design keeps; temporary checks stay in this preview or doc_preview + snapshot{candidate_id}. Revolute leaves axial rotation, rigid locks the full frame. Closed loops, ambiguous joints, grounded moving sides and invalid residuals reject. Optional candidate probes one mate and reports residuals, the two frames and the axis convention without saving it or changing revision. interference compares display meshes: the default budget is 8000 triangles and interference:true raises it to 120000. A skip sets interference_checked false. Overlap is display mesh evidence for a cheap screen, not a motion study. clearance:{a,b} is exact separation and overlap evidence between B-reps. separation_mm is the nonnegative gap; overlap_volume_mm3 reports measured intersection. Legacy clearance_mm negative values are penetration estimates, not minimum translation distances; penetration_depth_checked is false. Both faces and the method are reported. a and b take a name, an id, or {node,copy}; copy_a and copy_b also select a pattern copy. A body without a B-rep sets clearance_checked false and clearance_reason. measure:{a,b} is read-only and returns distance_mm, angle_deg and both frames for axis, planar face or origin references. Neither field writes the document or the revision. For visible placed parts with material density or p.density override, exact B-rep volume yields mass_g, mass_kg, center_mm, weight_N, gravity_m_s2, mass_scope and mass_checked. Hidden parts are excluded by the reported scope. A missing or invalid density sets mass_checked false and does not return a partial mass. Concentric, coincident/contact, signed distance and angle mates, grounded instances, local DOFs and residual diagnosis.",R"({"type":"object","properties":{"gravity_m_s2":{"type":"number","minimum":0,"maximum":100},"interference":{"type":"boolean"},"drive":{"type":"object","properties":{"driver":{"type":["integer","string"]},"mate":{"type":"integer","minimum":1},"mode":{"type":"string","enum":["rotate","translate"]},"delta":{"type":["number","string"]}},"required":["driver","delta"],"additionalProperties":false},"candidate":{"type":"object"},"clearance":{"type":"object"},"measure":{"type":"object"}}})",true},
        {"tube_inspect","Ideal centerline length, section area and bend table. No springback, weld allowance or material compensation.",R"({"type":"object","properties":{"node":{"type":"integer","minimum":1},"id":{"type":"integer","minimum":1}},"required":["id"]})",true},
        {"source_inspect","Read source projection evidence and exact opening coordinates without large mesh buffers. Optional progress returns finite-hinge display transforms; source geometry stays read-only.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"progress":{"type":"number","minimum":0,"maximum":1}},"required":["id"]})",true},
        {"native_inspect","Read native feature validation, opening inventory, and flat availability without transmitting B-rep or mesh buffers. When a named material density or p.density override is set, exact B-rep volume yields mass_g, mass_kg, weight_N, gravity_m_s2 and the local center_mm. Optional point returns the signed distance in mm, negative inside one solid, and the nearest face name.",R"({"type":"object","properties":{"gravity_m_s2":{"type":"number","minimum":0,"maximum":100},"id":{"oneOf":[{"type":"integer","minimum":1},{"type":"string","minLength":1,"maxLength":128}],"description":"feature id or unique feature name"},"point":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["id"]})",true},
        {"kernel_info","Report the isolated native OCCT modeling backend and version.",R"({"type":"object"})",true},
        {"step_inspect","Read STEP solids, units, B-rep validity, dimensions, volume and surface types before choosing an explicit body. Optional faces includes source face inventory. Read-only C++ OCCT; no automatic sheet classification.",R"({"type":"object","properties":{"path":{"type":"string"},"faces":{"type":"boolean"}},"required":["path"]})",true},
        {"import_step","Import and geometrically verify a planar tube STEP with cylindrical retained-wall hinges. Preserves exact hole/cutout B-reps, projects rigid spans, checks refolding and imports native preview. Optional replace:true starts a new document. Fails closed outside supported domain.",R"({"type":"object","properties":{"path":{"type":"string"},"replace":{"type":"boolean"}},"required":["path"]})",false},
        {"import_sheet_step","Import a source-owned supported sheet body through the isolated C++ OCCT worker. k_factor is explicit; body_index selects a zero-based solid occurrence and root_panel resolves a panel tie. Source geometry stays read-only.",R"({"type":"object","properties":{"path":{"type":"string"},"k_factor":{"type":"number","minimum":0,"maximum":1},"body_index":{"type":"integer","minimum":0},"root_panel":{"type":"integer","minimum":0,"maximum":1},"replace":{"type":"boolean"}},"required":["path","k_factor"]})",false},
        {"import_mesh","Import an STL or OBJ file as a mesh_body snapshot. Optional weld_mm merges vertices. The file is not linked: later edits of the file do not change the document. Caps are 12288 vertices and 4096 triangles. The result is mesh authority, not an exact B-rep.",R"({"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"},"weld_mm":{"type":"number","exclusiveMinimum":0,"maximum":1},"expected_revision":{"type":"integer","minimum":0}},"required":["path"]})",false},
        {"export_step","Export an exact native B-rep to STEP with readback validation. Sheet features default to the flat pose; pose:formed exports the formed source. A round tube exports its exact centerline sweep. A rectangular tube exports its exact hollow sweep. Existing notched and source tube exports keep their original straight behavior.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"path":{"type":"string"},"pose":{"type":"string","enum":["flat","formed"]}},"required":["id","path"]})",false},
        {"notch_inspect","Read a notched_tube feature's straight request and ideal cut plan. Optional progress returns sequential display matrices and bounds without document mutation.",R"({"type":"object","properties":{"progress":{"type":"number","minimum":0,"maximum":1},"id":{"type":"integer","minimum":1}},"required":["id"]})",true},
        {"notch_plan","Ideal preview of a straight hollow rectangular tube with V cuts leaving one wall. Requires explicit hinge offset inside that wall. Not a source reconstruction or production-approved laser program. Returns cutter polygons, analytic removed volume and cut-face closure proof; finite-wall strain is not validated.",R"({"type":"object","properties":{"width_mm":{"type":"number"},"height_mm":{"type":"number"},"wall_mm":{"type":"number"},"length_mm":{"type":"number"},"hinge_offset_mm":{"type":"number"},"notches":{"type":"array","items":{"type":"object","properties":{"station_mm":{"type":"number"},"angle_deg":{"type":"number"},"hinge_face":{"type":"string","enum":["bottom","top","left","right"]}},"required":["station_mm","angle_deg","hinge_face"]}}},"required":["width_mm","height_mm","wall_mm","length_mm","hinge_offset_mm","notches"]})",true},
        {"compute_info","Actual CUDA availability, selected device, transfer-inclusive last execution and CPU fallback reason.",R"({"type":"object"})",true},
        {"compute_benchmark","Bounded CPU/CUDA benchmark (default 1M vertices) with transfer, synchronization and numerical parity; initializes GPU runtime.",R"({"type":"object","properties":{"vertices":{"type":"integer","minimum":1,"maximum":4000000}}})",true},
        {"doc_save","Save .ark as an incremental binary container with persistent iterations; .arkjson/.json explicitly export versioned JSON.",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})",false},
        {"doc_load","Load .ark/.arkjson into this session, validate and commit atomically.",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})",false},
        {"export_mesh","Export final output bodies or a selected id as STL/OBJ/ASCII FBX; construction inputs are excluded.",R"({"type":"object","properties":{"path":{"type":"string"},"format":{"type":"string","enum":["stl","obj","fbx"]},"id":{"type":"integer","minimum":1}},"required":["path","format"]})",false},
        {"snapshot","Render selected CAD display meshes with revision and feature/occurrence identity. Presets iso/front/right/top use Z-up; front views from -Y. For a custom view, camera.direction points from target toward eye, camera.up fixes screen vertical; supply both with view custom or omit view. Returns actual inline PNG pixels (1024x640) by default; inline_image:false writes 1280x800 and requires path. node accepts a unique instance name or id; copy selects a pattern placement. Image textures (triplanar), procedural appearance patterns and surface stamps are drawn; media_time_s selects the GIF/video frame. Images are display evidence, not B-rep or manufacturing authority.",R"({"type":"object","properties":{"path":{"type":"string"},"id":{"type":"integer","minimum":1},"node":{"type":["integer","string"]},"copy":{"type":"integer","minimum":0},"view":{"type":"string","enum":["iso","front","right","top","custom","viewport"]},"camera":{"type":"object","properties":{"direction":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"up":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"target_mm":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"distance_mm":{"type":"number","exclusiveMinimum":0},"fov_deg":{"type":"number","minimum":5,"maximum":120}},"required":["direction","up"],"additionalProperties":false},"inline_image":{"type":"boolean"},"candidate_id":{"type":"string","description":"Read-only retained prepared candidate; requires expected_revision. Preview pixels are not committed."},"expected_revision":{"type":"integer","minimum":0},"label":{"type":"string","maxLength":128},"stage":{"type":"string","maxLength":128},"reference":{"type":"string","maxLength":128},"media_time_s":{"type":"number","minimum":0,"maximum":86400,"description":"Clip time for animated GIF/video stamps and textures (default 0)."}}})",false}
    };
    V list=V::makeArr();for(auto& d:defs){V t=V::makeObj(),schema,annotations=V::makeObj();t.set("name",V::makeStr(d.name));t.set("description",V::makeStr(!detailed&&std::string(d.name)=="doc_apply"?"Atomic batch of 1..128 modeling commands. Core forms: param.set{name,expr}; feature.add{type,name,p,inputs?,native?}; feature.edit{id,p?,native?,name?}; feature.remove{id}. Target id is a positive number or unique name; p maps actual authored parameter keys to numbers/expression strings. Read object_context for the target before editing. Native box keys are width/depth/height; legacy box keys are dx/dy/dz. Native box edit example: {op:feature.edit,id:Native block,p:{height:9}}. Pass expected_revision; stale revisions and failed evaluation leave the document unchanged. Discover operation contracts and examples with agent_onboard for your domain; agent_onboard{topic:automation} supplies the full command reference. Supports parameters, materials/transparency, features, sketches, datums, assemblies, mates, mate-aware assembly.drive and sheet edits. Inspect assembly_info.drive before committing a joint movement. doc_preview evaluates and caches a candidate; doc_apply{candidate_id,expected_revision} commits without recomputation. Optional checks:[{id?,query:{tool,args,path},op:ge|le|eq|between|true|false,value?,min?,max?,tolerance?,parameters?,diagnose?}] must all pass before commit; failures roll back and return measured residuals. diagnose adds bounded local sensitivity probes. Material density accepts g/cm3 or density_kg_m3, plus physical SI elastic properties. Angles are degrees except trig expressions in radians; lengths mm.":d.desc));arkjson::parse(d.schema,schema);if(std::string(d.name)=="mesh_raycast"||std::string(d.name)=="export_mesh"||std::string(d.name)=="snapshot"){
        V props=schema.find("properties")?*schema.find("properties"):V::makeObj(),node=V::makeObj(),kinds=V::makeArr(),integer=V::makeObj(),text=V::makeObj(),copy=V::makeObj();integer.set("type",V::makeStr("integer"));integer.set("minimum",V::makeNum(1));text.set("type",V::makeStr("string"));kinds.arr.push_back(integer);kinds.arr.push_back(text);node.set("anyOf",kinds);copy.set("type",V::makeStr("integer"));copy.set("minimum",V::makeNum(0));copy.set("maximum",V::makeNum(31));props.set("node",node);props.set("copy",copy);schema.set("properties",props);
    }
        if(std::string(d.name)=="native_inspect"){V props=*schema.find("properties"),detail;arkjson::parse(R"({"type":"string","enum":["full","report","properties"],"description":"properties projects current native validity, volume/area/bounds and authority flags; operation diagnostics/openings/topology omitted. report retains operation diagnostics; full includes topology/panels/bends."})",detail);props.set("detail",detail);schema.set("properties",props);}
        if(std::string(d.name)=="doc_info"){V props=*schema.find("properties"),detail,known;arkjson::parse(R"({"type":"string","enum":["summary","design","revision","digest"],"description":"revision returns current revision/status without any object inventory; summary preserves the legacy catalog; design supports ids/sections; digest returns compact per-revision rows with fingerprints."})",detail);props.set("detail",detail);
            arkjson::parse(R"({"type":"object","maxProperties":4096,"additionalProperties":{"type":"string","maxLength":64},"description":"detail digest only: the fingerprints map from an earlier digest. Unchanged rows are then omitted and removed keys listed."})",known);props.set("known_fingerprints",known);schema.set("properties",props);}
        if(std::string(d.name)=="agent_onboard"){V props=*schema.find("properties"),detail;arkjson::parse(R"({"type":"string","enum":["full","guide"],"description":"guide returns reusable static guidance without document state; full preserves document inventory. Prefer guide plus doc_info detail revision and selected object_context."})",detail);props.set("detail",detail);schema.set("properties",props);}
        if(std::string(d.name)=="assembly_info"){V props=*schema.find("properties"),detail;arkjson::parse(R"({"type":"string","enum":["full","result"],"description":"result requires exactly one measure/clearance/drive/sweep and returns its evidence plus revision, without document inventory/BOM/mass/interference. full/default preserves legacy inspection."})",detail);props.set("detail",detail);schema.set("properties",props);}
if(std::string(d.name)=="doc_apply"){
        {V props=*schema.find("properties"),detail;arkjson::parse(R"({"type":"string","enum":["full","design","changes"],"description":"changes returns revision/evaluation/created-ID/check receipts without a document inventory; full/default and design retain the legacy response."})",detail);props.set("detail",detail);schema.set("properties",props);}
        if(detailed)t.set("description",V::makeStr(t.find("description")->str+" Joint limits: prismatic (alias slider) aligns axes and locks relative orientation, leaving axial translation. Revolute/prismatic accept limits:{min:number|expression,max:number|expression}; limits:null clears. Bounds are signed B-relative-A coordinates in deg/mm, revolute spans must be less than 360 degrees. Drive rejects limits crossed along the full requested movement. These additions save document version31. assembly_info.drive.joint reports position/unit/limited/min/max/coordinate_sign."));
        V commands=*schema.find("properties")->find("commands"),command=*commands.find("items"),commandProps=*command.find("properties"),limits;
        // Expose the actual common command fields at discovery. Detailed native,
        // sheet and frame programs remain in domain onboarding; dispatch still
        // owns operation-specific required fields and semantic validation.
        V coreFields;arkjson::parse(R"({
            "id":{"type":["integer","string","null"],"description":"Required target for feature.edit/remove and assembly.edit/remove: positive id or unique name. feature.rollback accepts null to resume all history. Mate ids are numeric; mate endpoints a/b may select a unique mate instead."},
            "type":{"type":"string","description":"Feature type on feature.add; mate type on mate.add/edit."},
            "name":{"type":"string","minLength":1,"description":"Feature/instance label, parameter name, sketch name or datum name for the selected operation."},
            "p":{"type":"object","additionalProperties":{"type":["number","string"]},"description":"Editable parameters, each a finite number or expression string. Use actual authored keys from object_context: native_solid box uses width/depth/height; legacy box uses dx/dy/dz. feature.edit uses id plus p, not entity. Rigid placements use tx/ty/tz/rx/ry/rz."},
            "inputs":{"type":"array","items":{"type":["integer","string"]},"description":"Feature inputs as positive ids or unique feature names, in operation order."},
            "expr":{"type":["number","string"],"description":"param.set value or one sketch.patch coordinate expression."},
            "sketch":{"type":"object","properties":{"plane":{"type":"string"},"tolerance":{"type":"number"},"entities":{"type":"array","items":{"type":"object"}},"constraints":{"type":"array","items":{"type":"object"}}},"description":"sketch.set owns the complete entities/constraints object; use sketch onboarding for exact entity contracts."},
            "plane":{"type":"string"},"kind":{"type":"string"},"source":{"type":"string","maxLength":65536},
            "parent":{"type":["integer","string"],"description":"Assembly parent id or unique node name; 0 is root. Datum parent is a plane name."},
            "feature":{"type":["integer","string"],"description":"Assembly feature id or unique feature name; 0 creates an assembly group."},
            "a":{"type":["integer","string"],"description":"Mate endpoint instance id or unique instance name."},
            "b":{"type":["integer","string"],"description":"Mate endpoint instance id or unique instance name."},
            "reference_a":{"type":"string"},"reference_b":{"type":"string"},
            "fraction_a":{"type":"number"},"fraction_b":{"type":"number"},
            "value":{"type":["number","string"]},"motion":{"type":"object"},
            "driver":{"type":["integer","string"]},"mate":{"type":"integer","minimum":1},
            "mode":{"type":["integer","string"],"description":"feature.add uses integer 0..2 for Boolean mode; assembly.drive uses rotate or translate."},
            "delta":{"type":["number","string"],"description":"assembly.drive delta in degrees for rotate or mm for translate; requires inspected expected_revision."},
            "support":{"type":"object"},"reference":{"type":"string"},
            "visible":{"type":"boolean"},"fixed":{"type":"boolean"},"suppressed":{"type":"boolean"},
            "material":{"type":["string","null"]},"appearance":{"type":["object","null"]},"manufacturing":{"type":["object","null"]},
            "rows":{"type":"integer","minimum":1,"maximum":128},"columns":{"type":"integer","minimum":1,"maximum":128},"pitch_x":{"type":["number","string"]},"pitch_y":{"type":["number","string"]},"panel":{"type":"string"},"opening":{"type":"object"},
            "density":{"type":"number"},"density_kg_m3":{"type":"number"},"physical":{"type":"object"},
            "pattern":{"type":["object","null"]},"bends":{"type":"array","items":{"type":"object"}},"notches":{"type":"array","items":{"type":"object"}},"mesh":{"type":"object"},
            "anchor":{"type":"integer","minimum":0},"corner":{"type":"integer","minimum":0},"segment":{"type":"integer","minimum":0},"role":{"type":"string"},
            "x":{"type":["number","string"]},"y":{"type":["number","string"]},"r":{"type":["number","string"]},"a0":{"type":["number","string"]},"a1":{"type":["number","string"]},
            "radius":{"type":["number","string"]},"distance":{"type":["number","string"]},"width":{"type":["number","string"]},"height":{"type":["number","string"]},"opacity":{"type":"number"},"path":{"type":"string"}
        })",coreFields);for(const auto& entry:coreFields.obj)commandProps.set(entry.first,entry.second);
        {V operation=*commandProps.find("op"),choices=*operation.find("enum");for(const char* op:{"feature.move","feature.rollback","configuration.set","configuration.apply","configuration.remove","assembly.load","assembly.external.set","assembly.external.remove","motion.graph.set","motion.bake","motion.rename","motion.remove"})choices.arr.push_back(V::makeStr(op));operation.set("enum",choices);commandProps.set("op",operation);
            V fields;arkjson::parse(R"({"parameters":{"type":"object","additionalProperties":{"type":["number","string"]}},"visibility":{"type":"object","additionalProperties":{"type":"boolean"}},"roots":{"type":["array","null"],"items":{"type":"integer","minimum":1},"uniqueItems":true},"source_sha256":{"type":"string","pattern":"^[0-9a-f]{64}$"},"source_revision":{"type":"string","minLength":1},"graph":{"type":"object","description":"motion.graph.set vector motion graph {channels:[{name,mate,from?,curve|expr}],duration_s?,allow_jumps?}; agent_onboard motion has the node grammar."},"fps":{"type":"integer","minimum":1,"maximum":240},"new_name":{"type":"string","minLength":1,"maxLength":128}})",fields);for(const auto& entry:fields.obj)commandProps.set(entry.first,entry.second);}
        commandProps.set("drawing",drawingAuthoringSchema());
        {V pattern;arkjson::parse(R"({"oneOf":[{"type":"null"},{"type":"object","properties":{"type":{"type":"string","enum":["linear","circular","pattern_driven","sketch_driven","curve_driven","chain"]},"count":{"type":["number","string"],"description":"2..32 for curve/chain; pattern/sketch may infer stable slots."},"dx":{"type":["number","string"]},"dy":{"type":["number","string"]},"dz":{"type":["number","string"]},"step":{"type":["number","string"]},"axis":{"type":"string","enum":["X","Y","Z"]},"skips":{"type":"array","items":{"type":"integer","minimum":1,"maximum":31},"uniqueItems":true,"maxItems":31},"source":{"type":"object","description":"Bound source recipe. Onboard assembly for actual points, guide, inheritance and chain contracts.","properties":{"node":{"type":["integer","string"]},"feature":{"type":["integer","string"]},"sketch":{"type":"string"},"points":{"type":"array","items":{"type":"string"}},"reference":{"type":"string"},"spacing":{"enum":["equal","pitch"]},"pitch":{"type":["number","string"]},"margin":{"type":["number","string"]},"alignment":{"enum":["fixed","tangent"]},"normal":{"type":"array","minItems":3,"maxItems":3,"items":{"type":["number","string"]}},"closed":{"type":"boolean"},"endpoint":{"enum":["exclude","include"]},"max_source_error":{"type":["number","string"]},"alternate_feature":{"type":["integer","string"]},"alternate_node":{"type":["integer","string"]},"alternate_angle":{"type":["number","string"]}},"additionalProperties":false}},"required":["type"],"additionalProperties":false}]})",pattern);commandProps.set("pattern",pattern);}
        {V pattern=*commandProps.find("pattern"),variants=*pattern.find("oneOf"),shape=variants.arr[1],props=*shape.find("properties"),carry;arkjson::parse(R"({"type":"array","minItems":16,"maxItems":16,"items":{"type":"number"},"description":"Optional proper rigid column-major transform carrying the entire pattern before node placement; identity when omitted."})",carry);props.set("carry_frame",carry);shape.set("properties",props);variants.arr[1]=shape;pattern.set("oneOf",variants);commandProps.set("pattern",pattern);}
        {V operation=*commandProps.find("op"),choices=*operation.find("enum");for(const char* family:{"miter","swept_flange","corner","rip","tab"})for(const char* action:{"add","edit","remove"})choices.arr.push_back(V::makeStr(std::string("sheet.")+family+"."+action));choices.arr.push_back(V::makeStr("sheet.bend_table.set"));choices.arr.push_back(V::makeStr("sheet.bend_table.remove"));operation.set("enum",choices);commandProps.set("op",operation);
            V object;arkjson::parse(R"({"type":"object","description":"Editable authored sheet operation; agent_onboard sheet supplies exact operation-specific required fields. Unknown geometry fields reject at runtime."})",object);for(const char* field:{"miter","swept_flange","corner","rip","tab","table","base","flange","opening","hem","stamp"})if(std::string(field)=="corner"){V options=V::makeArr();options.arr.push_back(*commandProps.find("corner"));options.arr.push_back(object);V combined=V::makeObj();combined.set("oneOf",options);commandProps.set(field,combined);}else commandProps.set(field,object);
        }
        V part=V::makeObj(),partChoices=V::makeArr(),empty=V::makeObj();empty.set("type",V::makeStr("null"));partChoices.arr.push_back(partMetadataSchema());partChoices.arr.push_back(empty);part.set("oneOf",partChoices);commandProps.set("part",part);
        if(auto entity=commandProps.find("entity");entity&&entity->find("oneOf")){
            V variants=*entity->find("oneOf");
            if(variants.arr.size()>1){V shape=variants.arr[1],properties=*shape.find("properties"),kind=*properties.find("kind"),choices=*kind.find("enum");choices.arr.push_back(V::makeStr("recipe"));kind.set("enum",choices);properties.set("kind",kind);
                V recipe=V::makeObj(),recipeProps=V::makeObj(),type=V::makeObj(),types=V::makeArr();for(const auto& name:sketchRecipeTypes())types.arr.push_back(V::makeStr(name));types.arr.push_back(V::makeStr("three_point"));types.arr.push_back(V::makeStr("tangent"));type.set("type",V::makeStr("string"));type.set("enum",types);recipeProps.set("type",type);recipe.set("type",V::makeStr("object"));recipe.set("properties",recipeProps);recipe.set("additionalProperties",V::makeBool(true));recipe.set("description",V::makeStr("Typed source fields depend on type. agent_onboard sketch provides complete defaults; runtime rejects unknown fields and degeneracy. Edit the source with sketch.entity.set."));properties.set("recipe",recipe);shape.set("properties",properties);variants.arr[1]=shape;V updated=*entity;updated.set("oneOf",variants);commandProps.set("entity",updated);}
        }
        {V remove;arkjson::parse(R"({"type":"array","items":{"type":"string"},"maxItems":128})",remove);commandProps.set("p_remove",remove);}
        if(auto* op=commandProps.find("op")){V operation=*op;if(auto* choices=operation.find("enum")){V names=*choices;for(const char* name:{"light_up","sheet.vent","image_asset.set","image_asset.remove","stamp.set","stamp.remove","face_appearance.set","face_appearance.remove"})names.arr.push_back(V::makeStr(name));operation.set("enum",names);commandProps.set("op",operation);}}
        command.set("additionalProperties",V::makeBool(true));
        {V native=commandProps.find("native")?*commandProps.find("native"):V::makeObj();V extra=agentParametricNativeSchema();V fields=native.find("properties")?*native.find("properties"):V::makeObj();if(const auto* props=extra.find("properties"))for(const auto& p:props->obj)fields.set(p.first,p.second);V index=V::makeObj();index.set("type",V::makeStr("integer"));index.set("minimum",V::makeNum(0));V indices=V::makeObj();indices.set("type",V::makeStr("array"));indices.set("items",index);indices.set("minItems",V::makeNum(1));indices.set("maxItems",V::makeNum(12288));fields.set("vertices",indices);fields.set("triangles",indices);V fingerprint=V::makeObj();fingerprint.set("type",V::makeStr("string"));fields.set("source_topology",fingerprint);fields.set("source_vertex",fingerprint);fields.set("recipe",holesRecipeSchema());native.set("properties",fields);commandProps.set("native",native);}
        V examples;arkjson::parse(R"([{"op":"param.set","name":"width","expr":20},{"op":"feature.add","type":"box","name":"Base","p":{"dx":"width","dy":30,"dz":10}},{"op":"feature.edit","id":"Base","p":{"dx":25}},{"op":"feature.add","type":"native_solid","name":"Native block","native":{"op":"box"},"p":{"width":11,"depth":7,"height":4}},{"op":"feature.edit","id":"Native block","p":{"height":9}},{"op":"sketch.set","name":"Profile","sketch":{"plane":"XY","entities":[{"kind":"circle","name":"Ring","dof":[0,0,10]}]}},{"op":"assembly.add","name":"Placed base","feature":"Base","parent":0,"p":{"tx":10}},{"op":"mate.add","a":"Anchor","b":"Moving","type":"revolute","reference_a":"origin","reference_b":"origin"},{"op":"assembly.drive","driver":"Moving","mode":"rotate","delta":15}])",examples);command.set("examples",examples);
        arkjson::parse(R"({"oneOf":[{"type":"null"},{"type":"object","properties":{"min":{"type":["number","string"]},"max":{"type":["number","string"]}},"required":["min","max"],"additionalProperties":false}]})",limits);commandProps.set("limits",limits);command.set("properties",commandProps);commands.set("items",command);V newProps=*schema.find("properties");newProps.set("commands",commands);schema.set("properties",newProps);
        if(detailed)t.set("description",V::makeStr(t.find("description")->str+" Required checks are {query:{tool,args,path},op:ge|le|eq|between|true|false,value?,min?,max?,tolerance?,parameters?,diagnose?}. All must pass before commit. doc_preview returns candidate_id; doc_apply accepts candidate_id plus expected_revision instead of commands and reuses checked geometry. material.set also accepts density_kg_m3 instead of density, and physical SI elastic properties. See agent_onboard physics and automation."));
        V props=*schema.find("properties"),candidate=V::makeObj();candidate.set("type",V::makeStr("string"));props.set("candidate_id",candidate);V acceptDfm=V::makeObj();acceptDfm.set("type",V::makeStr("boolean"));acceptDfm.set("description",V::makeStr("Explicit acceptance after reviewing a manufacturing_propose comparison; required true for those candidates only."));props.set("accept_dfm",acceptDfm);props.set("checks",agentPostconditionsSchema());schema.set("properties",props);schema.obj.erase(std::remove_if(schema.obj.begin(),schema.obj.end(),[](const auto& p){return p.first=="required";}),schema.obj.end());V variants;arkjson::parse(R"([{"required":["commands"]},{"required":["candidate_id","expected_revision"]}])",variants);schema.set("oneOf",variants);
    }if(!d.read&&(std::string(d.name).rfind("doc_",0)==0||std::string(d.name).rfind("import_",0)==0||std::string(d.name)=="iteration_restore")){
        V actor,motivation=V::makeObj(),props=schema.find("properties")?*schema.find("properties"):V::makeObj();arkjson::parse(R"({"type":"object","properties":{"kind":{"type":"string","minLength":1,"maxLength":256},"id":{"type":"string","minLength":1,"maxLength":256},"role":{"type":"string","minLength":1,"maxLength":256},"model":{"type":"string","minLength":1,"maxLength":256}},"required":["kind","id"],"additionalProperties":false})",actor);motivation.set("type",V::makeStr("string"));motivation.set("maxLength",V::makeNum(4096));props.set("actor",actor);props.set("motivation",motivation);schema.set("properties",props);
    }t.set("inputSchema",schema);annotations.set("readOnlyHint",V::makeBool(d.read));t.set("annotations",annotations);list.arr.push_back(t);}appendAgentFeatureTools(list);appendAuthoring023Tools(list);appendAuthoring024Tools(list);appendMotionTools(list);appendParametricHudTools(list);appendExplodedViewTools(list);
    V commandItems;
    for(const auto& tool:list.arr)if(tool.find("name")->str=="doc_apply")commandItems=*tool.find("inputSchema")->find("properties")->find("commands")->find("items");
    for(auto& tool:list.arr)if(tool.find("name")->str=="doc_preview"){V schema=*tool.find("inputSchema"),props=*schema.find("properties"),commands=*props.find("commands");commands.set("items",commandItems);props.set("commands",commands);schema.set("properties",props);tool.set("inputSchema",schema);}
    enrichModeling031BSchemas(list);
    {V tool=V::makeObj(),schema,annotations=V::makeObj();tool.set("name",V::makeStr("context_graph"));tool.set("description",V::makeStr("Retrieve a small linked project context by query or exact keys. Includes authored dependencies/backlinks and attributed saved feedback; no geometry evaluation. Use known_scope and known_fingerprints to omit unchanged facts. Read object_context/native evidence before editing; this graph grants no write or manufacturing authority."));arkjson::parse(R"({"type":"object","properties":{"query":{"type":"string","maxLength":256},"keys":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"string","maxLength":256}},"depth":{"type":"integer","minimum":0,"maximum":2},"limit":{"type":"integer","minimum":1,"maximum":64},"history_limit":{"type":"integer","minimum":0,"maximum":8},"expected_revision":{"type":"integer","minimum":0},"known_scope":{"type":"string","maxLength":256},"known_fingerprints":{"type":"object","additionalProperties":{"type":"string","maxLength":64}}},"additionalProperties":false})",schema);tool.set("inputSchema",schema);annotations.set("readOnlyHint",V::makeBool(true));tool.set("annotations",annotations);list.arr.push_back(tool);}
    for(auto& tool:list.arr)if(tool.find("name")->str=="doc_apply"){V schema=*tool.find("inputSchema"),props=*schema.find("properties"),flag=V::makeObj();flag.set("type",V::makeStr("boolean"));props.set("refresh_candidate",flag);schema.set("properties",props);tool.set("inputSchema",schema);}
    appendProjectMemoryTools(list);
    return list;
}
Document makeDemoDocument(){
    Document d;d.setParam("radius","32");d.setParam("tube","11");d.setParam("waves","7");d.setParam("amplitude","0.22");
    Feature f;f.type=FeatureType::ParametricSurface;f.name="Harmonic ring";
    std::string t="tube*(1+amplitude*cos(waves*u+3*v))";
    f.p={{"x","(radius+"+t+"*cos(v))*cos(u)"},{"y",t+"*sin(v)+15"},{"z","(radius+"+t+"*cos(v))*sin(u)"},{"nu","160"},{"nv","64"},{"wrap_u","1"},{"wrap_v","1"}};
    d.addFeature(f);d.recompute();return d;
}
}
