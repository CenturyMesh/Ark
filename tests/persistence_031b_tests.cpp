#include "ark/agent.hpp"
#include "ark/document_container.hpp"
#include "ark/mesh_model.hpp"
#include "ark/serialize.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace {
using V=arkjson::Value;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
V json(const char* text){V value;check(arkjson::parse(text,value),"Fixture JSON parses");return value;}
V accepted(const V& result){check(result.find("isError")&&!result.find("isError")->b,result.dump().c_str());return *result.find("structuredContent");}
struct TempFile {std::filesystem::path path=std::filesystem::temp_directory_path()/("ark-031b-persistence-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ark");~TempFile(){std::error_code error;std::filesystem::remove(path,error);}};
}
int main(){try{
    ark::AgentSession session;session.suppressFailureCapture();
    accepted(session.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"mesh_body","name":"Source","mesh":{"positions":[0,0,0,10,0,0,10,10,0,0,10,0],"indices":[0,1,2,0,2,3]}}]})")));
    const auto fingerprint=ark::meshTopologyFingerprint(*session.document.evalBodies.front().baked);
    auto command=json(R"({"op":"feature.add","type":"mesh_edit","name":"Refined","inputs":[1],"native":{"op":"remesh"},"p":{"target_length":6,"iterations":2}})");
    auto native=*command.find("native");native.set("source_topology",V::makeStr(fingerprint));command.set("native",native);
    V args=V::makeObj(),commands=V::makeArr();commands.arr.push_back(command);args.set("commands",commands);args.set("expected_revision",V::makeNum(session.revision));accepted(session.call("doc_apply",args));
    const auto canonical=ark::documentToJson(session.document);check(json(canonical.c_str()).find("version")->num==43,"Advanced retained recipe selects43");
    const auto triangles=session.document.evalBodies.back().baked->triCount();check(triangles>2,"Fixture actually refines geometry");
    TempFile file;const auto utf=file.path.u8string();const std::string destination(reinterpret_cast<const char*>(utf.data()),utf.size());
    check(ark::saveDocument(session.document,destination.c_str()),"Binary source43 save succeeds");check(ark::documentContainerFile(destination.c_str()),"Native file retains binary container schema");
    ark::AgentSession restored;restored.suppressFailureCapture();std::string error;check(ark::loadDocument(destination.c_str(),restored.document,&error),error.c_str());
    check(ark::documentToJson(restored.document)==canonical,"Binary roundtrip preserves recipe, source43 and journal");restored.document.recompute();check(restored.document.evalBodies.back().baked->triCount()==triangles,"Binary reload recomputes same retained geometry");
    accepted(restored.call("doc_undo",V::makeObj()));check(restored.document.features.size()==1,"Persistent undo removes only derived recipe");
    accepted(restored.call("doc_redo",V::makeObj()));check(restored.document.features.size()==2&&restored.document.evalBodies.back().baked->triCount()==triangles,"Persistent redo restores advanced recipe and geometry");
    auto downgrade=json(ark::documentToJson(session.document,false,false).c_str());downgrade.set("version",V::makeNum(42));const auto before=ark::documentToJson(restored.document);check(!ark::documentFromJson(downgrade.dump(),restored.document,&error)&&ark::documentToJson(restored.document)==before,"Source43 downgrade rejects atomically");
    ark::Document legacy;ark::Feature primitive;primitive.type=ark::FeatureType::Box;primitive.name="Legacy";legacy.addFeature(primitive);check(json(ark::documentToJson(legacy,false,false).c_str()).find("version")->num==1,"Ordinary documents do not migrate with runtime identity");
    std::cout<<"031B binary persistence, conditional source43, journal undo/redo and downgrade custody passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
