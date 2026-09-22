#include "ark/assistant.hpp"
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
    auto invalid=config;invalid.model="";fails([&]{ark::validateAssistantConfig(invalid);},"model required");
    for(auto url:{"http://example.com/v1/responses","http://localhost.evil/v1/responses","https://user:pass@example.com/api","https:///api","https://example.com/api?key=x","https://example.com/api\r\nInjected:x"}){
        invalid=config;invalid.endpoint=url;fails([&]{ark::validateAssistantConfig(invalid);},"reject invalid endpoint");
    }
    for(auto url:{"http://localhost:1234/v1/responses","http://127.0.0.1/v1/responses","http://[::1]:1234/v1/responses"}){
        invalid=config;invalid.endpoint=url;fails([&]{ark::validateAssistantConfig(invalid);},"no credentials on HTTP");
        invalid.apiKeyEnv="";ark::validateAssistantConfig(invalid);check(ark::assistantKeyAvailable(invalid),"unauthenticated local connector");
    }
    invalid=config;invalid.apiKeyEnv="ARK_ASSISTANT_TEST_UNSET_5438D71F";check(!ark::assistantKeyAvailable(invalid),"presence-only missing credential check");
    invalid=config;invalid.maxRounds=0;fails([&]{ark::validateAssistantConfig(invalid);},"round bound");
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
    check(result.conversation.arr.size()==5,"retain complete history");check(events.size()==5,"status/tool/text events");
    auto runWith=[&](V response){return ark::runAssistant(config,V::makeStr("test"),tools,[&](auto&,auto&){++calls;return V{};},{},nullptr,[&](auto&,auto&,auto*){return response;});};
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
    std::cout<<checks<<" assistant checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"assistant test failed: "<<e.what()<<'\n';return 1;}}
