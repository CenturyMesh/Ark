#include "ark/assistant_acp.hpp"
#include "ark/assistant_connectors.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace ark {
namespace {
using V=arkjson::Value;
using Clock=std::chrono::steady_clock;
void need(bool ok,const char* message){if(!ok)throw AssistantTransportError(message);}
std::string str(const V& value,const char* key){const auto* p=value.find(key);return p?p->asStr():"";}
V obj(){return V::makeObj();}
V array(){return V::makeArr();}
V error(const char* message){auto value=obj();value.set("isError",V::makeBool(true));value.set("error",V::makeStr(message));return value;}
V rpc(const V& id,const char* method,V params){auto packet=obj();packet.set("jsonrpc",V::makeStr("2.0"));packet.set("id",id);packet.set("method",V::makeStr(method));packet.set("params",std::move(params));return packet;}
V reply(const V& id,V result){auto packet=obj();packet.set("jsonrpc",V::makeStr("2.0"));packet.set("id",id);packet.set("result",std::move(result));return packet;}
V rpcError(const V& id,int code,const char* message){auto packet=obj(),detail=obj();packet.set("jsonrpc",V::makeStr("2.0"));packet.set("id",id);detail.set("code",V::makeNum(code));detail.set("message",V::makeStr(message));packet.set("error",detail);return packet;}
V message(const char* role,const std::string& text){auto value=obj();value.set("role",V::makeStr(role));value.set("content",V::makeStr(text));return value;}
void stopped(const std::atomic_bool* cancel){need(!cancel||!cancel->load(),"Grok Build cancelled; prior tool receipts remain authoritative. Inspect the current revision before continuing.");}
std::string env(const char* key){const char* value=std::getenv(key);return value?value:"";}
std::filesystem::path executable(const AssistantConfig& config){
    auto path=std::filesystem::u8path(config.endpoint);
    if(config.endpoint=="grok"){
        const auto home=env("USERPROFILE");need(!home.empty(),"Cannot discover Grok Build. Configure the installed CLI's absolute path.");
        path=std::filesystem::u8path(home)/".grok"/"bin"/"grok.exe";
    }
    need(std::filesystem::is_regular_file(path),"Grok Build CLI is unavailable. Install it through the official CLI/GUI setup or choose its actual executable path.");return path;
}
#ifdef _WIN32
std::wstring wide(const std::string& text){int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),(int)text.size(),nullptr,0);need(count>0,"Invalid UTF-8 in Grok Build configuration.");std::wstring value(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),(int)text.size(),value.data(),count);return value;}
std::wstring quote(const std::wstring& text){std::wstring value=L"\"";unsigned slashes=0;for(auto c:text){if(c==L'\\'){++slashes;continue;}if(c==L'\"')value.append(slashes*2+1,L'\\');else value.append(slashes,L'\\');slashes=0;value+=c;}value.append(slashes*2,L'\\');return value+L'\"';}
struct OwnedHandle {HANDLE value=nullptr;~OwnedHandle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
class NativeChannel final:public AssistantAcpChannel {
    HANDLE process=nullptr,job=nullptr,input=nullptr,output=nullptr;
    std::filesystem::path directory;
    std::string pending;
    std::size_t limit;
public:
    explicit NativeChannel(const AssistantConfig& config,const V& roleTools):limit(config.maxResponseBytes){
        try{
            const auto cli=executable(config);
            auto originalHome=env("GROK_HOME");if(originalHome.empty())originalHome=env("USERPROFILE")+"/.grok";
            auto authPath=env("GROK_AUTH_PATH");if(authPath.empty())authPath=(std::filesystem::u8path(originalHome)/"auth.json").string();
            static std::atomic_uint64_t sequence{0};directory=std::filesystem::temp_directory_path()/("ark-acp-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(Clock::now().time_since_epoch().count())+"-"+std::to_string(++sequence));
            need(std::filesystem::create_directory(directory),"Cannot create isolated Grok Build workspace.");
            std::ofstream settings(directory/"config.toml");settings<<"[cli]\nauto_update = false\n[managed_mcps]\nenabled = false\ngateway_tools_enabled = false\n";
            for(const char* vendor:{"claude","cursor","codex"})settings<<"[compat."<<vendor<<"]\nskills = false\nrules = false\nagents = false\nmcps = false\nhooks = false\nsessions = false\n";settings.close();need((bool)settings,"Cannot write isolated Grok Build configuration.");
            // The CLI reads/refreshes its own store. ARK never opens that path.
            // Do not inherit an API key, inline credential, arbitrary Grok overlay,
            // plugin environment override or a global leader socket into this role.
            std::vector<std::wstring> environment;LPWCH block=GetEnvironmentStringsW();need(block!=nullptr,"Cannot prepare Grok Build process environment.");
            for(const wchar_t* cursor=block;*cursor;cursor+=wcslen(cursor)+1){std::wstring item(cursor),upper=item;std::transform(upper.begin(),upper.end(),upper.begin(),towupper);if(upper.rfind(L"GROK_",0)!=0&&upper.rfind(L"XAI_API_KEY=",0)!=0)environment.push_back(item);}FreeEnvironmentStringsW(block);
            environment.push_back(L"GROK_HOME="+directory.wstring());environment.push_back(L"GROK_AUTH_PATH="+wide(authPath));environment.push_back(L"GROK_DISABLE_API_KEY_AUTH=1");environment.push_back(L"GROK_MANAGED_MCPS_ENABLED=0");environment.push_back(L"GROK_MANAGED_MCP_GATEWAY_TOOLS_ENABLED=0");
            for(const char* vendor:{"CLAUDE","CURSOR","CODEX"})for(const char* surface:{"SKILLS","RULES","AGENTS","MCPS","HOOKS","SESSIONS"})environment.push_back(wide(std::string("GROK_")+vendor+"_"+surface+"_ENABLED=0"));
            std::sort(environment.begin(),environment.end());std::wstring childEnv;for(const auto& item:environment){childEnv+=item;childEnv+=L'\0';}childEnv+=L'\0';
            SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};OwnedHandle childIn,childOut,discard;
            need(CreatePipe(&childIn.value,&input,&sa,0)&&CreatePipe(&output,&childOut.value,&sa,0),"Cannot create Grok Build stdio pipes.");need(SetHandleInformation(input,HANDLE_FLAG_INHERIT,0)&&SetHandleInformation(output,HANDLE_FLAG_INHERIT,0),"Cannot restrict Grok Build handle inheritance.");
            discard.value=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);need(discard.value!=INVALID_HANDLE_VALUE,"Cannot discard CLI stderr securely.");
            STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;start.wShowWindow=SW_HIDE;start.hStdInput=childIn.value;start.hStdOutput=childOut.value;start.hStdError=discard.value;
            auto command=quote(cli.wstring())+L" --no-auto-update --oauth --tools search_tool,use_tool --no-subagents --disable-web-search --deny "+quote(L"Read(*)")+L" --deny "+quote(L"Bash(*)")+L" --deny "+quote(L"Edit(*)");
            if(roleTools.t==V::Arr)for(const auto& function:assistantFunctionTools(roleTools).arr)command+=L" --allow "+quote(wide("ark__"+str(function,"name")));
            command+=L" agent stdio --leader-socket "+quote((directory/"leader.sock").wstring());
            job=CreateJobObjectW(nullptr,nullptr);need(job!=nullptr,"Cannot isolate Grok Build process lifetime.");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;need(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=FALSE,"Cannot configure Grok Build process lifetime.");
            PROCESS_INFORMATION info{};const BOOL made=CreateProcessW(cli.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT|CREATE_SUSPENDED,childEnv.data(),directory.c_str(),&start,&info);need(made!=FALSE,"Cannot start the configured Grok Build CLI.");process=info.hProcess;
            if(!AssignProcessToJobObject(job,process)){TerminateProcess(process,1);CloseHandle(info.hThread);throw AssistantTransportError("Cannot own the Grok Build process; inference was not started.");}ResumeThread(info.hThread);CloseHandle(info.hThread);
        }catch(...){close();throw;}
    }
    ~NativeChannel()override{close();}
    void close(){if(job){CloseHandle(job);job=nullptr;}if(process){WaitForSingleObject(process,2000);CloseHandle(process);process=nullptr;}if(input){CloseHandle(input);input=nullptr;}if(output){CloseHandle(output);output=nullptr;}if(!directory.empty()){std::error_code ignored;std::filesystem::remove_all(directory,ignored);directory.clear();}}
    void send(const V& packet)override{std::string bytes=packet.dump()+"\n";need(bytes.size()<=32*1024*1024,"ACP request exceeded its transport bound.");DWORD written=0;std::size_t offset=0;while(offset<bytes.size()){need(WriteFile(input,bytes.data()+offset,(DWORD)std::min<std::size_t>(65536,bytes.size()-offset),&written,nullptr)&&written,"Grok Build stdin disconnected; prior receipts retained.");offset+=written;}}
    std::optional<V> receive(std::chrono::milliseconds wait)override{
        const auto deadline=Clock::now()+wait;
        do{auto newline=pending.find('\n');if(newline!=std::string::npos){std::string line=pending.substr(0,newline);pending.erase(0,newline+1);V packet;need(arkjson::parse(line,packet)&&packet.t==V::Obj,"Grok Build returned malformed ACP JSON; no partial packet was executed.");return packet;}
            DWORD available=0;need(PeekNamedPipe(output,nullptr,0,nullptr,&available,nullptr)!=FALSE,"Grok Build stdout disconnected; prior receipts retained.");
            if(available){char bytes[16384];DWORD count=0;need(ReadFile(output,bytes,std::min<DWORD>(available,sizeof(bytes)),&count,nullptr)&&count,"Grok Build output ended unexpectedly.");pending.append(bytes,count);need(pending.size()<=limit,"ACP packet exceeded the configured response bound.");continue;}
            need(WaitForSingleObject(process,0)==WAIT_TIMEOUT,"Grok Build exited before completing the ACP request.");std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }while(Clock::now()<deadline);return {};
    }
};
#endif
std::unique_ptr<AssistantAcpChannel> channel(const AssistantConfig& config,const V& tools=V{}){if(config.acpChannelFactory)return config.acpChannelFactory();
#ifdef _WIN32
    return std::make_unique<NativeChannel>(config,tools);
#else
    throw AssistantTransportError("Native Grok Build ACP transport is currently available on Windows only.");
#endif
}
V initialize(AssistantAcpChannel& io,unsigned timeout,const std::atomic_bool* cancel=nullptr){
    auto params=obj(),caps=obj(),fs=obj();fs.set("readTextFile",V::makeBool(false));fs.set("writeTextFile",V::makeBool(false));caps.set("fs",fs);caps.set("terminal",V::makeBool(false));params.set("protocolVersion",V::makeNum(1));params.set("clientCapabilities",caps);io.send(rpc(V::makeStr("ark_initialize"),"initialize",params));
    const auto deadline=Clock::now()+std::chrono::seconds(timeout);while(Clock::now()<deadline){stopped(cancel);auto packet=io.receive(std::chrono::milliseconds(20));if(!packet)continue;if(packet->find("method")){need(!packet->find("id"),"Unexpected ACP request during initialization; no tool was authorized.");continue;}need(str(*packet,"id")=="ark_initialize"&&!packet->find("error")&&packet->find("result"),"Grok Build ACP initialization failed.");const auto result=*packet->find("result");need(result.find("protocolVersion")&&result.find("protocolVersion")->asNum()==1,"Unsupported Grok Build ACP protocol version.");return result;}throw AssistantTransportError("Grok Build ACP initialization timed out.");
}
bool flag(const V* value,const char* key){auto p=value?value->find(key):nullptr;return p&&p->t==V::Bool&&p->b;}
V probeResult(const V& initialized){auto result=obj();const auto* meta=initialized.find("_meta"),*caps=initialized.find("agentCapabilities");result.set("sdkMcp",V::makeBool(flag(meta,"x.ai/mcp/sdk")));const auto* prompts=caps?caps->find("promptCapabilities"):nullptr;result.set("imageInputSupported",V::makeBool(flag(prompts,"image")));if(meta&&meta->find("modelState"))result.set("modelState",*meta->find("modelState"));return result;}
bool imageIn(const V& value){if(value.t==V::Obj){const auto kind=str(value,"type");if(kind=="input_image"||kind=="image")return true;for(const auto& entry:value.obj)if(imageIn(entry.second))return true;}if(value.t==V::Arr)for(const auto& entry:value.arr)if(imageIn(entry))return true;return false;}
}
void validateAssistantAcpConfig(const AssistantConfig& config){
    need(config.provider=="grok-build"&&config.authMode==AssistantAuthMode::CliAccount&&config.protocol==AssistantProtocol::Acp&&config.apiKeyEnv.empty(),"Grok Build ACP requires the CLI-account route with no API-key variable or cloud fallback.");
    need(config.endpoint=="grok"||(config.endpoint.size()<=4096&&config.endpoint.find_first_of("\r\n\t\"")==std::string::npos&&std::filesystem::u8path(config.endpoint).is_absolute()),"Choose grok auto-discovery or the CLI's actual absolute executable path.");
    need(config.model.size()<=128&&std::all_of(config.model.begin(),config.model.end(),[](unsigned char c){return c>=33&&c<127;}),"Invalid explicit Grok Build model ID.");need(config.reasoningEffort.empty(),"Grok Build ACP currently requires CLI-default effort; explicit effort is not silently ignored.");
    need(config.timeoutSeconds>=1&&config.timeoutSeconds<=300&&config.maxRounds<=32,"Invalid existing assistant timeout/round configuration.");need(config.maxResponseBytes>=1024&&config.maxResponseBytes<=16*1024*1024&&config.maxConversationBytes>=1024&&config.maxConversationBytes<=32*1024*1024,"Invalid ACP message/conversation bound.");
}
bool assistantAcpAvailable(const AssistantConfig& config){try{validateAssistantAcpConfig(config);if(config.acpChannelFactory)return true;(void)executable(config);return true;}catch(...){return false;}}
V assistantAcpProbe(const AssistantConfig& config){validateAssistantAcpConfig(config);auto io=channel(config);need((bool)io,"Missing ACP channel.");return probeResult(initialize(*io,config.timeoutSeconds));}
std::vector<std::string> assistantAcpModels(const AssistantConfig& config){const auto probe=assistantAcpProbe(config);const auto* state=probe.find("modelState");std::vector<std::string> ids;if(state){const auto* models=state->find("availableModels");if(models&&models->t==V::Arr)for(const auto& item:models->arr){auto id=str(item,"modelId");if(!id.empty())ids.push_back(id);}}need(!ids.empty(),"Grok Build did not provide a model catalog. Complete CLI login, or use the CLI-selected model explicitly; no API route was used.");return ids;}
AssistantResult runAssistantAcp(const AssistantConfig& config,const V& input,const V& tools,const AssistantTool& callTool,const AssistantEvents& events,const std::atomic_bool* cancel,const AssistantInbox& inbox,const AssistantHistory& history,const AssistantRetryHooks& hooks){
    validateAssistantAcpConfig(config);stopped(cancel);AssistantResult result;if(input.t==V::Str){need(!input.str.empty(),"Enter a Grok Build message first.");result.conversation.arr.push_back(message("user",input.str));}else{need(input.t==V::Arr&&!input.arr.empty(),"ACP requires a nonempty prompt/history.");result.conversation=input;}
    for(const auto& row:result.conversation.arr)if(str(row,"role")=="user")validateAssistantUserMessage(row);
    if(!config.onboardingPack.empty()){bool loaded=false;for(const auto& row:result.conversation.arr)if(str(row,"role")=="developer"&&str(row,"content").rfind("ARK_PREBAKED_GUIDE_V1\n",0)==0)loaded=true;if(!loaded)result.conversation.arr.insert(result.conversation.arr.begin(),message("developer","ARK_PREBAKED_GUIDE_V1\n"+config.onboardingPack));}
    need(!imageIn(result.conversation),"Grok Build ACP image input has not been qualified for this route. Pixels were not dropped or converted to text.");
    const auto functions=assistantFunctionTools(tools);auto catalog=array();std::set<std::string> allowed;
    for(const auto& function:functions.arr){auto entry=obj();const auto name=str(function,"name");need(allowed.insert(name).second,"Duplicate ARK tool in ACP role catalog.");entry.set("name",V::makeStr(name));if(function.find("description"))entry.set("description",*function.find("description"));entry.set("inputSchema",*function.find("parameters"));for(const auto& source:tools.arr)if(str(source,"name")==name&&source.find("annotations"))entry.set("annotations",*source.find("annotations"));catalog.arr.push_back(entry);}
    auto io=channel(config,tools);need((bool)io,"Missing ACP channel.");const auto initialized=initialize(*io,config.timeoutSeconds,cancel);auto probe=probeResult(initialized);need(flag(&probe,"sdkMcp"),"This Grok Build runtime does not advertise fail-closed SDK MCP. Upgrade the CLI; no prompt or CAD call was sent.");
    const auto* state=probe.find("modelState");std::string model=config.model;if(model.empty())model=state?str(*state,"currentModelId"):"";need(!model.empty(),"The CLI did not negotiate its selected model. Choose an explicit CLI model ID.");
    if(!config.model.empty()){const auto* models=state?state->find("availableModels"):nullptr;need(models&&models->t==V::Arr&&std::any_of(models->arr.begin(),models->arr.end(),[&](const auto& item){return str(item,"modelId")==model;}),"Selected model is absent from the CLI's negotiated catalog; no replacement or API fallback was used.");}
    result.conversation.arr.push_back(message("developer","ARK_ACP_MODEL_NEGOTIATED: provider grok-build, CLI account, model "+model+". Usage remains unknown without a provider receipt."));
    const std::string serverId="ark-role-"+std::to_string(Clock::now().time_since_epoch().count());auto servers=array(),server=obj();server.set("name",V::makeStr("ark"));server.set("serverId",V::makeStr(serverId));servers.arr.push_back(server);
    auto profile=obj(),toolConfig=obj(),nativeTools=array();profile.set("name",V::makeStr("ark-role"));profile.set("description",V::makeStr("ARK role with host-owned CAD authority"));profile.set("injectDefaultTools",V::makeBool(false));profile.set("agentsMd",V::makeBool(false));profile.set("discoverSkills",V::makeBool(false));
    for(const char* name:{"search_tool","use_tool"}){auto tool=obj();tool.set("id",V::makeStr(name));nativeTools.arr.push_back(tool);}toolConfig.set("tools",nativeTools);profile.set("toolConfig",toolConfig);
    auto meta=obj();meta.set("agentProfile",profile);meta.set("modelId",V::makeStr(model));meta.set("sessionKind",V::makeStr("headless"));meta.set("x.ai/mcp/servers",servers);
    auto params=obj();params.set("cwd",V::makeStr(".")); // Replaced by the CLI process's isolated current directory below.
    params.set("mcpServers",array());params.set("_meta",meta);
    // initialize's currentWorkingDirectory is the exact cwd the owned child uses.
    const auto* initMeta=initialized.find("_meta");auto cwd=initMeta?str(*initMeta,"currentWorkingDirectory"):"";need(!cwd.empty(),"CLI did not report its isolated current working directory.");params.set("cwd",V::makeStr(cwd));io->send(rpc(V::makeStr("ark_new"),"session/new",params));
    std::string sessionId;bool active=false,restart=false,serverInitialized=false,listed=false,inspectionRequired=false;std::string promptText;std::set<std::string> reverseIds,mcpIds,pendingFeedback;std::string feedbackProject,lastCompletionIssue;bool feedbackReminded=false;auto pendingMessages=array();
    for(const auto& row:result.conversation.arr){const auto content=str(row,"content");if(content.rfind("ARK_ACP_INSPECT_REQUIRED",0)==0)inspectionRequired=true;else if(content.rfind("ARK_ACP_INSPECTION_CONFIRMED",0)==0)inspectionRequired=false;}
    auto checkpoint=[&]{if(history)history(result.conversation);};
    auto emit=[&](AssistantEvent::Kind kind,const std::string& text){if(events){AssistantEvent event;event.kind=kind;event.text=text;event.usage=result.usage;event.performance=result.performance;events(event);}};
    auto track=[&](const V& receipt){const auto* data=receipt.find("structuredContent");if(!data)data=&receipt;if(const auto* p=data->find("activity_project_id");p&&p->t==V::Str&&p->str!=feedbackProject){feedbackProject=p->str;pendingFeedback.clear();}if(const auto* p=data->find("activity_id");p&&p->t==V::Str&&flag(data,"feedback_required"))pendingFeedback.insert(p->str);if(const auto* p=data->find("rated_ids");p&&p->t==V::Arr)for(const auto& id:p->arr)pendingFeedback.erase(id.asStr());if(const auto* p=data->find("pending_feedback_ids");p&&p->t==V::Arr)for(const auto& id:p->arr)pendingFeedback.insert(id.asStr());};
    for(const auto& row:result.conversation.arr)if(str(row,"type")=="function_call_output"){V receipt;if(arkjson::parse(str(row,"output"),receipt))track(receipt);}
    auto consume=[&]{if(!inbox)return false;auto batch=inbox();need(batch.isNull()||batch.t==V::Arr,"Invalid ACP steering mailbox.");bool changed=false;if(batch.t==V::Arr)for(auto& row:batch.arr){need(!imageIn(row),"Grok ACP does not accept this image steering input.");const auto role=str(row,"role");need(role=="user"||role=="developer","Invalid ACP steering role.");result.conversation.arr.push_back(row);pendingMessages.arr.push_back(row);changed=true;}if(changed)checkpoint();return changed;};
    auto prompt=[&](bool first){stopped(cancel);if(hooks.checkpoint)hooks.checkpoint();consume();V textInput=first?result.conversation:pendingMessages;pendingMessages=array();
        const auto request=assistantRequest(config,textInput.arr.empty()?result.conversation:textInput,tools);std::string text="Use only the registered ark tools through use_tool. CAD writes must use the current expected_revision and the ARK host's atomic contracts. Retained tool receipts are actual past operations: inspect current doc_info and never replay them. A final reply is not a completed Team goal.\n"+str(request,"instructions")+"\nRetained context and current input:\n"+request.find("input")->dump();need(text.size()<=config.maxConversationBytes,"ACP prompt exceeded the configured conversation bound.");
        auto content=array(),part=obj();part.set("type",V::makeStr("text"));part.set("text",V::makeStr(text));content.arr.push_back(part);auto p=obj();p.set("sessionId",V::makeStr(sessionId));p.set("prompt",content);++result.rounds;++result.performance.providerRequests;result.performance.lastRequestBytes=p.dump().size();result.performance.requestBytes+=result.performance.lastRequestBytes;result.performance.toolSchemaBytes=catalog.dump().size();io->send(rpc(V::makeStr("ark_prompt_"+std::to_string(result.rounds)),"session/prompt",p));active=true;restart=false;promptText.clear();emit(AssistantEvent::Status,"Grok Build / CLI account / working");};
    checkpoint();auto lastPacket=Clock::now(),promptBegan=Clock::now();
    try{for(;;){stopped(cancel);if(hooks.checkpoint)hooks.checkpoint();if(active&&consume()&&!restart){auto p=obj();p.set("sessionId",V::makeStr(sessionId));auto packet=rpc(V{},"session/cancel",p);packet.obj.erase(std::remove_if(packet.obj.begin(),packet.obj.end(),[](const auto& v){return v.first=="id";}),packet.obj.end());io->send(packet);restart=true;emit(AssistantEvent::Status,"User steering received; cancelling the old Grok turn before further writes");}
        auto received=io->receive(std::chrono::milliseconds(20));if(!received){need(Clock::now()-lastPacket<std::chrono::seconds(config.timeoutSeconds),"Grok Build ACP inactivity timeout; prior receipts retained. Inspect the current revision.");continue;}lastPacket=Clock::now();const auto& packet=*received;need(str(packet,"jsonrpc")=="2.0","Invalid ACP envelope.");auto method=str(packet,"method");if(method.rfind("_x.ai/",0)==0)method.erase(0,1);const auto* id=packet.find("id");
        if(!method.empty()){
            const auto* p=packet.find("params");need(p&&p->t==V::Obj,"ACP request needs complete object parameters.");
            if(!id){if(method=="session/update"){need(str(*p,"sessionId")==sessionId,"ACP update belongs to another session.");const auto* update=p->find("update");if(update&&str(*update,"sessionUpdate")=="agent_message_chunk"){const auto* content=update->find("content");need(content&&str(*content,"type")=="text","Unsupported ACP message chunk.");promptText+=str(*content,"text");need(promptText.size()<=config.maxResponseBytes,"ACP text exceeded the configured response bound.");}}continue;}
            need(reverseIds.insert(id->dump()).second,"Repeated ACP reverse request ID; no operation was replayed.");
            if(method!="x.ai/mcp/sdk_call"){if(method=="session/request_permission"){auto outcome=obj();outcome.set("outcome",V::makeStr("cancelled"));auto result=obj();result.set("outcome",outcome);io->send(reply(*id,result));}else if(method=="x.ai/hooks/run"){auto denial=obj();denial.set("decision",V::makeStr("deny"));denial.set("systemMessage",V::makeStr("External tools are disabled for this ARK role."));io->send(reply(*id,denial));}else io->send(rpcError(*id,-32601,"ARK exposes only its role-bound SDK MCP server; filesystem and terminal requests are denied."));continue;}
            need(str(*p,"serverId")==serverId,"ACP attempted to access a foreign SDK MCP server.");const auto* mcp=p->find("message");need(mcp&&mcp->t==V::Obj&&str(*mcp,"jsonrpc")=="2.0"&&mcp->find("id"),"Incomplete SDK MCP request; no tool was dispatched.");const auto& mid=*mcp->find("id");need(mcpIds.insert(mid.dump()).second,"Repeated MCP request ID; no operation was replayed.");const auto m=str(*mcp,"method");V response;
            if(m=="initialize"){serverInitialized=true;auto r=obj(),caps=obj(),info=obj();caps.set("tools",obj());info.set("name",V::makeStr("ark-role"));info.set("version",V::makeStr("1"));r.set("protocolVersion",V::makeStr("2025-11-25"));r.set("capabilities",caps);r.set("serverInfo",info);response=reply(mid,r);}
            else if(m=="tools/list"){need(serverInitialized,"MCP discovery before initialization.");listed=true;auto r=obj();r.set("tools",catalog);response=reply(mid,r);}
            else if(m=="ping")response=reply(mid,obj());
            else if(m=="tools/call"){
                need(active&&listed&&!sessionId.empty(),"CAD tool requested outside an active initialized ACP turn.");const auto* args=mcp->find("params");need(args&&args->t==V::Obj,"Incomplete MCP tool request.");const auto name=str(*args,"name");const auto* arguments=args->find("arguments");need(allowed.count(name)&&arguments&&arguments->t==V::Obj,"Unsupported role tool or incomplete arguments; no tool executed.");
                auto call=obj();const auto callId="acp_"+serverId+"_"+id->dump();call.set("type",V::makeStr("function_call"));call.set("call_id",V::makeStr(callId));call.set("name",V::makeStr(name));call.set("arguments",V::makeStr(arguments->dump()));result.conversation.arr.push_back(call);checkpoint();V receipt;
                if(restart||(hooks.requestCurrent&&!hooks.requestCurrent()))receipt=error("New user steering invalidated this old ACP tool request; inspect current state before another write.");else if(inspectionRequired&&!assistantToolReadOnly(tools,name,*arguments))receipt=error("A previous ACP turn was interrupted. Read current doc_info successfully before any further write; never replay retained edits.");else{emit(AssistantEvent::ToolStart,name);auto began=Clock::now();try{need((bool)callTool,"Missing host ARK tool executor.");receipt=callTool(name,*arguments);}catch(const std::exception&){receipt=error("Host tool execution was interrupted; it may have completed. Inspect the current revision before retrying.");inspectionRequired=true;}++result.performance.toolCalls;result.performance.lastToolMilliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-began).count();result.performance.toolMilliseconds+=result.performance.lastToolMilliseconds;emit(AssistantEvent::ToolEnd,name);}
                bool inspectionConfirmed=false;if(name=="doc_info"&&inspectionRequired){const auto* data=receipt.find("structuredContent");if(!data)data=&receipt;const auto* revision=data->find("revision");const auto* failed=receipt.find("isError");if(failed&&failed->t==V::Bool&&!failed->b&&revision&&revision->t==V::Num&&std::isfinite(revision->num)&&revision->num>=0&&std::floor(revision->num)==revision->num){inspectionRequired=false;inspectionConfirmed=true;}}
                track(receipt);if(receipt.dump().size()>config.maxResponseBytes)receipt=error("Tool result exceeded the ACP bound; execution may already have completed. Inspect compact current evidence.");auto retained=obj();retained.set("type",V::makeStr("function_call_output"));retained.set("call_id",V::makeStr(callId));retained.set("output",V::makeStr(receipt.dump()));result.conversation.arr.push_back(retained);if(inspectionConfirmed)result.conversation.arr.push_back(message("developer","ARK_ACP_INSPECTION_CONFIRMED: a real host doc_info receipt established the current revision."));checkpoint();stopped(cancel);response=reply(mid,receipt);
            }else response=rpcError(mid,-32601,"MCP method is not exposed by this ARK role.");io->send(reply(*id,response));continue;
        }
        need(id!=nullptr,"ACP response has no request ID.");if(str(packet,"id")=="ark_new"){need(!packet.find("error")&&packet.find("result"),"Grok Build session creation failed. Complete CLI login and check model eligibility; no API fallback was used.");sessionId=str(*packet.find("result"),"sessionId");need(!sessionId.empty(),"Grok Build did not create an ACP session.");promptBegan=Clock::now();prompt(true);continue;}
        need(active&&str(packet,"id")=="ark_prompt_"+std::to_string(result.rounds),"Unexpected ACP response ID.");active=false;result.usage.missingResponses++;result.performance.lastProviderMilliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-promptBegan).count();result.performance.providerMilliseconds+=result.performance.lastProviderMilliseconds;emit(AssistantEvent::Usage,assistantUsageLabel(result.usage));need(!packet.find("error")&&packet.find("result"),"Grok Build turn failed; earlier tool commits and receipts remain. Inspect current doc_info before continuing.");
        auto reason=str(*packet.find("result"),"stopReason");need(reason=="end_turn"||(restart&&reason=="cancelled"),"Grok Build did not complete its turn. Prior receipts remain; goal completion was not declared.");if(consume())restart=true;
        if(!restart){need(!promptText.empty(),"Grok Build ended without a visible final response; inspect prior tool receipts.");result.conversation.arr.push_back(message("assistant",promptText));checkpoint();if(!pendingFeedback.empty()&&allowed.count("tool_feedback")){need(!feedbackReminded,"Grok Build omitted required tool feedback after its reminder; prior work retained.");feedbackReminded=true;auto reminder=message("developer","Submit tool_feedback for the actual pending activity IDs: "+[&]{auto a=array();for(const auto& key:pendingFeedback)a.arr.push_back(V::makeStr(key));return a.dump();}());result.conversation.arr.push_back(reminder);pendingMessages.arr.push_back(reminder);restart=true;}
            if(!restart&&hooks.completionCheck){auto check=hooks.completionCheck();const auto* accepted=check.find("allowed");need(accepted&&accepted->t==V::Bool,"ARK completion check unavailable; prior work retained.");if(!accepted->b){auto issue=str(check,"error");need(!issue.empty()&&issue!=lastCompletionIssue,"ARK finalization remains blocked; inspect the retained evidence.");lastCompletionIssue=issue;auto reminder=message("developer",issue);result.conversation.arr.push_back(reminder);pendingMessages.arr.push_back(reminder);restart=true;}else if(!str(check,"summary").empty())promptText+="\n\n"+str(check,"summary");}
            if(!restart&&hooks.acceptCompletion&&!hooks.acceptCompletion())restart=true;
        }
        if(restart){checkpoint();if(config.maxRounds&&result.rounds>=config.maxRounds)throw AssistantRoundLimit(config.maxRounds);promptBegan=Clock::now();prompt(false);continue;}result.text=promptText;emit(AssistantEvent::Text,result.text);return result;
    }}catch(...){if(active){result.usage.missingResponses++;emit(AssistantEvent::Usage,assistantUsageLabel(result.usage));}std::set<std::string> missing;for(const auto& row:result.conversation.arr){if(str(row,"type")=="function_call")missing.insert(str(row,"call_id"));else if(str(row,"type")=="function_call_output")missing.erase(str(row,"call_id"));}for(const auto& id:missing){auto receipt=obj();receipt.set("type",V::makeStr("function_call_output"));receipt.set("call_id",V::makeStr(id));receipt.set("output",V::makeStr(error("ACP interrupted before a retained receipt; execution may have completed. Inspect current revision before any further write.").dump()));result.conversation.arr.push_back(receipt);}result.conversation.arr.push_back(message("developer","ARK_ACP_INSPECT_REQUIRED: the ACP turn did not finish successfully. Earlier host receipts may reflect committed work. Read current doc_info before further writes; goal completion was not declared."));checkpoint();throw;}
}
}
