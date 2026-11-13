#include "ark/assistant.hpp"
#include "ark/assistant_onboarding.hpp"
#include "ark/agent.hpp"
#include "ark/onboarding.hpp"
#include "ark/serialize.hpp"
#include <iostream>
#include <stdexcept>
using V=arkjson::Value;
static unsigned checks=0;
static void check(bool okay,const char* why){++checks;if(!okay)throw std::runtime_error(why);}
static const V& field(const V& value,const char* key){const auto* found=value.find(key);if(!found)throw std::runtime_error(std::string("Missing ")+key);return *found;}
static const V& tool(const V& tools,const char* name){for(const auto& item:tools.arr)if(field(item,"name").str==name)return item;throw std::runtime_error(name);}
static void contains(const std::string& value,const char* token){check(value.find(token)!=std::string::npos,token);}
static V parse(const char* text){V value;check(arkjson::parse(text,value),"Conversation fixture");return value;}
static void refreshContracts(){
    auto history=parse(R"([{"role":"developer","content":"ARK_PREBAKED_GUIDE_V1\nold static guide"},{"role":"user","content":"Keep my model and this message"},{"type":"function_call","call_id":"kept-call","name":"doc_info","arguments":"{}"},{"type":"function_call_output","call_id":"kept-call","output":"original receipt"},{"role":"developer","content":"Unrelated caller instruction"},{"role":"developer","content":"ARK_PREBAKED_GUIDE_V1\nduplicate old guide"}])");
    const auto original=history;ark::refreshAssistantOnboarding075D(history,"current guide");check(history.arr.size()==5,"Old static guides not deduplicated");check(field(history.arr.front(),"content").str=="ARK_PREBAKED_GUIDE_V1\ncurrent guide","Existing chat retained old guide");for(size_t i=1;i<5;++i)check(history.arr[i].dump()==original.arr[i].dump(),"User/tool/other developer evidence changed");const auto once=history.dump();ark::refreshAssistantOnboarding075D(history,"current guide");check(history.dump()==once,"Unchanged guide caused repeated churn");ark::refreshAssistantOnboarding075D(history,"");check(history.dump()==once,"Empty configuration removed existing content");
    auto user=parse(R"([{"role":"user","content":"ARK_PREBAKED_GUIDE_V1\nmy quoted text"}])");const auto quoted=user.arr[0].dump();ark::refreshAssistantOnboarding075D(user,"current guide");check(user.arr.size()==2&&user.arr[1].dump()==quoted,"User quote mistaken for a host guide");
    ark::AssistantConfig config;config.model="offline-guide-refresh";config.onboardingPack=ark::agentAuthoringOnboardingPack();auto input=parse(R"([{"role":"developer","content":"ARK_PREBAKED_GUIDE_V1\nOBSOLETE_GUIDE_FIXTURE"},{"role":"user","content":"Review only, no edits."}])");const auto retained=input.dump();unsigned requests=0;
    auto result=ark::runAssistant(config,input,ark::AgentSession::tools(),{}, {},nullptr,[&](const auto&,const V& request,const auto*){++requests;check(request.dump().find("OBSOLETE_GUIDE_FIXTURE")==std::string::npos,"Provider received stale static guide");contains(request.dump(),"geometry_extensions");contains(request.dump(),"body_select");return parse(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Reviewed current contracts."}]}]})");});check(requests==1&&result.performance.toolCalls==0&&input.dump()==retained,"Guide refresh made a write/tool call or altered caller history");
}
int main(){try{refreshContracts();
    ark::AgentSession session;const auto before=ark::documentToJson(session.document);
    for(const char* topic:{"overview","sketch","solids","surfacing","assembly","automation"})for(const char* detail:{"guide","full"}){
        V args=V::makeObj();args.set("topic",V::makeStr(topic));args.set("detail",V::makeStr(detail));const auto response=session.call("agent_onboard",args);check(!field(response,"isError").b,"Onboarding rejected");const auto& guide=field(response,"structuredContent");const auto& extensions=field(guide,"geometry_extensions");check(field(extensions,"source_version").num==59,"Geometry source format drift");
        const auto text=extensions.dump();for(const char* token:{"arrangement","body_select","reference_anchor","radius_domain","boolean_mode","exact_compatible","seam_parameter","expected_revision","candidate_id"})contains(text,token);
        check(field(guide,"document_state_included").b==(std::string(detail)=="full"),"Static guide misrepresents live state");
        check(guide.dump().find("does not check offset self-intersections")==std::string::npos,"Obsolete offset rejection remains");
        check(guide.dump().find("Unsupported rational/trimmed/periodic or incompatible basis cases reject")==std::string::npos,"Unqualified old surface boundary remains");
    }
    V interactive;check(arkjson::parse(ark::agentInteractiveOnboardingPack(),interactive),"Single-agent pack is not valid JSON");contains(field(interactive,"interaction").str,"needs_input:");check(field(interactive,"topics").arr.size()==3,"Interactive pack lost preloaded topics");
    const auto advertised=ark::assistantDeferredCatalogue(ark::AgentSession::tools(),V::makeArr(),ark::agentInteractiveOnboardingPack());check(!tool(advertised,"sketch_repair").find("ark_deferred"),"Single-agent bootstrap lost preloaded sketch tools");const auto without=ark::assistantDeferredCatalogue(ark::AgentSession::tools(),V::makeArr(),"");check(tool(without,"sketch_repair").find("ark_deferred"),"Fixture must distinguish preloaded specialist from default tools");
    V pack;check(arkjson::parse(ark::agentAuthoringOnboardingPack(),pack),"Preloaded pack parses");check(field(pack,"topics").arr.size()==3,"Unexpected preloaded topic expansion");for(const auto& guide:field(pack,"topics").arr){check(guide.find("geometry_extensions"),"Preloaded guide lost current extensions");check(!guide.find("document"),"Preloaded guide retains false live state");}check(pack.dump().find("\"expected_revision\":")==std::string::npos,"Preload retains executable revision zero");
    for(bool detailed:{false,true}){const auto tools=ark::AgentSession::tools(detailed);const auto& surface=field(field(tool(tools,"surface_control"),"inputSchema"),"properties");for(const char* name:{"basis_mode","u_closed","v_closed","method","controls"})check(surface.find(name),"Current surface parameter absent from schema");contains(surface.dump(),"exact_compatible");contains(surface.dump(),"joined");
        const auto& sketch=field(field(tool(tools,"sketch_inspect"),"inputSchema"),"properties");contains(field(sketch,"region_mode").dump(),"arrangement");const auto& info=field(field(tool(tools,"doc_info"),"inputSchema"),"properties");contains(field(info,"detail").dump(),"evaluation");}
    check(session.revision==0&&ark::documentToJson(session.document)==before,"Onboarding audit mutated the drawing");std::cout<<"Onboarding current075D: "<<checks<<" guides, preload, schema and no-mutation checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
