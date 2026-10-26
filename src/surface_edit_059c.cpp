#include "ark/surface_edit_059c.hpp"
#include "ark/native_backend.hpp"
#include "ark/native_features.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error("SURFACE_CONTROL: "+why);}
const V& field(const V& a,const char* key){const auto* p=a.find(key);need(p!=nullptr,std::string("Missing ")+key);return *p;}
std::string text(const V& a,const char* key){const auto& v=field(a,key);need(v.t==V::Str&&!v.str.empty()&&v.str.size()<=256,std::string("Expected text: ")+key);return v.str;}
void keys(const V& a,std::initializer_list<const char*> allowed){need(a.t==V::Obj,"Expected object");for(const auto& p:a.obj)need(std::any_of(allowed.begin(),allowed.end(),[&](const char* k){return p.first==k;}),"Unknown field: "+p.first);}
u64 id(const Document& document,const V& value){u64 result=0;unsigned count=0;for(const auto& f:document.features)if((value.t==V::Num&&value.num==(double)f.id)||(value.t==V::Str&&value.str==f.name)){result=f.id;++count;}need(count==1,count?"Feature name is ambiguous":"Feature does not exist");return result;}
V json(const char* str){V v;need(arkjson::parse(str,v),"Internal schema error");return v;}
NativePayload exact(Document& d,u64 source){ensureNativeBody(d,source,d.evalBodies);for(const auto& b:d.evalBodies)if(b.id==source){need(b.nativeData.find("brep_base64")&&b.nativeData.find("topology"),"Exact source B-rep and topology are required");return b.nativeData;}throw std::runtime_error("SURFACE_CONTROL: source has no evaluated body");}
void transfer(const V& from,V& to,std::initializer_list<const char*> fields){for(const auto* k:fields)if(const auto* p=from.find(k))to.set(k,*p);}
}
V surfaceControlSchema059(){return json(R"({"type":"object","properties":{"action":{"type":"string","enum":["inspect","candidates","stretch_plan","glue_plan"]},"expected_revision":{"type":"integer","minimum":0},"id":{"type":["integer","string"]},"reference":{"type":"string"},"source_brep_sha256":{"type":"string"},"target_id":{"type":["integer","string"]},"target_brep_sha256":{"type":"string"},"u":{"type":"integer","minimum":0,"maximum":31},"v":{"type":"integer","minimum":0,"maximum":31},"max_distance_mm":{"type":"number","exclusiveMinimum":0,"maximum":1000},"limit":{"type":"integer","minimum":1,"maximum":64},"max_displacement_mm":{"type":"number","exclusiveMinimum":0,"maximum":100000},"edits":{"type":"array","minItems":1,"maxItems":128,"items":{"type":"object","properties":{"u":{"type":"integer","minimum":0,"maximum":31},"v":{"type":"integer","minimum":0,"maximum":31},"delta_mm":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}}},"required":["u","v","delta_mm"],"additionalProperties":false}},"target_vertex":{"type":"string"},"target_edge":{"type":"string"},"tangent_axis":{"type":"string","enum":["none","u","v"]},"tangent_tolerance_deg":{"type":"number","exclusiveMinimum":0,"maximum":1},"name":{"type":"string"}},"required":["action","expected_revision","id"],"additionalProperties":false})");}
V surfaceControl059(AgentSession& session,const V& args){
    const auto& revision=field(args,"expected_revision");need(revision.t==V::Num&&revision.num==(double)session.revision,"Revision changed; inspect the current document");const auto action=text(args,"action");
    if(action=="inspect")keys(args,{"action","expected_revision","id","reference"});
    else if(action=="candidates")keys(args,{"action","expected_revision","id","reference","source_brep_sha256","target_id","target_brep_sha256","u","v","max_distance_mm","limit"});
    else if(action=="stretch_plan")keys(args,{"action","expected_revision","id","reference","source_brep_sha256","max_displacement_mm","edits","name"});
    else{need(action=="glue_plan","Unknown action");keys(args,{"action","expected_revision","id","reference","source_brep_sha256","target_id","target_brep_sha256","u","v","max_distance_mm","max_displacement_mm","target_vertex","target_edge","tangent_axis","tangent_tolerance_deg","name"});}
    // Materialization is confined to this copied document. Even derived payloads
    // and evaluation pointers of the live session remain untouched.
    Document snapshot=session.document;snapshot.evaluatingBodies=nullptr;const u64 source=id(snapshot,field(args,"id"));auto sourceData=exact(snapshot,source);V native=json(R"({"op":"surface_control_net"})");native.set("input_brep",*sourceData.find("brep_base64"));native.set("topology",*sourceData.find("topology"));transfer(args,native,{"reference"});
    V controls=nativeModelRequest(native);const auto sourceHash=text(controls,"source_brep_sha256");if(action!="inspect")need(text(args,"source_brep_sha256")==sourceHash,"Source B-rep changed; inspect the current control net");
    controls.set("revision",revision);controls.set("id",V::makeNum((double)source));controls.set("coordinate_space",V::makeStr("definition local millimetres; no occurrence placement is inferred"));if(action=="inspect")return controls;
    V glue=V::makeObj();u64 target=0;if(action=="candidates"||action=="glue_plan"){
        target=id(snapshot,field(args,"target_id"));need(target!=source,"Glue target must be a different feature");const auto targetData=exact(snapshot,target);const auto& topology=*targetData.find("topology");const auto targetHash=text(topology,"brep_sha256");need(text(args,"target_brep_sha256")==targetHash,"Target B-rep changed; inspect the current exact target");
        glue.set("target_brep",*targetData.find("brep_base64"));glue.set("target_topology",topology);glue.set("target_brep_sha256",V::makeStr(targetHash));transfer(args,glue,{"u","v","max_distance_mm","limit","target_vertex","target_edge","tangent_axis","tangent_tolerance_deg"});
    }
    if(action=="candidates"){native.set("op",V::makeStr("surface_glue_candidates"));for(const auto& p:glue.obj)native.set(p.first,p.second);V result=nativeModelRequest(native);result.set("revision",revision);result.set("id",V::makeNum((double)source));result.set("target_id",V::makeNum((double)target));result.set("coordinate_space",*controls.find("coordinate_space"));return result;}
    V recipe=json(R"({"op":"deform_poles"})");recipe.set("reference",field(controls,"face_reference"));recipe.set("source_brep_sha256",V::makeStr(sourceHash));recipe.set("max_displacement_mm",field(args,"max_displacement_mm"));native.set("op",V::makeStr("surface_deform_poles"));native.set("source_brep_sha256",V::makeStr(sourceHash));native.set("max_displacement_mm",field(args,"max_displacement_mm"));
    if(action=="stretch_plan"){recipe.set("edits",field(args,"edits"));native.set("edits",field(args,"edits"));}
    else{native.set("glue",glue);V persisted=V::makeObj();transfer(glue,persisted,{"u","v","target_brep_sha256","max_distance_mm","target_vertex","target_edge","tangent_axis","tangent_tolerance_deg"});recipe.set("glue",persisted);}
    // Prove the explicit exact edit now; doc_preview subsequently checks the
    // authored DAG, and publication still requires its current revision.
    const V evaluated=nativeModelRequest(native);V command=json(R"({"op":"feature.add","type":"surface_edit"})"),inputs=V::makeArr();inputs.arr.push_back(V::makeNum((double)source));if(target)inputs.arr.push_back(V::makeNum((double)target));command.set("inputs",inputs);command.set("native",recipe);command.set("name",V::makeStr(args.find("name")?text(args,"name"):action=="glue_plan"?"Surface corner glue":"Surface pole stretch"));V commands=V::makeArr();commands.arr.push_back(command);V preview=json(R"({"tool":"doc_preview","arguments":{"detail":"changes"}})");auto previewArgs=*preview.find("arguments");previewArgs.set("expected_revision",revision);previewArgs.set("commands",commands);preview.set("arguments",previewArgs);V out=V::makeObj();out.set("revision",revision);out.set("id",V::makeNum((double)source));out.set("source_brep_sha256",V::makeStr(sourceHash));out.set("commands",commands);out.set("preview",preview);out.set("evidence",field(evaluated,"report"));out.set("coordinate_space",*controls.find("coordinate_space"));out.set("note",V::makeStr("Read-only exact plan; preview and explicitly apply its candidate. Source and optional target remain dependencies. Corner/tangent alignment does not sew boundaries or certify G1/G2 continuity."));return out;
}
}
