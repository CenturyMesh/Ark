#include "ark/agent.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ark;
using V=arkjson::Value;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
V json(const char* source){V value;check(arkjson::parse(source,value),"test JSON");return value;}
V accepted(V reply){if(reply.find("isError")->b)throw std::runtime_error(reply.find("structuredContent")->dump());return *reply.find("structuredContent");}
const V& instance(const V& report,const char* name){for(const auto& entry:report.find("instances")->arr)if(entry.find("name")->str==name)return entry;throw std::runtime_error("Missing instance");}
Mat4 matrix(const V& report,const char* name){Mat4 out;const auto& values=instance(report,name).find("world_column_major")->arr;for(size_t i=0;i<16;++i)out.m[i]=values[i].num;return out;}
V apply(AgentSession& session,const char* commands){V args=json(commands);args.set("expected_revision",V::makeNum((double)session.revision));return accepted(session.call("doc_apply",args));}
void unchanged(AgentSession& session,const V& reply,const std::string& before,u64 revision,const char* reason){check(reply.find("isError")->b&&session.revision==revision&&documentToJson(session.document)==before,reason);}
}
int main(){try{
    AgentSession session;
    apply(session,R"({"commands":[
      {"op":"feature.add","type":"box","name":"Fixture body","p":{"dx":10,"dy":2,"dz":2}},
      {"op":"assembly.add","name":"Frame","feature":"Fixture body","fixed":true},
      {"op":"assembly.add","name":"Fork","feature":"Fixture body"},
      {"op":"assembly.add","name":"Handlebar","feature":"Fixture body"},
      {"op":"assembly.add","name":"Wheel","feature":"Fixture body"},
      {"op":"mate.add","a":"Frame","b":"Fork","type":"revolute","reference_a":"Z","reference_b":"Z"},
      {"op":"mate.add","a":"Fork","b":"Handlebar","type":"rigid","reference_a":"origin","reference_b":"origin"},
      {"op":"mate.add","a":"Fork","b":"Wheel","type":"revolute","reference_a":"X","reference_b":"X"}
    ]})");
    const auto baseline=documentToJson(session.document);const u64 revision=session.revision;
    check(json(documentToJson(session.document,true,false).c_str()).find("version")->num==29,"new joint types select document version29");
    const auto preview=accepted(session.call("assembly_info",json(R"({"drive":{"driver":"Handlebar","delta":30}})")));
    const auto& drive=*preview.find("drive");check(session.revision==revision&&documentToJson(session.document)==baseline,"drive query is isolated");
    check(drive.find("document_unchanged")->b&&drive.find("joint")->find("moving_nodes")->arr.size()==3,"steering connected side includes fork, handlebar and wheel");
    check(matrix(drive,"Fork").transformDir({1,0,0}).approx({std::cos(K_PI/6),.5,0},1e-6),"preview steering angle");
    check(matrix(drive,"Wheel").transformDir({0,1,0}).approx(matrix(drive,"Fork").transformDir({0,1,0}),1e-6),"passive wheel spin stays unchanged");
    unchanged(session,session.call("doc_apply",json(R"({"commands":[{"op":"assembly.drive","driver":"Handlebar","delta":30}]})")),baseline,revision,"drive without revision rejects");
    unchanged(session,session.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"assembly.drive","driver":"Handlebar","delta":30}]})")),baseline,revision,"stale drive rejects");
    unchanged(session,session.call("doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"bad_marker","expr":1},{"op":"assembly.drive","driver":"Frame","delta":30}]})")),baseline,revision,"failed drive rolls back preceding command");
    unchanged(session,session.call("assembly_info",json(R"({"drive":{"driver":"Handlebar","delta":30,"mode":"free"}})")),baseline,revision,"unsupported drive mode rejects");
    unchanged(session,session.call("assembly_info",json(R"({"drive":{"driver":"Handlebar","delta":30,"ignored":true}})")),baseline,revision,"unknown drive fields reject");
    V recipe=json(R"({"expected_revision":1,"commands":[{"op":"assembly.drive","driver":"Handlebar","delta":30}]})");
    auto candidate=accepted(session.call("doc_preview",recipe));check(candidate.find("valid")->b&&session.revision==revision&&documentToJson(session.document)==baseline,"doc_preview drive caches without mutation");
    V commit=V::makeObj();commit.set("expected_revision",V::makeNum(revision));commit.set("candidate_id",*candidate.find("candidate_id"));accepted(session.call("doc_apply",commit));
    const auto after=accepted(session.call("assembly_info",V::makeObj()));check(matrix(after,"Handlebar").transformDir({1,0,0}).approx(matrix(drive,"Handlebar").transformDir({1,0,0}),1e-6),"cached commit equals preview");
    Document loaded;std::string error;check(documentFromJson(documentToJson(session.document),loaded,&error),"version29 joint poses and journal reload");
    AgentSession restored;restored.document=std::move(loaded);auto reload=accepted(restored.call("assembly_info",V::makeObj()));check(matrix(reload,"Wheel").transformDir({0,1,0}).approx(matrix(after,"Wheel").transformDir({0,1,0}),1e-6),"reload preserves steered wheel pose");
    V old=json(documentToJson(session.document,true,false).c_str());old.set("version",V::makeNum(28));const auto kept=documentToJson(restored.document);check(!documentFromJson(old.dump(),restored.document,&error)&&documentToJson(restored.document)==kept,"new types in old version reject without replacing destination");
    accepted(session.call("doc_undo",V::makeObj()));const auto undo=accepted(session.call("assembly_info",V::makeObj()));check(matrix(undo,"Fork").transformDir({1,0,0}).approx({1,0,0},1e-6),"one undo restores joint pose");
    accepted(session.call("doc_redo",V::makeObj()));const auto redo=accepted(session.call("assembly_info",V::makeObj()));check(matrix(redo,"Fork").transformDir({1,0,0}).approx(matrix(after,"Fork").transformDir({1,0,0}),1e-6),"redo restores pose");
    apply(session,R"({"commands":[{"op":"assembly.drive","driver":"Wheel","mate":3,"delta":25}]})");const auto spin=accepted(session.call("assembly_info",V::makeObj()));check(matrix(spin,"Fork").transformDir({0,1,0}).approx(matrix(after,"Fork").transformDir({0,1,0}),1e-6)&&!matrix(spin,"Wheel").transformDir({0,1,0}).approx(matrix(after,"Wheel").transformDir({0,1,0}),1e-3),"explicit axle drive spins only wheel");
    apply(session,R"({"commands":[{"op":"feature.edit","id":"Fixture body","appearance":{"opacity":0.25}}]})");check(resolvedAppearance(session.document,session.document.features[0]).opacity==.25,"transparency commits through existing feature contract");
    const auto appearance=accepted(session.call("agent_onboard",json(R"({"topic":"materials"})")));check(appearance.dump().find("Interior inspection")!=std::string::npos,"agents discover opacity scope");
    const auto overview=accepted(session.call("agent_onboard",V::makeObj()));check(overview.dump().find("assembly_info.drive")!=std::string::npos,"compact overview dispatches manipulation");
    const auto tools=AgentSession::tools();check(tools.arr.size()==85&&tools.dump().find("assembly.drive")!=std::string::npos&&tools.dump().find("\"drive\"")!=std::string::npos,"compact schema advertises command and preview with stable tool count");
    std::cout<<"assembly manipulation tool contracts passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
