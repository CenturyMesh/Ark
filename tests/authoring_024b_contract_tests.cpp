#include "ark/agent.hpp"
#include "ark/onboarding.hpp"
#include "ark/serialize.hpp"
#include "ark/iteration_history.hpp"
#include <iostream>
#include <stdexcept>
using namespace ark;using V=arkjson::Value;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static V json(const char* text){V v;if(!arkjson::parse(text,v))throw std::runtime_error("Fixture JSON");return v;}
static V call(AgentSession& s,const char* tool,V args){auto result=s.call(tool,args);if(result.find("isError")->b)throw std::runtime_error(result.find("structuredContent")->dump());return *result.find("structuredContent");}
int main(){try{
    AgentSession session;const auto tools=AgentSession::tools();check(tools.arr.size()==110,"Expected 110 discoverable tools");
    for(const char* topic:{"stamps","planes"})check(!call(session,"agent_onboard",json((std::string("{\"topic\":\"")+topic+"\"}").c_str())).find("rules")->arr.empty(),"Missing specialized onboarding");
    const auto baseline=documentToJson(session.document);auto preview=call(session,"plane_authoring",json(R"({"action":"create_plane","name":"Elevated","parent":"XY","p":{"oz":"12"},"expected_revision":0,"preview":true})"));check(preview.find("commands")&&!preview.find("applied")->b&&session.revision==0&&documentToJson(session.document)==baseline,"Plane preview must be concrete and immutable");
    call(session,"plane_authoring",json(R"({"action":"create_plane","name":"Elevated","parent":"XY","p":{"oz":"12"},"expected_revision":0})"));check(session.revision==1,"Plane command must atomically commit once");
    {std::string error;check(session.document.iterationHistory.t==V::Obj&&iterationValidate(session.document,session.document.iterationHistory,error),"Plane wrapper must append a valid persistent history");AgentSession restored;check(documentFromJson(documentToJson(session.document),restored.document,&error),"Plane history must reload");auto hasPlane=[&](){for(const auto& plane:restored.document.planes)if(plane.name=="Elevated")return true;return false;};call(restored,"doc_undo",json("{}"));check(!hasPlane(),"Reloaded plane history must undo the plane");call(restored,"doc_redo",json("{}"));check(hasPlane(),"Reloaded plane history must redo the plane");}
    auto stale=session.call("plane_authoring",json(R"({"action":"offset_plane","name":"Bad","plane":"XY","offset":4,"expected_revision":0})"));check(stale.find("isError")->b&&session.revision==1,"Stale plane tool must reject");
    auto images=call(session,"image_asset",json(R"({"action":"inspect"})"));check(images.find("assets")->arr.empty()&&session.revision==1,"Asset metadata inspect must not mutate");
    auto missing=session.call("image_asset",json(R"({"action":"import","name":"Logo","path":"does-not-exist.png","expected_revision":1})"));check(missing.find("isError")->b&&session.revision==1,"Missing image must not partially publish");
#ifdef ARK_AUTHORING_024B_NATIVE_TESTS
    call(session,"doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"feature.add","type":"native_solid","name":"Box","native":{"op":"box"},"p":{"width":20,"depth":30,"height":10}}]})"));
    const auto full=call(session,"native_inspect",json(R"({"id":1})"));const auto compact=call(session,"native_inspect",json(R"({"id":1,"detail":"report"})"));
    check(full.find("topology")&&!compact.find("topology")&&!compact.find("topology_included")->b,"Compact inspection must omit topology explicitly");check(full.find("report")->dump()==compact.find("report")->dump(),"Compact inspection must retain identical measured report");check(compact.dump().size()<full.dump().size(),"Compact report should reduce equivalent payload");
    std::cout<<"compact_inspect_full_bytes="<<full.dump().size()<<" compact_bytes="<<compact.dump().size()<<'\n';
    const auto rejected=session.call("native_inspect",json(R"({"id":1,"detail":"unknown"})"));check(rejected.find("isError")->b,"Unknown detail must explicitly reject");
#endif
    std::cout<<"PASS: 024B discovery, onboarding, plane tool preview/stale, image failure atomicity and compact inspection\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
