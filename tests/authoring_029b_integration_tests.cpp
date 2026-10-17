#include "ark/agent.hpp"
#include "ark/history_authoring.hpp"
#include "ark/iteration_history.hpp"
#include "native_process.hpp"
#include <fstream>
#include <iostream>

using namespace ark;
using V=arkjson::Value;
namespace {
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
V json(const std::string& text){V value;check(arkjson::parse(text,value),"Fixture JSON parses");return value;}
V accepted(const V& reply){check(reply.find("isError")&&!reply.find("isError")->b,reply.dump());return *reply.find("structuredContent");}
V apply(AgentSession& session,V commands){V args=V::makeObj();args.set("expected_revision",V::makeNum((double)session.revision));args.set("commands",commands);return accepted(session.call("doc_apply",args));}
void rejected(AgentSession& session,const std::string& tool,const V& args,const char* reason){
    const auto before=documentToJson(session.document);const auto revision=session.revision;
    const auto reply=session.call(tool,args);
    check(reply.find("isError")->b&&reply.dump().find(reason)!=std::string::npos,tool+" rejects with "+reason+": "+reply.dump());
    check(session.revision==revision&&documentToJson(session.document)==before,"Rejected operation retains source, journal and revision");
}
void importedCadPins(){
    native_test::TempDirectory temp("ark-029b-native-pin-");const auto source=temp.path()/"producer.step";
    AgentSession producer;apply(producer,json(R"([{"op":"feature.add","name":"Exact producer","type":"native_solid","native":{"op":"box"},"p":{"width":12,"depth":8,"height":5}}])"));
    V args=json(R"({"id":1})");args.set("path",V::makeStr(native_test::pathUtf8(source)));accepted(producer.call("export_step",args));
    AgentSession session;args=json(R"({"mode":"import","expected_revision":0})");args.set("path",V::makeStr(native_test::pathUtf8(source)));
    const auto imported=accepted(session.call("import_cad",args));const auto importedId=*imported.find("created_id");
    const auto* definition=session.document.find((u64)importedId.num);check(definition&&featureOperation(*definition)=="imported_cad","Latest general CAD producer remains authoritative");
    V pin=json(R"({"op":"assembly.external.set","source_revision":"native-fixture-1"})");pin.set("feature",importedId);pin.set("path",V::makeStr(native_test::pathUtf8(source)));pin.set("source_sha256",*definition->nativeSpec.find("package")->find("source_sha256"));
    V commands=V::makeArr();commands.arr.push_back(pin);apply(session,commands);
    check(json(documentToJson(session.document)).find("version")->num==40,"Pin conditionally selects source40 while retaining imported_cad source38");
    Document restored;std::string error;check(documentFromJson(documentToJson(session.document),restored,&error),error);restored.recompute();
    check(restored.evalBodies.size()==1&&featureOperation(restored.features.front())=="imported_cad","Source40 reopen retains the latest exact import producer");
    args=json(R"({"commands":[{"op":"param.set","name":"reviewed","expr":1}]})");args.set("expected_revision",V::makeNum((double)session.revision));const auto preview=accepted(session.call("doc_preview",args));
    check(preview.find("valid")->b&&preview.find("candidate_cached")->b,"Valid source pin supports a retained preview");const auto commit=*preview.find("apply");
    const auto original=native_test::readFile(source);{std::ofstream stream(source,std::ios::binary|std::ios::app);stream<<"\n ";}
    args=V::makeObj();args.set("id",importedId);
    rejected(session,"native_inspect",args,"EXTERNAL_REFERENCE_DRIFT");
    rejected(session,"collision_inspect",json(R"({"a":1,"b":1})"),"EXTERNAL_REFERENCE_DRIFT");
    rejected(session,"doc_apply",commit,"EXTERNAL_REFERENCE_DRIFT");
    {std::ofstream stream(source,std::ios::binary|std::ios::trunc);stream<<original;}
    accepted(session.call("doc_apply",commit));check(session.document.getParam("reviewed")==1,"Restored exact source permits the retained revision-checked commit");
    const auto moved=temp.path()/"producer.hidden";std::filesystem::rename(source,moved);
    rejected(session,"native_inspect",args,"EXTERNAL_REFERENCE_MISSING");std::filesystem::rename(moved,source);
    commands=json(R"([{"op":"assembly.external.remove","feature":1}])");apply(session,commands);
    std::filesystem::remove(source);accepted(session.call("native_inspect",args));check(session.document.evalBodies.size()==1,"Explicit unpin retains embedded exact geometry without claiming a live reference");
}
void unloadedNamedHistory(){
    AgentSession session;session.suppressFailureCapture();
    apply(session,json(R"([{"op":"feature.add","type":"curve","name":"Lower","native":{"start":[0,0,0],"segments":[{"kind":"line","to":[10,0,0]},{"kind":"line","to":[10,10,0]},{"kind":"line","to":[0,10,0]},{"kind":"line","to":[0,0,0]}]}},{"op":"feature.add","type":"curve","name":"Upper","native":{"start":[0,0,10],"segments":[{"kind":"line","to":[10,0,10]},{"kind":"line","to":[10,10,10]},{"kind":"line","to":[0,10,10]},{"kind":"line","to":[0,0,10]}]}},{"op":"feature.add","type":"loft","name":"Named-source loft","native":{"sections":["Lower","Upper"]},"p":{"solid":1,"ruled":1}},{"op":"feature.add","type":"box","name":"Loaded independent box","p":{"dx":2,"dy":2,"dz":2}},{"op":"assembly.add","name":"Loaded only","feature":4}])"));
    apply(session,json(R"([{"op":"assembly.load","roots":[1]}])"));check(session.document.evalBodies.size()==1&&session.document.evalBodies[0].id==4,"Selective fixture genuinely excludes the named-source loft and its curves");
    V args=json(R"({"commands":[{"op":"feature.move","id":3,"delta":-2}]})");args.set("expected_revision",V::makeNum((double)session.revision));rejected(session,"doc_apply",args,"Forward or cyclic associative feature dependency");
    check(session.document.features[0].id==1&&session.document.features[1].id==2&&session.document.features[2].id==3,"Rejected inactive reorder retains original source order");
    apply(session,json(R"([{"op":"assembly.load","roots":null}])"));check(session.document.evalBodies.size()==4,"Loading all definitions remains valid after the rejected reorder");for(const auto& result:session.document.lastResults)check(result.ok,"Named loft and curve sources recompute after rejection");
}
void discoveryAndHistory(){
    const auto tools=AgentSession::tools();check(tools.arr.size()==97,"029 tools plus the read-only Wood cutlist remain discoverable");
    const auto schema=tools.dump();check(schema.find("configuration.apply")!=std::string::npos&&schema.find("configuration.activate")==std::string::npos,"Discovery exposes executable configuration operations");
    AgentSession session;for(const char* topic:{"history","configurations","drawings","stamps","planes","cad_import","containers"}){V args=V::makeObj();args.set("topic",V::makeStr(topic));accepted(session.call("agent_onboard",args));}
    apply(session,json(R"([{"op":"feature.add","type":"box","p":{"dx":10,"dy":8,"dz":5}},{"op":"feature.add","type":"box","p":{"dx":3,"dy":2,"dz":1}},{"op":"feature.rollback","id":1}])"));
    for(const char* tool:{"import_cad","import_mesh","library_insert","import_step","import_sheet_step"})rejected(session,tool,V::makeObj(),"Resume full feature history");
    // 028 face-source extrusion consumes a geometric reference without hiding
    // its source body. The shared active-output helper must preserve that rule.
    Document doc;Feature base;base.type=FeatureType::Box;doc.addFeature(base);Feature extrude;extrude.type=FeatureType::NativeSolid;extrude.nativeSpec=json(R"({"op":"extrude"})");extrude.inputs={1};doc.addFeature(extrude);
    check(!consumedFeatureInputs(doc).count(1),"Reference-source extrusion retains its028 body-consumption semantics");
}
}
int main(){try{discoveryAndHistory();importedCadPins();unloadedNamedHistory();std::cout<<"029B latest-producer pins, cached publication, source40, tool discovery and retained named history boundaries passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
