#include "ark/agent.hpp"
#include "ark/onboarding.hpp"
#include "ark/holes_orchestrator.hpp"
#include "ark/assistant.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace ark;
using V=arkjson::Value;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static V json(const char* input){V out;check(arkjson::parse(input,out),"fixture JSON");return out;}
static const V& tool(const V& tools,const std::string& name){for(const auto& item:tools.arr)if(item.find("name")->str==name)return item;throw std::runtime_error("Missing tool "+name);}
int main(){try{
    AgentSession session;const auto baseline=documentToJson(session.document);const auto tools=AgentSession::tools(true);
    check(tools.arr.size()==108,"Unexpected discovered tool count");for(const char* name:{"holes_catalog","cad_inspect","import_cad","design_containers"})check(tool(tools,name).find("inputSchema"),"Undiscoverable authoring tool");
    const auto& apply=*tool(tools,"doc_apply").find("inputSchema")->find("properties")->find("commands")->find("items");const auto& preview=*tool(tools,"doc_preview").find("inputSchema")->find("properties")->find("commands")->find("items");check(apply.dump()==preview.dump(),"Preview/apply recipes diverged");check(apply.find("properties")->find("p_remove"),"Inactive feature override removal absent");
    for(const char* topic:{"overview","sketch","holes","cad_import","containers"}){V args=V::makeObj();args.set("topic",V::makeStr(topic));auto guide=agentOnboard(session,args);check(guide.find("functional_holes")&&guide.find("functional_holes")->str.find("Holes Orchestrator")!=std::string::npos,"Functional-hole policy absent");check(guide.find("drawing_authorization")&&guide.find("drawing_authorization")->str.find("explicitly requests")!=std::string::npos,"Drawing opt-in policy lost");}
    auto bad=session.call("import_cad",json(R"({"path":"missing.step","expected_revision":1})"));check(bad.find("isError")->b,"Stale CAD import accepted");
    for(const char* input:{R"({"id":-1})",R"({"id":9007199254740992})",R"({"path":"missing.step","compare_id":1})"})check(session.call("cad_inspect",json(input)).find("isError")->b,"Invalid source selector accepted");
    const auto started=std::chrono::steady_clock::now();for(int i=0;i<1000;++i){auto row=holesCatalog(json(R"({"family":"metric","size":"M6"})"));check(row.t==V::Obj,"Local catalog lookup failed");}const auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    check(session.revision==0&&documentToJson(session.document)==baseline,"Read-only contract checks mutated source");
    AssistantConfig config;config.model="offline-023b-contract";bool observed=false;auto result=runAssistant(config,V::makeStr("Create a model with functional holes"),tools,[](const auto&,const auto&){throw std::runtime_error("No real provider calls allowed");return V{};},{},nullptr,[&](const auto&,const V& request,auto*){auto* instruction=request.find("instructions");check(instruction&&instruction->str.find("Holes Orchestrator")!=std::string::npos,"Runtime policy lacks semantic holes");observed=true;return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Offline contract only."}]}]})");});check(observed&&!result.text.empty(),"Assistant policy not executed");
#ifdef ARK_023B_NATIVE_TESTS
    auto guide=agentOnboard(session,json(R"({"topic":"holes"})"));V args=*guide.find("example_functional_holes");args.set("expected_revision",V::makeNum(0));auto authored=session.call("doc_apply",args);check(!authored.find("isError")->b,authored.find("structuredContent")->dump().c_str());check(session.document.drawings.empty()&&session.document.features.size()==2,"Functional-hole example did not create a semantic tree");V saved;check(arkjson::parse(documentToJson(session.document),saved)&&saved.find("version")->num==38,"New recipe format missing");
#endif
    std::cout<<"023B contracts passed; tools="<<tools.arr.size()<<"; 1000 local M6 lookups_ms="<<milliseconds<<" (synthetic lookup only, not geometry or interaction latency)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
