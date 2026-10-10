#include "ark/assistant.hpp"
#include "ark/agent.hpp"
#include "ark/chatgpt_auth.hpp"
#include <iostream>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>
using V=arkjson::Value;
static int checks=0;
static void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
static V json(const std::string& s){V v;if(!arkjson::parse(s,v))throw std::runtime_error("bad test JSON");return v;}
template<class F>static void fails(F action,const char* why){bool caught=false;try{action();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
static V toolResponse(const char* id="call_1",const char* name="doc_info",const char* args="{}"){
    V response=json(R"({"status":"completed","output":[{"type":"reasoning","id":"rs_1","summary":[],"encrypted_content":"opaque-reasoning"}]})");
    V call=V::makeObj();call.set("type",V::makeStr("function_call"));call.set("call_id",V::makeStr(id));call.set("name",V::makeStr(name));call.set("arguments",V::makeStr(args));
    V output=*response.find("output");output.arr.push_back(call);response.set("output",output);return response;
}
static V finalResponse(){return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Done."}]}]})");}
int main(int argc,char** argv){try{
    ark::AssistantConfig config;config.model="test-model";
    V tools=json(R"([{"name":"doc_info","description":"Read document","inputSchema":{"type":"object"}}])");
    if(argc==3&&(std::string(argv[1])=="--local-http"||std::string(argv[1])=="--cancel-local-http")){
        config.endpoint=argv[2];config.apiKeyEnv="";config.timeoutSeconds=2;
        std::atomic_bool cancel=false;bool doCancel=std::string(argv[1])=="--cancel-local-http";
        std::jthread trigger([&]{if(doCancel){std::this_thread::sleep_for(std::chrono::milliseconds(150));cancel=true;}});
        auto result=ark::runAssistant(config,V::makeStr("offline transport test"),tools,[](auto&,auto&){return V::makeObj();},{},&cancel);
        std::cout<<result.text<<'\n';return 0;
    }
    auto converted=ark::assistantFunctionTools(tools);
    check(converted.arr.size()==1,"tool count");check(converted.arr[0].find("strict")->b==false,"non-strict extensible geometry schema");
    check(converted.arr[0].find("parameters")->dump()==tools.arr[0].find("inputSchema")->dump(),"preserve schema");
    V conversation=json(R"([{"role":"user","content":"Inspect"}])");
    auto request=ark::assistantRequest(config,conversation,tools);
    check(!request.find("store")->b,"no stored response");check(!request.find("parallel_tool_calls")->b,"serial tools");
    check(request.find("include")->arr[0].str=="reasoning.encrypted_content","request portable reasoning");
    check(request.find("instructions")->str.find("agent_onboard")!=std::string::npos,"onboarding instruction");
    check(request.dump().find("OPENAI_API_KEY")==std::string::npos,"credential configuration excluded from request");
    check(request.find("reasoning")==nullptr,"provider default reasoning is omitted");
    auto plan=config;plan.authMode=ark::AssistantAuthMode::ChatGptPlan;plan.model="gpt-6.1-sol";plan.reasoningEffort="low";
    auto planRequest=ark::assistantRequest(plan,conversation,tools);check(planRequest.find("reasoning")->find("effort")->str=="low","explicit reasoning effort is sent");
    check(planRequest.find("tools")->arr.size()==1&&planRequest.find("tools")->arr[0].find("type")->str=="namespace","ChatGPT tools are namespaced");
    plan.reasoningEffort="none";check(ark::assistantRequest(plan,conversation,tools).find("reasoning")->find("effort")->str=="none","plan model-effort support is deferred to account metadata");
    plan.reasoningEffort="ultra";ark::validateAssistantConfig(plan);check(ark::assistantRequest(plan,conversation,tools).find("reasoning")->find("effort")->str=="ultra","account-advertised plan ultra passes through when explicit");
    auto apiEffort=config;apiEffort.model="gpt-6.1-sol";apiEffort.reasoningEffort="none";fails([&]{ark::validateAssistantConfig(apiEffort);},"API-key known model rejects unsupported none effort");
    apiEffort.reasoningEffort="ultra";fails([&]{ark::validateAssistantConfig(apiEffort);},"API-key route rejects nonpublic ultra effort");
    plan.reasoningEffort="low";
    auto invalid=config;invalid.model="";fails([&]{ark::validateAssistantConfig(invalid);},"model required");
    for(auto url:{"http://example.com/v1/responses","http://localhost.evil/v1/responses","https://user:pass@example.com/api","https:///api","https://example.com/api?key=x","https://example.com/api\r\nInjected:x"}){
        invalid=config;invalid.endpoint=url;fails([&]{ark::validateAssistantConfig(invalid);},"reject invalid endpoint");
    }
    for(auto url:{"http://localhost:1234/v1/responses","http://127.0.0.1/v1/responses","http://[::1]:1234/v1/responses"}){
        invalid=config;invalid.endpoint=url;fails([&]{ark::validateAssistantConfig(invalid);},"no credentials on HTTP");
        invalid.apiKeyEnv="";ark::validateAssistantConfig(invalid);check(ark::assistantKeyAvailable(invalid),"unauthenticated local connector");
    }
    invalid=config;invalid.apiKeyEnv="ARK_ASSISTANT_TEST_UNSET_5438D71F";check(!ark::assistantKeyAvailable(invalid),"presence-only missing credential check");
    invalid=config;invalid.maxRounds=33;fails([&]{ark::validateAssistantConfig(invalid);},"round bound");
    auto duplicate=tools;duplicate.arr.push_back(tools.arr[0]);fails([&]{ark::assistantFunctionTools(duplicate);},"duplicate tool rejected");
    int transports=0,calls=0;std::vector<ark::AssistantEvent::Kind> events;
    auto result=ark::runAssistant(config,V::makeStr("Inspect"),tools,[&](auto& name,auto& args){++calls;check(name=="doc_info"&&args.t==V::Obj,"tool dispatch");return json(R"({"revision":42})");},[&](const auto& e){events.push_back(e.kind);},nullptr,[&](const auto&,const auto& body,const auto*){
        ++transports;
        if(transports==1)return toolResponse();
        auto history=body.find("input");check(history->arr.size()==4,"history includes prompt reasoning call result");
        check(history->arr[1].find("encrypted_content")->str=="opaque-reasoning","reasoning roundtrip");
        check(history->arr[3].find("call_id")->str=="call_1","result call ID");
        check(history->arr[3].find("output")->str==R"({"revision":42})","actual tool result propagated");return finalResponse();
    });
    check(calls==1&&transports==2&&result.rounds==2,"bounded two round loop");check(result.text=="Done.","final text");
    check(result.conversation.arr.size()==5,"retain complete history");check(events.size()==7,"status/tool/text and usage events");
    check(result.usage.reportedResponses==0&&result.usage.missingResponses==2,"missing usage is explicitly unknown");
    check(ark::assistantUsageLabel(result.usage).find("unavailable")!=std::string::npos,"unknown usage label");
    auto runWith=[&](V response){return ark::runAssistant(config,V::makeStr("test"),tools,[&](auto&,auto&){++calls;return V{};},{},nullptr,[&](auto&,auto&,auto*){return response;});};
    V reasoningOnly=json(R"({"status":"completed","output":[{"type":"reasoning","id":"rs_no_action","summary":[]}],"usage":{"input_tokens":5940,"output_tokens":20,"total_tokens":5960,"output_tokens_details":{"reasoning_tokens":20}}})");
    ark::AssistantUsage noActionUsage;std::string noActionError;calls=0;
    try{ark::runAssistant(plan,V::makeStr("Create a model"),tools,[&](auto&,auto&){++calls;return V{};},[&](const auto& event){if(event.kind==ark::AssistantEvent::Usage)noActionUsage=event.usage;},nullptr,[&](auto&,auto&,auto*){return reasoningOnly;});}catch(const std::runtime_error& error){noActionError=error.what();}
    check(noActionError.find("without a visible reply or ARK tool call")!=std::string::npos&&noActionUsage.totalTokens==5960&&calls==0,"reasoning-only completed response reports billed usage and clear no-action error");
    int optionalNamespaceCalls=0,planRounds=0;
    auto optionalNamespace=ark::runAssistant(plan,V::makeStr("Inspect"),tools,[&](auto& name,auto&){check(name=="doc_info","optional namespace preserves tool identity");++optionalNamespaceCalls;return V::makeObj();},{},nullptr,[&](auto&,auto&,auto*){return ++planRounds==1?toolResponse("call_optional","doc_info"):finalResponse();});
    check(optionalNamespaceCalls==1&&optionalNamespace.text=="Done.","documented optional namespace still dispatches unique advertised ARK tool");
    int priorCalls=0,priorRounds=0;std::string priorError;
    try{ark::runAssistant(config,V::makeStr("Edit"),tools,[&](auto&,auto&){++priorCalls;return V::makeObj();},{},nullptr,[&](auto&,auto&,auto*){return ++priorRounds==1?toolResponse("call_prior","doc_info"):reasoningOnly;});}catch(const std::runtime_error& error){priorError=error.what();}
    check(priorCalls==1&&priorError.find("Earlier ARK tool calls may have changed")!=std::string::npos,"no final visible reply warns about prior tool calls");
    // Reproduce the direct-plan wire shape: completed output items arrive before
    // a terminal response whose output array is empty. Carry the recovered tool
    // call through the real AgentSession and continue with its actual result.
    ark::AgentSession disposable;V applyTools=json(R"([{"name":"doc_apply","description":"Atomic document edit","inputSchema":{"type":"object"}}])");
    auto doneEvent=[](int index,V item){V event=V::makeObj();event.set("type",V::makeStr("response.output_item.done"));event.set("output_index",V::makeNum(index));event.set("item",std::move(item));return std::string("data: ")+event.dump()+"\n\n";};
    auto terminalEvent=[](int in,int out){V usage=V::makeObj(),response=V::makeObj(),event=V::makeObj();usage.set("input_tokens",V::makeNum(in));usage.set("output_tokens",V::makeNum(out));usage.set("total_tokens",V::makeNum(in+out));response.set("status",V::makeStr("completed"));response.set("output",V::makeArr());response.set("usage",usage);event.set("type",V::makeStr("response.completed"));event.set("response",response);return std::string("data: ")+event.dump()+"\n\n";};
    V doneReason=json(R"({"type":"reasoning","id":"rs_box","summary":[],"encrypted_content":"offline-opaque"})");
    V boxCall=json(R"({"type":"function_call","namespace":"ark","name":"doc_apply","call_id":"call_box"})");
    boxCall.set("arguments",V::makeStr(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"Offline box","p":{"dx":10,"dy":12,"dz":8}}]})"));
    const V firstWire=ark::parseChatGptSse(doneEvent(0,doneReason)+doneEvent(1,boxCall)+terminalEvent(100,10));
    V finalMessage=json(R"({"type":"message","role":"assistant","content":[{"type":"output_text","text":"Box created."}]})");
    const V secondWire=ark::parseChatGptSse(doneEvent(0,finalMessage)+terminalEvent(20,5));
    int wireRounds=0,wireCalls=0;std::vector<ark::AssistantUsage> wireUsage;
    auto modeled=ark::runAssistant(plan,V::makeStr("Create a box"),applyTools,[&](const std::string& name,const V& args){++wireCalls;check(name=="doc_apply","recovered tool identity");return disposable.call(name,args);},[&](const ark::AssistantEvent& event){if(event.kind==ark::AssistantEvent::Usage)wireUsage.push_back(event.usage);},nullptr,[&](const ark::AssistantConfig&,const V& request,const std::atomic_bool*){
        if(++wireRounds==1)return firstWire;
        const auto& history=request.find("input")->arr;check(history.size()==4&&history.back().find("type")->str=="function_call_output","continuation carries recovered call and tool result");
        V returned;check(arkjson::parse(history.back().find("output")->str,returned)&&!returned.find("isError")->b,"real doc_apply succeeded before continuation");return secondWire;
    });
    check(wireCalls==1&&disposable.revision==1&&disposable.document.features.size()==1&&disposable.document.features[0].name=="Offline box","streamed tool item creates exactly one analytic box");
    check(modeled.text=="Box created."&&wireRounds==2&&modeled.usage.totalTokens==135&&modeled.usage.reportedResponses==2&&wireUsage.size()==2&&wireUsage[0].totalTokens==110&&wireUsage[1].totalTokens==135,"full SSE-to-document chain returns text and counts each provider response once");
    calls=0;fails([&]{runWith(toolResponse("call_a","shell_execute"));},"unknown tool rejected");check(calls==0,"unknown tool not executed");
    auto mixed=toolResponse();V outputs=*mixed.find("output");auto bad=toolResponse("call_bad","shell_execute");outputs.arr.push_back(bad.find("output")->arr.back());mixed.set("output",outputs);
    fails([&]{runWith(mixed);},"whole response validated before dispatch");check(calls==0,"no partial dispatch before invalid tool");
    auto incomplete=toolResponse();incomplete.set("status",V::makeStr("incomplete"));fails([&]{runWith(incomplete);},"incomplete rejected");check(calls==0,"incomplete tool not executed");
    auto failed=toolResponse();failed.set("error",json(R"({"message":"provider problem"})"));fails([&]{runWith(failed);},"provider error rejected");
    transports=0;calls=0;
    ark::runAssistant(config,V::makeStr("test"),tools,[&](auto&,auto&){++calls;return V{};},{},nullptr,[&](auto&,const auto& body,auto*){
        if(++transports==1)return toolResponse("call_malformed","doc_info","[]");
        V error;check(arkjson::parse(body.find("input")->arr.back().find("output")->str,error)&&error.find("isError")->b,"malformed args reported back");return finalResponse();
    });check(calls==0,"malformed args not dispatched");
    transports=0;
    ark::runAssistant(config,V::makeStr("test"),tools,[](auto&,auto&)->V{throw std::runtime_error("sensitive error detail");},{},nullptr,[&](auto&,const auto& body,auto*){
        if(++transports==1)return toolResponse();
        auto data=body.find("input")->arr.back().find("output")->str;
        check(data.find("sensitive")==std::string::npos&&data.find("isError")!=std::string::npos,"callback exceptions sanitized");return finalResponse();
    });
    transports=0;
    ark::runAssistant(config,V::makeStr("test"),tools,[](auto&,auto&){return json(R"({"isError":true,"structuredContent":{"error":"Stale revision","revision":17}})");},{},nullptr,[&](auto&,const auto& body,auto*){
        if(++transports==1)return toolResponse();
        V data;check(arkjson::parse(body.find("input")->arr.back().find("output")->str,data),"tool error is JSON");
        check(data.find("isError")->b&&data.find("structuredContent")->find("revision")->num==17,"tool error and revision preserved");return finalResponse();
    });
    std::atomic_bool cancel=true;transports=0;
    fails([&]{ark::runAssistant(config,V::makeStr("test"),tools,{}, {},&cancel,[&](auto&,auto&,auto*){++transports;return finalResponse();});},"early cancellation");check(transports==0,"cancel before network");
    cancel=false;calls=0;fails([&]{ark::runAssistant(config,V::makeStr("test"),tools,[&](auto&,auto&){++calls;return V{};},{},&cancel,[&](auto&,auto&,auto*){cancel=true;return toolResponse();});},"cancel after response");check(calls==0,"no mutation after cancellation");
    invalid=config;invalid.maxRounds=1;
    fails([&]{ark::runAssistant(invalid,V::makeStr("test"),tools,[](auto&,auto&){return V::makeObj();},{},nullptr,[&](auto&,auto&,auto*){return toolResponse();});},"round exhaustion explicit");
    fails([&]{runWith(toolResponse(""));},"missing call ID rejected");
    fails([&]{ark::runAssistant(config,result.conversation,tools,[](auto&,auto&){return V{};},{},nullptr,[&](auto&,auto&,auto*){return toolResponse();});},"reused historical call ID rejected");
    invalid=config;invalid.maxResponseBytes=1024;
    fails([&]{ark::runAssistant(invalid,V::makeStr("test"),tools,{}, {},nullptr,[&](auto&,auto&,auto*){auto huge=finalResponse();huge.set("padding",V::makeStr(std::string(1024,'x')));return huge;});},"response byte bound");
    invalid=config;invalid.maxConversationBytes=1024;
    fails([&]{ark::assistantRequest(invalid,conversation,tools);},"conversation byte bound");
    // Usage across tool rounds includes reasoning and cache only as subsets.
    transports=0;std::vector<ark::AssistantUsage> snapshots;
    auto counted=ark::runAssistant(config,V::makeStr("count"),tools,[](auto&,auto&){return V::makeObj();},
        [&](const auto& e){if(e.kind==ark::AssistantEvent::Usage)snapshots.push_back(e.usage);},nullptr,
        [&](auto&,auto&,auto*){
            V response=++transports==1?toolResponse():finalResponse();
            response.set("usage",json(transports==1?
                R"({"input_tokens":100,"output_tokens":40,"total_tokens":140,"input_tokens_details":{"cached_tokens":80},"output_tokens_details":{"reasoning_tokens":30}})":
                R"({"input_tokens":150,"output_tokens":20,"total_tokens":170,"input_tokens_details":{"cached_tokens":100},"output_tokens_details":{"reasoning_tokens":10}})"));
            return response;
        });
    check(counted.usage.totalTokens==310&&counted.usage.inputTokens==250&&counted.usage.outputTokens==60,"sum provider usage over tool rounds");
    check(counted.usage.cachedInputTokens==180&&counted.usage.reasoningTokens==40,"cache and reasoning subsets");
    check(counted.usage.reportedResponses==2&&!counted.usage.missingResponses,"complete usage evidence");
    check(snapshots.size()==2&&snapshots[0].totalTokens==140&&snapshots[1].totalTokens==310,"usage events are cumulative snapshots");
    check(ark::assistantUsageLabel(counted.usage).find("partial")==std::string::npos,"complete label");
    ark::AssistantUsage session;ark::accumulateAssistantUsage(session,counted.usage);
    ark::accumulateAssistantUsage(session,result.usage);
    check(session.totalTokens==310&&session.missingResponses==2&&ark::assistantUsageLabel(session).find("partial")!=std::string::npos,"mixed provider support yields partial lower bound");
    for(auto malformed:{R"({"input_tokens":-1,"output_tokens":2,"total_tokens":1})",
                        R"({"input_tokens":1.5,"output_tokens":2,"total_tokens":3.5})",
                        R"({"input_tokens":1,"output_tokens":2,"total_tokens":4})",
                        R"({"input_tokens":1,"output_tokens":2,"total_tokens":3,"input_tokens_details":{"cached_tokens":5}})",
                        R"({"input_tokens":1,"output_tokens":2,"total_tokens":3,"output_tokens_details":{"reasoning_tokens":3}})",
                        R"({"input_tokens":1e30,"output_tokens":2,"total_tokens":1e30})"}){
        auto response=finalResponse();response.set("usage",json(malformed));auto r=runWith(response);
        check(r.text=="Done."&&r.usage.missingResponses==1&&!r.usage.reportedResponses&&!r.usage.totalTokens,"malformed usage does not invent tokens or block modeling");
    }
    auto zero=finalResponse();zero.set("usage",json(R"({"input_tokens":0,"output_tokens":0,"total_tokens":0})"));
    check(runWith(zero).usage.reportedResponses==1,"reported zero differs from absent usage");
    auto incompleteCounted=finalResponse();incompleteCounted.set("status",V::makeStr("incomplete"));
    incompleteCounted.set("usage",json(R"({"input_tokens":10,"output_tokens":5,"total_tokens":15})"));
    ark::AssistantUsage partial;
    fails([&]{ark::runAssistant(config,V::makeStr("count"),tools,{},
        [&](const auto& e){if(e.kind==ark::AssistantEvent::Usage)partial=e.usage;},nullptr,
        [&](auto&,auto&,auto*){return incompleteCounted;});},"incomplete response remains rejected");
    check(partial.totalTokens==15,"usage from incomplete response survives via event");
    std::cout<<checks<<" assistant checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"assistant test failed: "<<e.what()<<'\n';return 1;}}
