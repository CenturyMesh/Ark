#include "ark/assistant_claude_code.hpp"
#include "ark/assistant_acp.hpp"
#include "ark/assistant_connectors.hpp"
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace ark;
using V=arkjson::Value;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
V json(const char* text){V v;check(arkjson::parse(text,v),"Bad test fixture JSON");return v;}
std::string field(const V& v,const char* key){auto p=v.find(key);return p?p->asStr():"";}
template<class Fn>std::string rejected(Fn action,const char* message){try{action();}catch(const std::exception& error){return error.what();}throw std::runtime_error(message);}
const char* kInit=R"({"commands":[],"models":[{"value":"default","resolvedModel":"claude-opus-5-5","supportedEffortLevels":["low","medium","high","xhigh","max"]},{"value":"sonnet","resolvedModel":"claude-sonnet-5-5","supportedEffortLevels":["low","high"]}],"account":{"email":"person@example.com","subscriptionType":"Claude Max","apiProvider":"firstParty"},"claude_code_version":"2.1.301"})";
const char* kUsage=R"({"input_tokens":4,"cache_creation_input_tokens":1154,"cache_read_input_tokens":1076,"output_tokens":37,"output_tokens_details":{"thinking_tokens":5}})";
// Speaks the Claude Code side of the stream-json control protocol.
struct State {std::deque<V> incoming;std::vector<V> sent,toolResults;std::string mode="write";V catalog=V::makeArr();int users=0,mcpId=0,control=0;bool interrupted=false;};
V controlRequest(State& s,V request){auto packet=V::makeObj();packet.set("type",V::makeStr("control_request"));packet.set("request_id",V::makeStr(s.mode=="duplicate-control"&&s.control==2?"c2":"c"+std::to_string(++s.control)));packet.set("request",request);return packet;}
void mcp(State& s,const char* method,V params,bool notification=false,const char* server="ark"){auto m=V::makeObj();m.set("jsonrpc",V::makeStr("2.0"));m.set("method",V::makeStr(method));if(!notification)m.set("id",V::makeNum(s.mcpId++));if(!params.isNull())m.set("params",params);auto r=V::makeObj();r.set("subtype",V::makeStr("mcp_message"));r.set("server_name",V::makeStr(server));r.set("message",m);s.incoming.push_back(controlRequest(s,r));}
void call(State& s,const char* name,V args,const char* useId){auto p=V::makeObj(),meta=V::makeObj();p.set("name",V::makeStr(name));p.set("arguments",args);meta.set("claudecode/toolUseId",V::makeStr(useId));p.set("_meta",meta);mcp(s,"tools/call",p,false,s.mode=="foreign-server"?"other":"ark");}
void finish(State& s,const char* subtype,bool error,const char* text){auto r=V::makeObj();r.set("type",V::makeStr("result"));r.set("subtype",V::makeStr(subtype));r.set("is_error",V::makeBool(error));r.set("result",V::makeStr(text));r.set("usage",json(kUsage));s.incoming.push_back(r);}
class Script final:public AssistantAcpChannel {
    std::shared_ptr<State> s;
public:
    explicit Script(std::shared_ptr<State> state):s(state){}
    void send(const V& p)override{s->sent.push_back(p);const auto type=field(p,"type");
        if(type=="control_request"){const auto subtype=field(*p.find("request"),"subtype");
            if(subtype=="initialize"){check(field(p,"request_id")=="ark_init","Initialize must use the ARK request id");mcp(*s,"initialize",json(R"({"protocolVersion":"2025-11-25","capabilities":{}})"));auto r=V::makeObj(),inner=V::makeObj();inner.set("subtype",V::makeStr("success"));inner.set("request_id",V::makeStr("ark_init"));inner.set("response",s->mode=="no-models"?json(R"({"commands":[]})"):json(kInit));r.set("type",V::makeStr("control_response"));r.set("response",inner);s->incoming.push_back(r);}
            else if(subtype=="interrupt"){s->interrupted=true;finish(*s,"error_during_execution",true,"");}
            return;}
        if(type=="control_response"){const auto& inner=*p.find("response");if(field(inner,"subtype")!="success")return;const auto* body=inner.find("response");const auto* m=body?body->find("mcp_response"):nullptr;if(!m)return;const auto* result=m->find("result");if(!result)return;
            if(result->find("serverInfo")){mcp(*s,"notifications/initialized",V{},true);mcp(*s,"tools/list",V{});}
            else if(result->find("tools"))s->catalog=*result->find("tools");
            else if(result->find("content")){s->toolResults.push_back(*result);if(s->interrupted)return;if(s->toolResults.size()==1){call(*s,"doc_apply",json(R"({"expected_revision":7})"),"toolu_2");}else{auto text=json(R"({"type":"assistant","message":{"role":"assistant","content":[{"type":"text","text":"Done"}]}})");s->incoming.push_back(text);finish(*s,"success",false,"Done");}}
            return;}
        if(type=="user"){++s->users;auto init=json(R"({"type":"system","subtype":"init","permissionMode":"dontAsk","model":"claude-opus-5-5","mcp_servers":[{"name":"ark","status":"connected","source":"sdk"}]})");auto names=V::makeArr();for(const auto& tool:s->catalog.arr)names.arr.push_back(V::makeStr("mcp__ark__"+field(tool,"name")));if(s->mode=="bash-exposed")names.arr.push_back(V::makeStr("Bash"));init.set("tools",names);s->incoming.push_back(init);
            if(s->users>1){finish(*s,"success",false,"Steered");return;}
            if(s->mode=="error-result"){finish(*s,"error_during_execution",true,"");return;}
            if(s->mode=="login"){finish(*s,"success",true,"Not logged in - Please run /login");return;}
            if(s->mode=="can-use-tool"){auto r=json(R"({"subtype":"can_use_tool","tool_name":"Bash","input":{"command":"dir"}})");s->incoming.push_back(controlRequest(*s,r));}
            s->incoming.push_back(json(R"({"type":"rate_limit_event","rate_limit_info":{"status":"allowed"}})"));
            call(*s,"doc_info",V::makeObj(),"toolu_1");}
    }
    std::optional<V> receive(std::chrono::milliseconds wait)override{if(s->incoming.empty()){std::this_thread::sleep_for(wait);return {};}auto v=s->incoming.front();s->incoming.pop_front();return v;}
};
V tools(){return json(R"([{"name":"doc_info","description":"Read document identity","inputSchema":{"type":"object","properties":{}},"annotations":{"readOnlyHint":true}},{"name":"doc_apply","description":"Apply commands","inputSchema":{"type":"object","properties":{"expected_revision":{"type":"number"}}},"annotations":{"readOnlyHint":false}}])");}
AssistantConfig config(std::shared_ptr<State> state){auto c=assistantProviderPreset("claude-code");c.timeoutSeconds=2;c.acpChannelFactory=[state]{return std::make_unique<Script>(state);};return c;}
V receipt(double revision){V data=V::makeObj();data.set("revision",V::makeNum(revision));auto r=V::makeObj(),content=V::makeArr(),item=V::makeObj();item.set("type",V::makeStr("text"));item.set("text",V::makeStr(data.dump()));content.arr.push_back(item);r.set("content",content);r.set("structuredContent",data);r.set("isError",V::makeBool(false));return r;}
bool contains(const V& conversation,const char* prefix){for(const auto& row:conversation.arr)if(field(row,"content").rfind(prefix,0)==0)return true;return false;}
void presetAndValidation(){
    auto c=assistantProviderPreset("claude-code");check(c.protocol==AssistantProtocol::ClaudeCode&&c.authMode==AssistantAuthMode::CliAccount&&c.endpoint=="claude"&&c.apiKeyEnv.empty()&&!c.imageInputSupported,"Claude Code preset must be the CLI-account route");
    check(assistantProtocolName(AssistantProtocol::ClaudeCode)=="claude-code"&&assistantProtocolFromName("claude-code")==AssistantProtocol::ClaudeCode,"Protocol name must round-trip for saved settings");
    validateAssistantConfig(c);
    auto bad=c;bad.apiKeyEnv="ANTHROPIC_API_KEY";rejected([&]{validateAssistantConfig(bad);},"API-key variable must reject on the CLI route");
    bad=c;bad.imageInputSupported=true;rejected([&]{validateAssistantConfig(bad);},"Unqualified image input must reject");
    bad=c;bad.reasoningEffort="ultra";rejected([&]{validateAssistantConfig(bad);},"Unknown effort must reject");
    bad=c;bad.model="bad model";rejected([&]{validateAssistantConfig(bad);},"Model names with spaces must reject");
    check(!assistantClaudeCodeInheritsEnvironment("CLAUDECODE")&&!assistantClaudeCodeInheritsEnvironment("claude_code_entrypoint"),"A parent Claude Code session marker must not be inherited");
    check(assistantClaudeCodeInheritsEnvironment("ANTHROPIC_API_KEY")&&assistantClaudeCodeInheritsEnvironment("CLAUDE_CODE_OAUTH_TOKEN")&&assistantClaudeCodeInheritsEnvironment("PATH"),"Every sign-in method the user configured must stay available to the CLI");
}
void arguments(){
    auto c=assistantProviderPreset("claude-code");c.model="sonnet";c.reasoningEffort="high";const auto args=assistantClaudeCodeArguments(c,tools(),"C:/iso/ark-role.md");
    auto after=[&](const char* flag)->std::string{auto it=std::find(args.begin(),args.end(),flag);check(it!=args.end()&&it+1!=args.end(),"Missing Claude Code flag");return *(it+1);};
    check(after("--input-format")=="stream-json"&&after("--output-format")=="stream-json"&&after("--tools").empty()&&after("--permission-mode")=="dontAsk","Built-in tools must be disabled");
    check(std::find(args.begin(),args.end(),"--setting-sources")==args.end()&&std::find(args.begin(),args.end(),"--bare")==args.end(),"Authentication options must not be restricted");
    check(std::find(args.begin(),args.end(),"--strict-mcp-config")!=args.end()&&std::find(args.begin(),args.end(),"--no-session-persistence")!=args.end()&&std::find(args.begin(),args.end(),"--disable-slash-commands")!=args.end(),"Isolation flags are required");
    check(after("--mcp-config")==R"({"mcpServers":{"ark":{"type":"sdk","name":"ark"}}})","Only the in-process ARK SDK server may be configured");
    check(after("--allowedTools")=="mcp__ark__doc_info,mcp__ark__doc_apply","Grants must name exact role tools without wildcards");
    check(after("--model")=="sonnet"&&after("--effort")=="high"&&after("--system-prompt-file")=="C:/iso/ark-role.md","Model, effort and role prompt must be explicit");
    auto badTools=json(R"([{"name":"bad name","inputSchema":{"type":"object"}}])");rejected([&]{assistantClaudeCodeArguments(c,badTools,"x");},"Tool names outside the MCP grammar must reject");
}
void single(){
    auto state=std::make_shared<State>();int reads=0,writes=0;V retained;
    auto run=runAssistant(config(state),V::makeStr("Inspect and edit"),tools(),[&](const auto& name,const V& args){if(name=="doc_info")++reads;else{check(args.find("expected_revision")->asNum()==7,"Arguments must reach the host unchanged");++writes;}return receipt(name=="doc_info"?7:8);},{},nullptr,{},{},[&](const V& h){retained=h;});
    check(reads==1&&writes==1&&run.text=="Done"&&run.rounds==1,"Host must execute both role tools once");
    check(field(state->sent.front(),"type")=="control_request","Initialize must precede any prompt");
    check(state->catalog.arr.size()==2&&state->catalog.arr[1].find("inputSchema")&&state->catalog.arr[0].find("annotations"),"tools/list must advertise exact schemas and annotations");
    check(run.usage.inputTokens==4+1154+1076&&run.usage.outputTokens==37&&run.usage.cachedInputTokens==1076&&run.usage.reasoningTokens==5&&run.usage.reportedResponses==1,"Usage must come from the CLI result receipt");
    int calls=0,outputs=0;for(const auto& row:run.conversation.arr){if(field(row,"type")=="function_call")++calls;if(field(row,"type")=="function_call_output")++outputs;}check(calls==2&&outputs==2,"Every executed call must keep its receipt");
    check(contains(run.conversation,"ARK_CLI_MODEL_NEGOTIATED")&&run.conversation.dump().find("claude-opus-5-5")!=std::string::npos,"Negotiated model must be recorded");
    check(run.conversation.dump().find("person@example.com")==std::string::npos,"The account e-mail must never enter the conversation");
    for(const auto& p:state->sent)if(field(p,"type")=="user")check(field(*p.find("message"),"content").find("Retained context and current input")==0,"First prompt carries the retained context");
    check(!retained.arr.empty(),"History checkpoints must be retained");
}
void rejections(){
    for(const char* mode:{"bash-exposed","foreign-server","duplicate-control","error-result","login","no-models"}){
        auto state=std::make_shared<State>();state->mode=mode;int executed=0;
        const auto error=rejected([&]{runAssistant(config(state),V::makeStr("Must reject"),tools(),[&](const auto&,const V&){++executed;return receipt(7);});},mode);
        if(std::string(mode)=="bash-exposed"||std::string(mode)=="foreign-server"||std::string(mode)=="no-models")check(executed==0,"No CAD tool may run after an isolation failure");
        if(std::string(mode)=="no-models")check(state->users==0,"No prompt may be sent without a model catalog");
        if(std::string(mode)=="login")check(error.find("/login")!=std::string::npos,"Login failures must say how to sign in");
    }
}
void negotiation(){
    auto state=std::make_shared<State>();auto c=config(state);c.model="unknown-model";rejected([&]{runAssistant(c,V::makeStr("x"),tools(),[](const auto&,const V&){return receipt(7);});},"Unknown model must reject");check(state->users==0,"No prompt after a rejected model");
    state=std::make_shared<State>();c=config(state);c.model="sonnet";c.reasoningEffort="max";rejected([&]{runAssistant(c,V::makeStr("x"),tools(),[](const auto&,const V&){return receipt(7);});},"Unsupported effort must reject");check(state->users==0,"No prompt after a rejected effort");
    state=std::make_shared<State>();c=config(state);c.model="sonnet";c.reasoningEffort="high";auto run=runAssistant(c,V::makeStr("x"),tools(),[](const auto&,const V&){return receipt(7);});check(run.conversation.dump().find("claude-sonnet-5-5")!=std::string::npos,"Alias must resolve to the advertised model");
    state=std::make_shared<State>();auto probe=assistantClaudeCodeProbe(config(state));check(state->users==0&&!probe.find("prompt_sent")->b&&probe.find("sdk_mcp_listed")->b,"Probe must not send a prompt");
    check(field(probe,"api_provider")=="firstParty"&&field(probe,"subscription_type")=="Claude Max"&&probe.find("models")->arr.size()==2&&probe.dump().find("@example.com")==std::string::npos,"Probe returns the catalog and route, never the e-mail");
    state=std::make_shared<State>();auto models=assistantProviderModels(config(state));check(models.size()==4&&models[0]=="default"&&std::find(models.begin(),models.end(),"claude-sonnet-5-5")!=models.end(),"Catalog lists aliases and resolved IDs");
}
void permissionAndSteering(){
    auto state=std::make_shared<State>();state->mode="can-use-tool";auto run=runAssistant(config(state),V::makeStr("x"),tools(),[](const auto&,const V&){return receipt(7);});bool denied=false;
    for(const auto& p:state->sent)if(field(p,"type")=="control_response"){const auto& inner=*p.find("response");const auto* body=inner.find("response");if(body&&field(*body,"behavior")=="deny")denied=true;}check(denied&&run.text=="Done","can_use_tool must be denied without stopping role tools");
    state=std::make_shared<State>();int writes=0;bool steered=false;
    AssistantInbox inbox=[&]{if(steered||state->toolResults.empty())return V{};steered=true;return json(R"([{"role":"user","content":"Use 9 mm instead"}])");};
    run=runAssistant(config(state),V::makeStr("Old request"),tools(),[&](const auto& name,const V&){if(name=="doc_apply")++writes;return receipt(7);},{},nullptr,{},inbox);
    check(state->interrupted&&state->users==2&&writes==0&&run.text=="Steered","Steering must interrupt the old turn and reject its stale write");
    bool forwarded=false;for(const auto& p:state->sent)if(field(p,"type")=="user"&&field(*p.find("message"),"content").find("Use 9 mm instead")!=std::string::npos)forwarded=true;check(forwarded,"Steering input must reach the next turn");
}
void cancellationAndResume(){
    auto state=std::make_shared<State>();std::atomic_bool cancelled=false;V retained;
    rejected([&]{runAssistant(config(state),V::makeStr("Write"),tools(),[&](const auto& name,const V&){if(name=="doc_apply")cancelled=true;return receipt(8);},{},&cancelled,{},{},[&](const V& h){retained=h;});},"Cancellation must stop the run");
    check(contains(retained,"ARK_ACP_INSPECT_REQUIRED"),"Interrupted runs must require inspection before writes");
    state=std::make_shared<State>();int writes=0;auto context=json(R"([{"role":"user","content":"Resume"},{"role":"developer","content":"ARK_ACP_INSPECT_REQUIRED: previous interruption"}])");
    auto run=runAssistant(config(state),context,tools(),[&](const auto& name,const V&){if(name=="doc_apply")++writes;return receipt(7);});
    check(writes==1&&contains(run.conversation,"ARK_ACP_INSPECTION_CONFIRMED"),"A real doc_info receipt must unlock writes after an interruption");
    state=std::make_shared<State>();auto image=json(R"([{"role":"user","content":[{"type":"input_text","text":"x"},{"type":"input_image","image_url":"data:image/png;base64,AA=="}]}])");rejected([&]{runAssistant(config(state),image,tools(),[](const auto&,const V&){return receipt(7);});},"Images must reject on this route");check(state->sent.empty(),"No CLI traffic before rejecting images");
}
#ifdef _WIN32
// The test executable doubles as a fake native CLI so the real process channel is exercised.
int fakeCli(int argc,char** argv){
    std::vector<std::string> args(argv+1,argv+argc);auto after=[&](const char* flag){auto it=std::find(args.begin(),args.end(),flag);return it!=args.end()&&it+1!=args.end()?*(it+1):std::string("<missing>");};
    if(after("--tools")!=""||after("--allowedTools")!="mcp__ark__doc_info,mcp__ark__doc_apply")return 3;
    const char* key=std::getenv("ANTHROPIC_API_KEY");if(!key||std::string(key)!="sk-test-user-own-key"||std::getenv("CLAUDECODE")||!std::getenv("ARK_CLAUDE_FAKE_MARKER"))return 4;
    const auto cwd=std::filesystem::current_path();if(!std::filesystem::is_directory(cwd/".git")||std::filesystem::path(after("--system-prompt-file"))!=cwd/"ark-role.md")return 5;
    std::ifstream prompt(cwd/"ark-role.md");std::string first;std::getline(prompt,first);if(first.rfind("You are an ARK CAD role",0)!=0)return 6;
    auto out=[](const std::string& line){std::cout<<line<<"\n";std::cout.flush();};std::string line;int toolsSeen=0;
    while(std::getline(std::cin,line)){V p;if(!arkjson::parse(line,p))return 7;const auto type=field(p,"type");
        if(type=="control_request"&&field(*p.find("request"),"subtype")=="initialize"){out(R"({"type":"control_request","request_id":"n1","request":{"subtype":"mcp_message","server_name":"ark","message":{"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-11-25"}}}})");out(std::string(R"({"type":"control_response","response":{"subtype":"success","request_id":"ark_init","response":)")+kInit+"}}");}
        else if(type=="control_response"){const auto& body=*p.find("response")->find("response");const auto& m=*body.find("mcp_response");const auto& r=*m.find("result");if(r.find("serverInfo"))out(R"({"type":"control_request","request_id":"n2","request":{"subtype":"mcp_message","server_name":"ark","message":{"jsonrpc":"2.0","id":1,"method":"tools/list"}}})");else if(r.find("tools"))toolsSeen=(int)r.find("tools")->arr.size();else if(r.find("content")){out(R"({"type":"result","subtype":"success","is_error":false,"result":"native ok","usage":{"input_tokens":10,"output_tokens":2}})");}}
        else if(type=="user"){if(toolsSeen!=2)return 8;out(R"({"type":"system","subtype":"init","permissionMode":"dontAsk","model":"claude-opus-5-5","tools":["mcp__ark__doc_info","mcp__ark__doc_apply"],"mcp_servers":[{"name":"ark","status":"connected"}]})");out(R"({"type":"control_request","request_id":"n3","request":{"subtype":"mcp_message","server_name":"ark","message":{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"doc_info","arguments":{}}}}})");}}
    return 0;
}
void nativeProcess(){
    wchar_t file[32768]{};check(GetModuleFileNameW(nullptr,file,32768)!=0,"No test executable path");
    _putenv_s("ANTHROPIC_API_KEY","sk-test-user-own-key");_putenv_s("CLAUDECODE","1");_putenv_s("ARK_CLAUDE_FAKE_MARKER","1");
    auto c=assistantProviderPreset("claude-code");const auto utf8=std::filesystem::path(file).u8string();c.endpoint=std::string(utf8.begin(),utf8.end());c.timeoutSeconds=10;int reads=0;
    auto run=runAssistant(c,V::makeStr("Native"),tools(),[&](const auto& name,const V&){check(name=="doc_info","Unexpected native tool");++reads;return receipt(7);});
    _putenv_s("ANTHROPIC_API_KEY","");_putenv_s("CLAUDECODE","");
    check(reads==1&&run.text=="native ok"&&run.usage.inputTokens==10,"Native channel must run the role tool through real pipes");
    auto script=c;script.endpoint="C:/Windows/System32/cmd.cmd";rejected([&]{assistantClaudeCodeExecutable(script);},"Script shims must reject");
}
#endif
}
int main(int argc,char** argv){
#ifdef _WIN32
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--input-format")return fakeCli(argc,argv);
#else
    (void)argc;(void)argv;
#endif
    try{presetAndValidation();arguments();single();rejections();negotiation();permissionAndSteering();cancellationAndResume();
#ifdef _WIN32
        nativeProcess();
#endif
        std::cout<<"assistant claude code tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<"\n";return 1;}
}
