#include "ark/agent.hpp"
#include "ark/assembly_authoring.hpp"
#include "ark/cad_import.hpp"
#include "ark/design_containers.hpp"
#include "ark/references.hpp"
#include "ark/serialize.hpp"
#include "../src/occt/common.hpp"
#include "native_process.hpp"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <iostream>
#include <set>

using namespace ark;
using V=arkjson::Value;
namespace {
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
V json(const char* source){V v;check(arkjson::parse(source,v),"Fixture JSON parses");return v;}
V accepted(const V& reply){check(reply.find("isError")&&!reply.find("isError")->b,reply.dump());return *reply.find("structuredContent");}
void apply(AgentSession& session,V commands){V args=V::makeObj();args.set("commands",commands);args.set("expected_revision",V::makeNum(session.revision));accepted(session.call("doc_apply",args));}
void valid(const Document& d){check(d.parameterError.empty(),d.parameterError);for(const auto& result:d.lastResults)check(result.ok,result.error);}
size_t nativeBytes(const Document& d){size_t bytes=0;for(const auto& body:d.evalBodies)bytes+=body.nativeData.dump().size();for(const auto& entry:d.evaluationCache)bytes+=entry.second.body->nativeData.dump().size();return bytes;}
size_t referenceBytes(const Document& d){size_t bytes=0;for(const auto& entry:d.referenceFrameCache){bytes+=entry.second.brep.size()+entry.second.topology.size();for(const auto& frame:entry.second.frames)bytes+=frame.first.size()+frame.second.dump().size();}return bytes;}
std::string canonical(V value){std::function<void(V&)> sort=[&](V& item){if(item.t==V::Obj){for(auto& entry:item.obj)sort(entry.second);std::sort(item.obj.begin(),item.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});}else if(item.t==V::Arr)for(auto& row:item.arr)sort(row);};sort(value);return value.dump();}
size_t meshBytes(const Document& d){std::set<const Mesh*> seen;size_t bytes=0;auto count=[&](const Body& b){if(b.baked&&seen.insert(b.baked.get()).second)bytes+=(b.baked->pos.capacity()+b.baked->nrm.capacity())*sizeof(float)+b.baked->idx.capacity()*sizeof(u32);};for(const auto& b:d.evalBodies)count(b);for(const auto& c:d.evaluationCache)count(*c.second.body);return bytes;}
void fixture(){
    native_test::TempDirectory temporary("ark-native-assembly-scaling-");Document d;std::vector<std::filesystem::path> paths;size_t sourceBytes=0;
    for(int i=0;i<8;++i){
        const auto path=temporary.path()/("source-"+std::to_string(i)+".step");paths.push_back(path);
        const auto box=BRepPrimAPI_MakeBox(10+i,12,6).Shape(),cylinder=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30,0,0),gp_Dir(0,0,1)),3+i*.25,8).Shape();
        occt::writeStep(occt::compound({box,cylinder}),path);sourceBytes+=std::filesystem::file_size(path);
        const auto imported=buildCadImport(native_test::pathUtf8(path));Feature feature;feature.name="Native definition "+std::to_string(i+1);feature.type=FeatureType::NativeSolid;feature.nativeSpec=*imported.find("command")->find("native");const auto featureId=d.addFeature(feature);
        const u64 group=(u64)i*41+1;d.assembly.push_back({group,0,0,"Module "+std::to_string(i+1),{},true});for(int copy=0;copy<40;++copy)d.assembly.push_back({group+copy+1,group,featureId,"Occurrence "+std::to_string(i)+"-"+std::to_string(copy),{{"tx",std::to_string(copy*60)}},true});
        V pin=V::makeObj();pin.set("op",V::makeStr("assembly.external.set"));pin.set("feature",V::makeNum(featureId));pin.set("path",V::makeStr(native_test::pathUtf8(path)));pin.set("source_revision",V::makeStr("fixture-1"));pin.set("source_sha256",*feature.nativeSpec.find("package")->find("source_sha256"));std::string error;check(applyAssemblyAuthoringCommand(d,pin,error)&&error.empty(),error);
    }
    d.nextAssemblyId_=329;d.recompute();valid(d);check(d.evalBodies.size()==8&&d.evaluationCache.size()==8,"All eight native multi-solid definitions evaluate");
    for(const auto& body:d.evalBodies){const auto* topology=body.nativeData.find("topology");check(topology&&topology->find("references"),"Exact imported source provides reference inventory");std::string reference;for(const auto& row:topology->find("references")->arr)if(row.find("kind")&&row.find("kind")->asStr()=="face"&&row.find("frame_available")&&row.find("frame_available")->b){reference=row.find("name")->str;break;}check(!reference.empty(),"Exact source has a bound face frame");resolveGeometryReference(d,body.id,reference);}
    // Compare warmed recomputes over exactly the same eight retained sources.
    auto before=assemblyExternalIo();d.recompute();valid(d);const auto fullIo=assemblyExternalIo();const size_t fullNative=nativeBytes(d),fullMesh=meshBytes(d),fullReference=referenceBytes(d);check(d.referenceFrameCache.size()==8,"Real exact reference lookups populate eight native reference caches");
    check(fullIo.filesRead-before.filesRead==8&&fullIo.bytesRead-before.bytesRead==sourceBytes,"Full recompute actually reads each pinned source once");
    std::vector<std::weak_ptr<const Body>> discarded;for(const auto& cached:d.evaluationCache)if(cached.first!=1)discarded.push_back(cached.second.body);
    std::string error;check(applyAssemblyAuthoringCommand(d,json(R"({"op":"assembly.load","roots":[1]})"),error)&&error.empty(),error);
    before=assemblyExternalIo();d.recompute();valid(d);const auto selectedIo=assemblyExternalIo();const size_t selectedNative=nativeBytes(d),selectedMesh=meshBytes(d);
    check(selectedIo.filesRead-before.filesRead==1&&selectedIo.bytesRead-before.bytesRead==std::filesystem::file_size(paths[0]),"Selected recompute reads only the loaded pin");
    check(d.evalBodies.size()==1&&d.evaluationCache.size()==1&&d.features.size()==8,"Selected native geometry/cache released while all authored definitions remain");
    check(d.referenceFrameCache.size()==1&&d.referenceFrameCache.count(1)&&referenceBytes(d)<fullReference,"Unselected actual native reference caches release their B-rep/topology buffers");
    for(const auto& weak:discarded)check(weak.expired(),"Excluded cache body ownership is actually released");
    check(selectedNative<fullNative&&selectedMesh<fullMesh,"Retained native payload and mesh buffers decrease after selection");
    check(d.evalBodies[0].nativeData.find("report")->find("solids")->num==2,"Loaded source retains both exact solids");
    std::vector<AssemblyPlacement> placed;check(resolveAssembly(d,placed,error),error);check(placed.size()==320&&std::count_if(placed.begin(),placed.end(),[](const auto& p){return p.visible;})==40,"Native assembly keeps all 320 physical occurrences and displays selected 40");
    std::cout<<"B.04_NATIVE_MEASUREMENT {\"definitions_total\":8,\"occurrences_total\":320,\"loaded_full\":8,\"loaded_selected\":1,\"pin_files_full\":8,\"pin_files_selected\":1,\"pin_bytes_full\":"<<sourceBytes<<",\"pin_bytes_selected\":"<<(selectedIo.bytesRead-before.bytesRead)<<",\"retained_native_json_bytes_full\":"<<fullNative<<",\"retained_native_json_bytes_selected\":"<<selectedNative<<",\"mesh_capacity_bytes_full\":"<<fullMesh<<",\"mesh_capacity_bytes_selected\":"<<selectedMesh<<",\"reference_cache_bytes_full\":"<<fullReference<<",\"reference_cache_bytes_selected\":"<<referenceBytes(d)<<",\"released_cache_bodies\":7}\n";
    const auto original= native_test::readFile(paths[7]);{std::ofstream out(paths[7],std::ios::binary|std::ios::app);out<<"drift";}
    d.recompute();valid(d);check(inspectAssemblyAuthoring(d,0).find("deferred_external_sources")->num==7,"Unloaded file drift is explicitly deferred without reading that source");
    AgentSession parent;parent.document=std::move(d);V load=json(R"({"commands":[{"op":"assembly.load","roots":null}]})");load.set("expected_revision",V::makeNum(parent.revision));const auto authored=documentToJson(parent.document);const auto failed=parent.call("doc_apply",load);check(failed.find("isError")->b&&failed.dump().find("EXTERNAL_REFERENCE_DRIFT")!=std::string::npos&&documentToJson(parent.document)==authored&&parent.revision==0,"Reload detects source drift and atomically preserves selected document/revision");
    {std::ofstream out(paths[7],std::ios::binary);out<<original;}
    apply(parent,json(R"([{"op":"configuration.set","name":"Scoped native","visibility":{"2":false}}])"));
    V request=json(R"({"action":"checkout","node":1})");request.set("expected_revision",V::makeNum(parent.revision));auto checkout=accepted(parent.call("design_containers",request));AgentSession child;check(documentFromJson(checkout.find("document")->dump(),child.document,&error),error);
    check(child.document.features.size()==1&&child.document.assemblyExternalReferences.arr.size()==1,"Native checkout scopes exact imported source and its pin");
    const auto saved=temporary.path()/"scoped.ark";check(saveDocument(child.document,native_test::pathUtf8(saved).c_str()),"Save native scoped configuration document");Document restored;check(loadDocument(native_test::pathUtf8(saved).c_str(),restored,&error),error);check(canonical(restored.designContainerCheckout)==canonical(child.document.designContainerCheckout)&&canonical(restored.assemblyExternalReferences)==canonical(child.document.assemblyExternalReferences),"Native binary roundtrip preserves v2 manifest and external pin");child.document=std::move(restored);
    V commands=json(R"([{"op":"configuration.apply","name":"Scoped native"},{"op":"assembly.load","roots":[]}])");V pin=child.document.assemblyExternalReferences.arr.front();pin.set("op",V::makeStr("assembly.external.set"));pin.set("source_revision",V::makeStr("fixture-reviewed"));commands.arr.push_back(pin);apply(child,commands);
    V childSource;check(arkjson::parse(documentToJson(child.document,false,false),childSource),"Native child authoring parses");request=json(R"({"action":"merge","preview":true})");request.set("expected_revision",V::makeNum(parent.revision));request.set("document",childSource);const auto preview=accepted(parent.call("design_containers",request));request.set("preview",V::makeBool(false));request.set("expected_child_sha256",*preview.find("child_sha256"));accepted(parent.call("design_containers",request));
    check(!parent.document.assembly[1].visible&&parent.document.assemblyExternalReferences.arr[0].find("source_revision")->str=="fixture-reviewed"&&parent.document.assemblyExternalReferences.arr.size()==8&&parent.document.assemblyLoadSelection.arr.size()==1,"Reviewed native merge preserves outside pins and parent selection while applying scoped recipe");
    apply(parent,json(R"([{"op":"assembly.load","roots":null}])"));valid(parent.document);check(parent.document.evalBodies.size()==8,"Explicit reload restores all exact retained sources after scoped merge");
}
}
int main(){try{fixture();std::cout<<"B.04 native assembly scaling, pin I/O, released buffers and isolated configuration/pin roundtrip passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
