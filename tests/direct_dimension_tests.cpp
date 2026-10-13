#include "ark/agent.hpp"
#include "ark/references.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ark;using V=arkjson::Value;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static V parse(const char* text){V value;check(arkjson::parse(text,value),"test JSON");return value;}
static void apply(AgentSession& session,const char* commands){auto args=V::makeObj();args.set("expected_revision",V::makeNum((double)session.revision));args.set("commands",parse(commands));auto result=session.call("doc_apply",args);if(result.find("isError")->b)throw std::runtime_error(result.find("structuredContent")->dump());}
static double volume(const Document& document){check(document.evalBodies.size()==1,"exact extrusion output count");const auto& data=document.evalBodies[0].nativeData;check(data.find("brep_base64")&&data.find("report"),"exact B-rep custody");return data.find("report")->find("volume_mm3")->num;}
int main(){try{
    AgentSession dimensions;dimensions.suppressIterationHistory();dimensions.suppressFailureCapture();
    auto onboard=dimensions.call("agent_onboard",parse(R"JSON({"topic":"sketch"})JSON"));auto create=*onboard.find("structuredContent")->find("dimension_create_example");check(!dimensions.call("doc_apply",create).find("isError")->b,"onboarding adds a driving sketch dimension at the observed revision");
    check(dimensions.document.findSketch("Dimensioned circle")->sk.ents[0].dof[2]==7,"named sketch radius is evaluated");onboard=dimensions.call("agent_onboard",parse(R"JSON({"topic":"sketch"})JSON"));auto edit=*onboard.find("structuredContent")->find("dimension_edit_example");check(!dimensions.call("doc_apply",edit).find("isError")->b&&dimensions.document.findSketch("Dimensioned circle")->sk.ents[0].dof[2]==9,"refreshed onboarding edits an existing driving dimension");
    AgentSession session;session.suppressIterationHistory();session.suppressFailureCapture();
    apply(session,R"JSON([{"op":"param.set","name":"R","expr":"7"},{"op":"param.set","name":"D","expr":"20"},{"op":"sketch.set","name":"Profile","sketch":{"plane":"XZ","entities":[{"kind":"circle","name":"ring","dof":[0,0,"R"]}],"constraints":[]}},{"op":"feature.add","type":"native_solid","name":"Editable extrusion","p":{"op":"extrude","sketch":"Profile","depth":"D"}}])JSON");
    check(std::abs(volume(session.document)-K_PI*49*20)<1e-5,"initial extrusion volume from source radius and depth");
    apply(session,R"JSON([{"op":"feature.edit","id":1,"p":{"depth":"(D)+(5)"}}])JSON");
    check(std::abs(volume(session.document)-K_PI*49*25)<1e-5,"cap drag recipe recomputes exact volume");
    check(session.document.find(1)->get("depth")=="(D)+(5)"&&session.document.find(1)->get("sketch")=="Profile","source profile and depth expression retained");
    apply(session,R"JSON([{"op":"param.set","name":"D","expr":"30"},{"op":"param.set","name":"R","expr":"9"}])JSON");
    check(std::abs(volume(session.document)-K_PI*81*35)<1e-5,"agent radius and depth edits recompute exact geometry");
    const auto start=resolveGeometryReference(session.document,1,"cap.start",.5),end=resolveGeometryReference(session.document,1,"cap.end",.5);check(std::abs(start.frame.m[13])<1e-8&&std::abs(end.frame.m[13]+35)<1e-8,"exact semantic caps follow XZ source normal with retained start anchor");
    const auto revision=session.revision;const auto saved=documentToJson(session.document,false,false);
    auto bad=parse(R"JSON({"commands":[{"op":"feature.edit","id":1,"p":{"depth":"-1"}}]})JSON");bad.set("expected_revision",V::makeNum((double)revision));check(session.call("doc_apply",bad).find("isError")->b&&session.revision==revision&&documentToJson(session.document,false,false)==saved,"negative edited depth rejects without partial publication");
    bad.set("expected_revision",V::makeNum((double)(revision-1)));check(session.call("doc_apply",bad).find("isError")->b&&session.revision==revision&&documentToJson(session.document,false,false)==saved,"stale dimension recipe rejects without rebase");
    Document restored;std::string error;check(documentFromJson(saved,restored,&error),"source dimension roundtrip load");restored.recompute();check(restored.find(1)->get("depth")=="(D)+(5)"&&std::abs(volume(restored)-K_PI*81*35)<1e-5,"roundtrip preserves expressions and exact geometry");
    std::cout<<"Direct dimensions: exact XZ extrusion, cap displacement, agent source edits, rejected negative/stale writes, persistence passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
