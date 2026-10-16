#include "ark/native_backend.hpp"
#include "ark/occt_solid.hpp"
#include "../src/occt/common.hpp"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using V=arkjson::Value;namespace native=ark::occt;
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
const V& field(const V& value,const char* name){const auto* found=value.find(name);if(!found)throw std::runtime_error(std::string("Missing fixture field: ")+name);return *found;}
V& edit(V& value,const char* name){for(auto& item:value.obj)if(item.first==name)return item.second;throw std::runtime_error(std::string("Missing mutable fixture field: ")+name);}
V parse(const char* text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Bad synthetic fixture JSON");return value;}
V boxRequest(){return parse(R"({"operation":"model_solid","request":{"op":"box","width":12,"depth":15,"height":8}})");}
V restore(const V& request,const V& result){V envelope=V::makeObj();envelope.set("operation",V::makeStr("restore_native_result"));envelope.set("resolved_request",request);envelope.set("saved_result",result);return ark::nativeWorkerRequest(envelope);}
template<class F>void rejects(F action,const char* message){bool failed=false;try{action();}catch(const std::exception& error){failed=std::string(error.what()).find("Saved native result rejected")!=std::string::npos||std::string(error.what()).find("Invalid native B-rep payload")!=std::string::npos;}check(failed,message);}
V& face(V& result){for(auto& row:edit(edit(result,"topology"),"references").arr)if(field(row,"kind").str=="face"&&field(row,"status").str=="bound"&&field(row,"name").str.find("@ark.")==std::string::npos)return row;throw std::runtime_error("Fixture lacks semantic face");}
V& edge(V& result){for(auto& row:edit(edit(result,"topology"),"references").arr)if(field(row,"kind").str=="edge"&&field(row,"status").str=="bound")return row;throw std::runtime_error("Fixture lacks edge");}
struct DisableCache {
    std::string previous;bool existed=false;
    DisableCache(){if(const char* old=std::getenv("ARK_NATIVE_RESULT_CACHE")){previous=old;existed=true;}set("0");}
    static void set(const char* value){
#ifdef _WIN32
        _putenv_s("ARK_NATIVE_RESULT_CACHE",value);
#else
        setenv("ARK_NATIVE_RESULT_CACHE",value,1);
#endif
    }
    ~DisableCache(){
#ifdef _WIN32
        _putenv_s("ARK_NATIVE_RESULT_CACHE",existed?previous.c_str():"");
#else
        if(existed)setenv("ARK_NATIVE_RESULT_CACHE",previous.c_str(),1);else unsetenv("ARK_NATIVE_RESULT_CACHE");
#endif
    }
};
void reportRestoration(const V& request,const V& original){
    auto saved=original;auto& report=edit(saved,"report");report.set("volume_mm3",V::makeNum(999999));report.set("area_mm2",V::makeNum(1));report.set("bounds_mm",parse("[999,999,999,1000,1000,1000]"));report.set("historical_note",V::makeStr("synthetic previous diagnostic"));saved.set("mesh",parse(R"({"pos":[999,999,999],"idx":[999999]})"));
    const auto restored=restore(request,saved);const auto& actual=field(restored,"report"),&before=field(original,"report");
    for(const char* key:{"volume_mm3","area_mm2","bounds_mm","solids","faces"})check(field(actual,key).dump()==field(before,key).dump(),"Restore must measure exact current geometry, never trust saved scalar reports");
    check(field(restored,"brep_base64").str==field(original,"brep_base64").str,"Restore must preserve original serialized B-rep bytes and downstream identity");
    check(field(field(restored,"topology"),"brep_sha256").str==field(field(original,"topology"),"brep_sha256").str,"Restore preserves exact reference identity");
    check(field(restored,"topology").dump()==field(original,"topology").dump(),"Validated topology representation remains identical for dependent native request keys");
    check(field(restored,"mesh").dump()==field(original,"mesh").dump(),"Restore tessellates current B-rep instead of trusting saved display triangles");
    check(!field(actual,"manufacturing_authoritative").b&&!field(actual,"display_mesh_authoritative").b&&!field(actual,"derivation_replayed").b,"Restore does not manufacture authority or claim derivation replay");
    check(!actual.find("historical_note")&&field(field(field(actual,"saved_previous_evaluation"),"diagnostics"),"historical_note").str=="synthetic previous diagnostic","Previous diagnostics are explicitly quarantined from current measured fields");
    const auto again=restore(request,restored);check(field(again,"brep_base64").str==field(original,"brep_base64").str,"Repeated restore preserves exact B-rep identity");check(field(field(again,"report"),"saved_previous_evaluation").dump()==field(actual,"saved_previous_evaluation").dump(),"Repeated restore preserves one flat previous-evaluation wrapper without growth");
}
void rejectionContracts(const V& request,const V& original){
    auto bad=original;edit(bad,"topology").set("brep_sha256",V::makeStr(std::string(64,'0')));rejects([&]{restore(request,bad);},"Reject mismatching exact B-rep digest");
    bad=original;auto& fields=edit(bad,"topology").obj;fields.erase(std::remove_if(fields.begin(),fields.end(),[](const auto& item){return item.first=="references";}),fields.end());rejects([&]{restore(request,bad);},"Reject missing reference inventory without dereferencing a null field");
    bad=original;bad.set("brep_base64",V::makeStr(native::base64Encode("not a B-rep")));edit(bad,"topology").set("brep_sha256",V::makeStr(native::sha256("not a B-rep")));rejects([&]{restore(request,bad);},"Reject invalid B-rep even if its digest matches");
    bad=original;edit(bad,"report").set("solids",V::makeNum(2));rejects([&]{restore(request,bad);},"Reject saved solid census disagreement");
    bad=original;edit(bad,"report").set("faces",V::makeNum(7));rejects([&]{restore(request,bad);},"Reject saved face census disagreement");
    bad=original;edit(bad,"report").set("volume_mm3",V::makeNum(-1));rejects([&]{restore(request,bad);},"Reject negative saved volume contract");
    bad=original;edit(bad,"report").set("manufacturing_authoritative",V::makeBool(true));rejects([&]{restore(request,bad);},"Reject fabricated manufacturing authority");
    bad=original;auto& rows=edit(edit(bad,"topology"),"references");rows.arr.push_back(rows.arr.front());rejects([&]{restore(request,bad);},"Reject duplicate semantic aliases");
    for(const char* kind:{"vertex","FACE",""}){bad=original;face(bad).set("kind",V::makeStr(kind));rejects([&]{restore(request,bad);},"Reject unsupported alias kinds");}
    for(const char* index:{"[-1]","[0.5]","[999999]","[\"0\"]","[0,1]"}){bad=original;face(bad).set("indices",parse(index));rejects([&]{restore(request,bad);},"Reject malformed or out-of-range semantic indices");}
    bad=original;face(bad).set("status",V::makeStr("invented"));rejects([&]{restore(request,bad);},"Reject unknown reference status");
    bad=original;face(bad).set("status",V::makeStr("deleted"));rejects([&]{restore(request,bad);},"Reject deleted alias with live indices");
    bad=original;face(bad).set("name",V::makeStr(""));rejects([&]{restore(request,bad);},"Reject empty semantic names");
    bad=original;face(bad).set("split_into",parse("[1]"));rejects([&]{restore(request,bad);},"Reject malformed lineage names");
    bad=original;auto& normal=edit(edit(face(bad),"descriptor"),"normal");for(auto& number:normal.arr)number.num=-number.num;rejects([&]{restore(request,bad);},"Reject inward normal masquerading as outward semantic frame");
    bad=original;auto& desc=edit(face(bad),"descriptor");desc.set("x_axis",field(desc,"normal"));rejects([&]{restore(request,bad);},"Reject non-tangent planar X datum");
    bad=original;edit(edit(edge(bad),"descriptor"),"tangent").arr[0]=V::makeNum(123);rejects([&]{restore(request,bad);},"Reject malformed direction seeds");
    bad=original;edit(edit(bad,"topology"),"vertices").arr[0].set("source_topology",V::makeStr(std::string(64,'0')));rejects([&]{restore(request,bad);},"Reject stale exact vertex digest");
    bad=original;edit(edit(bad,"topology"),"vertices").arr[0].set("name",V::makeStr("vertex.1"));rejects([&]{restore(request,bad);},"Reject retargeted vertex indices");
    bad=original;edit(edit(edit(bad,"topology"),"vertices").arr[0],"point_mm").arr[0].num+=1;rejects([&]{restore(request,bad);},"Reject saved vertex point inconsistent with exact indexed vertex");
    bad=original;edit(edit(bad,"topology"),"vertices").arr.pop_back();rejects([&]{restore(request,bad);},"Reject incomplete vertex registry claimed complete");
    auto unsupported=request;edit(unsupported,"request").set("op",V::makeStr("vertex_pull"));rejects([&]{restore(unsupported,original);},"Self-intersection checked vertex edits require normal derivation replay");
    unsupported=request;unsupported.set("operation",V::makeStr("model_electronics"));rejects([&]{restore(unsupported,original);},"Electronics clearance claims require normal derivation replay");
}
void explicitFrame(const V& request,const V& original){auto saved=original;auto& d=edit(face(saved),"descriptor");const auto n=field(d,"normal"),x=field(d,"x_axis");V y=V::makeArr();y.arr={V::makeNum(n.arr[1].num*x.arr[2].num-n.arr[2].num*x.arr[1].num),V::makeNum(n.arr[2].num*x.arr[0].num-n.arr[0].num*x.arr[2].num),V::makeNum(n.arr[0].num*x.arr[1].num-n.arr[1].num*x.arr[0].num)};d.set("x_axis",y);const auto restored=restore(request,saved);auto mutableRestored=restored;check(field(field(face(mutableRestored),"descriptor"),"x_axis").dump()==y.dump(),"Valid explicit tangent frame orientation remains saved CAD design state");}
void exactDomains(){
    auto a=BRepPrimAPI_MakeBox(2,3,4).Shape(),b=BRepPrimAPI_MakeBox(gp_Pnt(5,0,0),2,3,4).Shape();const auto saved=native::generatedShapeResult(native::compound({a,b}),false,{},true);auto pattern=boxRequest();edit(pattern,"request").set("op",V::makeStr("linear_pattern"));const auto restored=restore(pattern,saved);check(field(field(restored,"report"),"solids").num==2&&std::abs(field(field(restored,"report"),"volume_mm3").num-48)<1e-10,"Explicit multi-solid pattern preserves all positive solids");rejects([&]{restore(boxRequest(),saved);},"Single-solid operation cannot restore a compound");
    auto negative=b;negative.Reverse();auto larger=BRepPrimAPI_MakeBox(3,4,5).Shape();const auto mixed=native::generatedShapeResult(native::compound({larger,negative}),false,{},true);rejects([&]{restore(pattern,mixed);},"Positive compound total cannot conceal a negative oriented piece");
    auto planar=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0,4,0,5).Shape();auto surface=native::generatedShapeResult(planar,false,{},false);auto request=parse(R"({"operation":"model","request":{"op":"surface_nurbs"}})");const auto accepted=restore(request,surface);check(field(field(accepted,"report"),"volume_mm3").t==V::Null&&std::abs(field(field(accepted,"report"),"area_mm2").num-20)<1e-10,"Exact surface restore retains null solid volume and measured area");
}
V referenceFrame(const V& result,const std::string& name){V request=V::makeObj();request.set("input_brep",field(result,"brep_base64"));request.set("topology",field(result,"topology"));request.set("reference",V::makeStr(name));return native::referenceFrameNative(request);}
void outwardDescriptorAgreement(const V& result){for(const auto& row:field(field(result,"topology"),"references").arr)if(field(row,"kind").str=="face"&&field(row,"status").str=="bound")if(const auto* descriptor=row.find("descriptor");descriptor&&descriptor->find("normal")){const auto& normal=field(*descriptor,"normal");const auto actual=referenceFrame(result,field(row,"name").str);const auto& frame=field(actual,"frame");double error=0;for(size_t axis=0;axis<3;++axis)error+=std::abs(normal.arr[axis].num-frame.arr[8+axis].num);check(error<1e-10,"Producer descriptor normal agrees with the bound result face and live exact reference frame");}}
void propagatedHandles(const V& original){
    auto seeded=original;V current=face(seeded);current.set("name",V::makeStr("@ark.face."+field(field(seeded,"topology"),"brep_sha256").str+"."+std::to_string((int)field(current,"indices").arr[0].num)));edit(edit(seeded,"topology"),"references").arr.push_back(current);
    auto request=parse(R"({"operation":"model_solid","request":{"op":"transform","frame":[1,0,0,0,0,1,0,0,0,0,1,0,2,0,0,1]}})");edit(request,"request").set("input_brep",field(seeded,"brep_base64"));edit(request,"request").set("input_topology",field(seeded,"topology"));const auto transformed=ark::nativeWorkerRequest(request);size_t inactiveWithoutDescriptor=0;for(const auto& row:field(field(transformed,"topology"),"references").arr)if(field(row,"status").str=="obsolete"&&!row.find("descriptor"))++inactiveWithoutDescriptor;check(inactiveWithoutDescriptor>0,"Actual transform producer omits descriptors on obsolete current-B-rep handles");outwardDescriptorAgreement(transformed);
    const auto restored=restore(request,transformed);check(field(restored,"topology").dump()==field(transformed,"topology").dump(),"Obsolete null descriptors and all accepted metadata remain identical after exact snapshot validation");
    auto explicitNull=transformed;for(auto& row:edit(edit(explicitNull,"topology"),"references").arr)if(field(row,"status").str=="obsolete")row.set("descriptor",V{});check(field(restore(request,explicitNull),"topology").dump()==field(explicitNull,"topology").dump(),"Explicitly null inactive descriptor is preserved without creating a live frame");
    auto tool=native::shapeResult(BRepPrimAPI_MakeBox(gp_Pnt(6,4,-1),3,4,10).Shape(),true);auto cut=parse(R"({"operation":"model_solid","request":{"op":"cut"}})");edit(cut,"request").set("input_brep",field(transformed,"brep_base64"));edit(cut,"request").set("input_topology",field(transformed,"topology"));edit(cut,"request").set("tool_brep",field(tool,"brep_base64"));edit(cut,"request").set("tool_topology",field(tool,"topology"));const auto cutResult=ark::nativeWorkerRequest(cut);outwardDescriptorAgreement(cutResult);check(field(restore(cut,cutResult),"topology").dump()==field(cutResult,"topology").dump(),"Boolean result restores without changing dependent topology request inputs");
    auto fillet=parse(R"({"operation":"model_solid","request":{"op":"fillet","edges":["edge.x.ymin.zmin"],"radius":0.25}})");edit(fillet,"request").set("input_brep",field(cutResult,"brep_base64"));edit(fillet,"request").set("input_topology",field(cutResult,"topology"));const auto finished=ark::nativeWorkerRequest(fillet);outwardDescriptorAgreement(finished);check(field(restore(fillet,finished),"topology").dump()==field(finished,"topology").dump(),"Fillet after a Boolean retains exact saved semantic bindings and representation");
}
void restoreReadCount(const V& request,const V& original){
    namespace fs=std::filesystem;const auto directory=fs::temp_directory_path()/("ark-snapshot-read-count-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(fs::create_directory(directory),"Create owned timing fixture");std::string previous;bool existed=false;if(const char* old=std::getenv("ARK_NATIVE_PROFILE_DIRECTORY")){previous=old;existed=true;}
    const auto raw=directory.u8string();const std::string path(raw.begin(),raw.end());
#ifdef _WIN32
    _putenv_s("ARK_NATIVE_PROFILE_DIRECTORY",path.c_str());
#else
    setenv("ARK_NATIVE_PROFILE_DIRECTORY",path.c_str(),1);
#endif
    struct Cleanup {fs::path directory;std::string previous;bool existed;~Cleanup(){
#ifdef _WIN32
        _putenv_s("ARK_NATIVE_PROFILE_DIRECTORY",existed?previous.c_str():"");
#else
        if(existed)setenv("ARK_NATIVE_PROFILE_DIRECTORY",previous.c_str(),1);else unsetenv("ARK_NATIVE_PROFILE_DIRECTORY");
#endif
        std::error_code error;for(const auto& entry:fs::directory_iterator(directory,error))if(entry.is_regular_file())fs::remove(entry.path(),error);fs::remove(directory,error);
    }} cleanup{directory,previous,existed};
    V restored;{native::NativeProfileRequest timing;timing.operation("restore_native_result");restored=native::restoreNativeResult(request,original);timing.succeeded();}size_t files=0,reads=0,validations=0,completions=0;for(const auto& entry:fs::directory_iterator(directory)){++files;V timing;check(arkjson::parse(native::readFile(entry.path()),timing),"Restore timing record parses");for(const auto& phase:field(timing,"phases").arr){const auto& name=field(phase,"phase").str;if(name=="readBrep")reads+=(size_t)field(phase,"calls").num;if(name=="brep.validate")validations+=(size_t)field(phase,"calls").num;if(name=="completeSelectionReferences")completions+=(size_t)field(phase,"calls").num;}}check(files==1&&reads==1&&validations==1&&completions==1,"Restore reads/validates the original B-rep once and completes selection once on that same local shape");check(field(restored,"brep_base64").str==field(original,"brep_base64").str&&field(restored,"topology").dump()==field(original,"topology").dump(),"Private shape reuse preserves exact saved bytes and complete semantic representation");
}
void recipeSnapshots(const V& stock){
    auto request=parse(R"({"operation":"model_solid","request":{"op":"holes_orchestrator","recipe":{"family":"metric","size":"M3","kind":"clearance","fit":"normal","termination":"blind","recess":"counterbore","points":[{"name":"bore","position":[6,7,8],"direction":[0,0,-1]}]},"depth":4,"recess_diameter":6,"recess_depth":2}})");edit(request,"request").set("input_brep",field(stock,"brep_base64"));edit(request,"request").set("input_topology",field(stock,"topology"));const auto cut=ark::nativeWorkerRequest(request),restored=restore(request,cut);const auto& report=field(restored,"report"),&previous=field(field(field(report,"saved_previous_evaluation"),"diagnostics"),"holes_orchestrator");check(field(restored,"brep_base64").str==field(cut,"brep_base64").str&&field(restored,"topology").dump()==field(cut,"topology").dump(),"Saved counterbore retains exact B-rep and semantic reference inputs");check(std::abs(field(report,"volume_mm3").num-field(field(cut,"report"),"volume_mm3").num)<1e-8,"Saved holes result is measured from its actual exact geometry");const auto& location=field(previous,"locations").arr.front();check(field(location,"entry_boundary_checked").b&&field(location,"blind_stock_containment_checked").b,"Original producer contains real entry and blind-stock checks");check(!report.find("holes_orchestrator")&&!report.find("entry_boundary_checked")&&!report.find("blind_stock_containment_checked")&&!field(report,"derivation_replayed").b&&!field(report,"manufacturing_authoritative").b,"Hole derivation checks remain only historical diagnostics and never fresh qualification");
    auto extrusion=parse(R"({"operation":"model_solid","request":{"op":"extrude","depth":5,"profile":{"entities":[{"kind":"circle","name":"ring","center":[0,0],"radius":2}]}}})");const auto source=ark::nativeWorkerRequest(extrusion),snapshot=restore(extrusion,source);check(field(snapshot,"brep_base64").str==field(source,"brep_base64").str&&field(snapshot,"topology").dump()==field(source,"topology").dump(),"Fully resolved saved extrusion preserves exact geometry and downstream semantic input identity");check(std::abs(field(field(snapshot,"report"),"volume_mm3").num-20*std::acos(-1.))<1e-8,"Saved circular extrusion is remeasured with unchanged exact integration");check(!field(snapshot,"report").find("extrusion")&&field(field(field(snapshot,"report"),"saved_previous_evaluation"),"diagnostics").find("extrusion"),"Extrusion construction evidence remains explicitly historical");
}
void semanticGeometry(const V& request,const V& original){
    const auto restored=restore(request,original);for(const auto& row:field(field(original,"topology"),"references").arr)if(field(row,"status").str=="bound")check(referenceFrame(original,field(row,"name").str).dump()==referenceFrame(restored,field(row,"name").str).dump(),"Restored semantic face/edge resolves identical exact frame and evidence");
    auto generic=original;V row=face(generic);const auto index=field(row,"indices").arr[0].num;const auto identity=field(field(original,"topology"),"brep_sha256").str;const auto currentName="@ark.face."+identity+"."+std::to_string((int)index);row.set("name",V::makeStr(currentName));edit(edit(generic,"topology"),"references").arr.push_back(row);const auto withCurrent=restore(request,generic);check(referenceFrame(withCurrent,currentName).find("frame")!=nullptr,"Reserved current-B-rep handle remains bound to its exact saved face");
    auto bad=generic;edit(edit(bad,"topology"),"references").arr.back().set("name",V::makeStr("@ark.face."+std::string(64,'0')+"."+std::to_string((int)index)));rejects([&]{restore(request,bad);},"Reject live reserved handle with stale digest");
    bad=generic;edit(edit(bad,"topology"),"references").arr.back().set("name",V::makeStr("@ark.face."+identity+"."+std::to_string((int)index+1)));rejects([&]{restore(request,bad);},"Reject reserved handle retargeted to another exact face");
    const auto cylinderRequest=parse(R"({"operation":"model_solid","request":{"op":"cylinder","radius":3,"height":9}})");const auto cylinder=ark::nativeWorkerRequest(cylinderRequest),restoredCylinder=restore(cylinderRequest,cylinder);check(referenceFrame(cylinder,"axis").dump()==referenceFrame(restoredCylinder,"axis").dump(),"Restored analytic cylinder keeps axis position, direction and radius");
    bad=cylinder;for(auto& alias:edit(edit(bad,"topology"),"references").arr)if(field(alias,"name").str=="axis")edit(alias,"descriptor").set("axis",parse("[1,0,0]"));rejects([&]{restore(cylinderRequest,bad);},"Reject saved analytic axis that disagrees with the bound cylinder");
}
}
int main(){try{DisableCache disable;const auto request=boxRequest();const auto original=ark::nativeWorkerRequest(request);reportRestoration(request,original);rejectionContracts(request,original);explicitFrame(request,original);exactDomains();semanticGeometry(request,original);propagatedHandles(original);restoreReadCount(request,original);recipeSnapshots(original);std::cout<<checks<<" native saved-state checks passed: fresh exact measurements/mesh, original B-rep identity, strict semantic and vertex custody, explicit replay domains\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
