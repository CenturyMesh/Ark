#include "ark/assistant_claude_code.hpp"
#include "ark/assistant_acp.hpp"
#include "ark/assistant_connectors.hpp"
#include "assistant_vision.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace ark {
namespace {
using V=arkjson::Value;
using Clock=std::chrono::steady_clock;
void need(bool ok,const std::string& message){if(!ok)throw AssistantTransportError(message);}
std::string str(const V& value,const char* key){const auto* p=value.find(key);return p?p->asStr():"";}
V obj(){return V::makeObj();}
V array(){return V::makeArr();}
V message(const char* role,const std::string& text){auto value=obj();value.set("role",V::makeStr(role));value.set("content",V::makeStr(text));return value;}
void stopped(const std::atomic_bool* cancel){need(!cancel||!cancel->load(),"Claude Code cancelled; prior tool receipts remain authoritative. Inspect the current revision before continuing.");}
std::string env(const char* key){const char* value=std::getenv(key);return value?value:"";}
// CLI-generated identifiers and statuses only; never free text from the account record.
std::string clean(const std::string& text){std::string out;for(unsigned char c:text)if((std::isalnum(c)||c=='_'||c=='-'||c=='.'||c==' ')&&out.size()<64)out+=(char)c;return out;}
bool validId(const std::string& id){return !id.empty()&&id.size()<=256&&std::all_of(id.begin(),id.end(),[](unsigned char c){return c>=33&&c!=127;});}
bool validMcpId(const V& id){return (id.t==V::Str&&validId(id.str))||(id.t==V::Num&&std::isfinite(id.num)&&id.num>=0&&id.num<=9007199254740991.0&&std::floor(id.num)==id.num);}
constexpr std::size_t kMaxPromptImages=20;
// User images leave the JSON text as image blocks of the same stream-json user turn.
// The text keeps a numbered placeholder so each image stays tied to its message.
// Only validated inline PNG/JPEG data is sent; nothing is fetched, dropped or recoded.
V detachImages(V value,V& blocks){
    if(value.t==V::Arr){for(auto& item:value.arr)item=detachImages(std::move(item),blocks);return value;}
    if(value.t!=V::Obj)return value;
    if(const auto kind=str(value,"type");kind=="input_image"||kind=="image"){
        const V* url=value.find("image_url");if(url&&url->t==V::Obj)url=url->find("url");need(url!=nullptr,"Claude Code image input requires inline image data; no image was dropped.");
        std::pair<std::string,std::string> image;try{image=assistant_vision::dataUrl(*url);}catch(const std::exception& error){throw AssistantTransportError(std::string("Claude Code image input rejected: ")+error.what());}
        need(blocks.arr.size()<kMaxPromptImages,"Claude Code prompt exceeds "+std::to_string(kMaxPromptImages)+" images; no image was dropped.");
        auto source=obj(),block=obj(),placeholder=obj();source.set("type",V::makeStr("base64"));source.set("media_type",V::makeStr(image.first));source.set("data",V::makeStr(image.second));block.set("type",V::makeStr("image"));block.set("source",source);blocks.arr.push_back(std::move(block));
        placeholder.set("type",V::makeStr("input_image"));placeholder.set("attached_image",V::makeNum((double)blocks.arr.size()));return placeholder;}
    for(auto& entry:value.obj)entry.second=detachImages(std::move(entry.second),blocks);return value;}
std::optional<std::uint64_t> count(const V* object,const char* key){const auto* p=object?object->find(key):nullptr;if(!p||p->t!=V::Num||!std::isfinite(p->num)||p->num<0||p->num>9007199254740991.0||std::floor(p->num)!=p->num)return {};return (std::uint64_t)p->num;}
V toolError(const std::string& text){auto data=obj(),result=obj(),content=array(),item=obj();data.set("error",V::makeStr(text));item.set("type",V::makeStr("text"));item.set("text",V::makeStr(data.dump()));content.arr.push_back(item);result.set("content",content);result.set("structuredContent",data);result.set("isError",V::makeBool(true));return result;}
// MCP CallToolResult: content is required and structuredContent must be an object.
V forCli(V receipt){if(receipt.t!=V::Obj){auto wrapped=obj();wrapped.set("value",receipt);receipt=obj();receipt.set("structuredContent",wrapped);}
    if(const auto* data=receipt.find("structuredContent");data&&data->t!=V::Obj)receipt.obj.erase(std::remove_if(receipt.obj.begin(),receipt.obj.end(),[](const auto& kv){return kv.first=="structuredContent";}),receipt.obj.end());
    const auto* content=receipt.find("content");if(!content||content->t!=V::Arr){auto list=array(),item=obj();const auto* data=receipt.find("structuredContent");item.set("type",V::makeStr("text"));item.set("text",V::makeStr((data?*data:receipt).dump()));list.arr.push_back(item);receipt.set("content",list);}
    if(const auto* failed=receipt.find("isError");!failed||failed->t!=V::Bool)receipt.set("isError",V::makeBool(receipt.find("error")!=nullptr));return receipt;}
V mcpReply(const V& id,V result){auto packet=obj();packet.set("jsonrpc",V::makeStr("2.0"));packet.set("id",id);packet.set("result",std::move(result));return packet;}
V mcpError(const V& id,int code,const char* text){auto packet=obj(),detail=obj();packet.set("jsonrpc",V::makeStr("2.0"));packet.set("id",id);detail.set("code",V::makeNum(code));detail.set("message",V::makeStr(text));packet.set("error",detail);return packet;}
V controlRequest(const std::string& id,V request){auto packet=obj();packet.set("type",V::makeStr("control_request"));packet.set("request_id",V::makeStr(id));packet.set("request",std::move(request));return packet;}
V controlSuccess(const std::string& id,V response){auto packet=obj(),inner=obj();inner.set("subtype",V::makeStr("success"));inner.set("request_id",V::makeStr(id));inner.set("response",std::move(response));packet.set("type",V::makeStr("control_response"));packet.set("response",inner);return packet;}
V controlError(const std::string& id,const char* text){auto packet=obj(),inner=obj();inner.set("subtype",V::makeStr("error"));inner.set("request_id",V::makeStr(id));inner.set("error",V::makeStr(text));packet.set("type",V::makeStr("control_response"));packet.set("response",inner);return packet;}
const char* kRolePreamble="You are an ARK CAD role running inside ARK Studio through Claude Code. Use only the registered mcp__ark__ tools; built-in file, shell, web, skill and subagent tools are unavailable. CAD writes must use the current expected_revision and the ARK host's atomic contracts. Retained tool receipts are actual past operations: inspect current doc_info and never replay them. A final reply is not a completed Team goal.\n\n";
std::filesystem::path discoveredCli(){
#ifdef _WIN32
    const char* name="claude.exe";const char separator=';';const char* homeKey="USERPROFILE";
#else
    const char* name="claude";const char separator=':';const char* homeKey="HOME";
#endif
    std::error_code ignored;const auto path=env("PATH");
    for(std::size_t start=0;start<=path.size();){auto end=path.find(separator,start);if(end==std::string::npos)end=path.size();const auto folder=path.substr(start,end-start);if(!folder.empty()){const auto candidate=std::filesystem::u8path(folder)/name;if(std::filesystem::is_regular_file(candidate,ignored))return candidate;}start=end+1;}
    const auto home=env(homeKey);if(home.empty())return {};
    const auto local=std::filesystem::u8path(home)/".local"/"bin"/name;if(std::filesystem::is_regular_file(local,ignored))return local;
    // The VS Code extension ships the same native CLI; prefer its newest version.
    std::filesystem::path best;std::vector<long> bestVersion;const auto extensions=std::filesystem::u8path(home)/".vscode"/"extensions";
    try{if(std::filesystem::is_directory(extensions,ignored))for(const auto& entry:std::filesystem::directory_iterator(extensions,ignored)){const auto folder=entry.path().filename().string();const std::string prefix="anthropic.claude-code-";if(folder.rfind(prefix,0)!=0)continue;const auto candidate=entry.path()/"resources"/"native-binary"/name;if(!std::filesystem::is_regular_file(candidate,ignored))continue;std::vector<long> version;long part=-1;for(char c:folder.substr(prefix.size())){if(c>='0'&&c<='9'){part=(part<0?0:part*10)+(c-'0');if(part>1000000)break;}else{if(part>=0)version.push_back(part);part=-1;if(c!='.')break;}}if(part>=0)version.push_back(part);if(best.empty()||version>bestVersion){best=candidate;bestVersion=version;}}}catch(const std::filesystem::filesystem_error&){}
    return best;
}
#ifdef _WIN32
std::wstring wide(const std::string& text){if(text.empty())return {};int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),(int)text.size(),nullptr,0);need(size>0,"Invalid UTF-8 in Claude Code configuration.");std::wstring value(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),(int)text.size(),value.data(),size);return value;}
std::wstring quote(const std::wstring& text){std::wstring value=L"\"";unsigned slashes=0;for(auto c:text){if(c==L'\\'){++slashes;continue;}if(c==L'\"')value.append(slashes*2+1,L'\\');else value.append(slashes,L'\\');slashes=0;value+=c;}value.append(slashes*2,L'\\');return value+L'\"';}
struct OwnedHandle {HANDLE value=nullptr;~OwnedHandle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
#include "assistant_child_stdout.inl"
class NativeChannel final:public AssistantAcpChannel {
    HANDLE process=nullptr,job=nullptr,input=nullptr,output=nullptr;
    std::filesystem::path directory;
    std::string pending;
    std::size_t limit;
    unsigned timeoutSeconds;
    const std::atomic_bool* cancel;
    ChildStdout reader;
    std::string exitDetail(){std::ifstream in(directory/"stderr.txt",std::ios::binary);std::string text(512,'\0');in.read(text.data(),(std::streamsize)text.size());text.resize((std::size_t)std::max<std::streamsize>(0,in.gcount()));std::string out;for(unsigned char c:text)if(c>=32&&c<127)out+=(char)c;else if(!out.empty()&&out.back()!=' ')out+=' ';return out.empty()?"":" CLI stderr: "+out.substr(0,300);}
public:
    NativeChannel(const AssistantConfig& config,const V& roleTools,const std::string& systemPrompt,const std::atomic_bool* cancellation):limit(config.maxResponseBytes),timeoutSeconds(config.timeoutSeconds),cancel(cancellation){
        try{
            const auto cli=std::filesystem::u8path(assistantClaudeCodeExecutable(config));
            static std::atomic_uint64_t sequence{0};directory=std::filesystem::temp_directory_path()/("ark-claude-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(Clock::now().time_since_epoch().count())+"-"+std::to_string(++sequence));
            need(std::filesystem::create_directory(directory),"Cannot create isolated Claude Code workspace.");
            // A disposable git root stops project discovery. User settings still load so every
            // sign-in method the user configured for Claude Code keeps working unchanged.
            need(std::filesystem::create_directory(directory/".git"),"Cannot fence Claude Code project discovery.");
            const auto promptFile=directory/"ark-role.md";{std::ofstream out(promptFile,std::ios::binary);out<<systemPrompt;out.close();need((bool)out,"Cannot write the isolated Claude Code role prompt.");}
            const auto promptUtf8=promptFile.u8string();std::wstring command=quote(cli.wstring());
            for(const auto& argument:assistantClaudeCodeArguments(config,roleTools,std::string(promptUtf8.begin(),promptUtf8.end())))command+=L" "+quote(wide(argument));
            need(command.size()<32000,"The ARK role tool list is too long for the Claude Code command line.");
            // The CLI authenticates exactly as the user configured it; only parent Claude Code
            // session/host markers are dropped so the child starts as its own session.
            std::vector<std::wstring> environment;LPWCH block=GetEnvironmentStringsW();need(block!=nullptr,"Cannot prepare Claude Code process environment.");
            for(const wchar_t* cursor=block;*cursor;cursor+=wcslen(cursor)+1){std::wstring item(cursor);const auto equals=item.find(L'=',1);std::string name;for(std::size_t i=0;i<(equals==std::wstring::npos?item.size():equals);++i)name+=item[i]<128?(char)item[i]:'?';if(equals==std::wstring::npos||assistantClaudeCodeInheritsEnvironment(name))environment.push_back(item);}FreeEnvironmentStringsW(block);
            std::sort(environment.begin(),environment.end());std::wstring childEnv;for(const auto& item:environment){childEnv+=item;childEnv+=L'\0';}childEnv+=L'\0';
            SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};OwnedHandle childIn,childOut,childErr;
            need(CreatePipe(&childIn.value,&input,&sa,0)&&CreatePipe(&output,&childOut.value,&sa,0),"Cannot create Claude Code stdio pipes.");need(SetHandleInformation(input,HANDLE_FLAG_INHERIT,0)&&SetHandleInformation(output,HANDLE_FLAG_INHERIT,0),"Cannot restrict Claude Code handle inheritance.");
            childErr.value=CreateFileW((directory/"stderr.txt").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY,nullptr);need(childErr.value!=INVALID_HANDLE_VALUE,"Cannot capture Claude Code diagnostics.");
            STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;start.wShowWindow=SW_HIDE;start.hStdInput=childIn.value;start.hStdOutput=childOut.value;start.hStdError=childErr.value;
            job=CreateJobObjectW(nullptr,nullptr);need(job!=nullptr,"Cannot isolate Claude Code process lifetime.");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;need(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=FALSE,"Cannot configure Claude Code process lifetime.");
            PROCESS_INFORMATION info{};const BOOL made=CreateProcessW(cli.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT|CREATE_SUSPENDED,childEnv.data(),directory.c_str(),&start,&info);need(made!=FALSE,"Cannot start the configured Claude Code CLI.");process=info.hProcess;
            if(!AssignProcessToJobObject(job,process)){TerminateProcess(process,1);CloseHandle(info.hThread);throw AssistantTransportError("Cannot own the Claude Code process; inference was not started.");}ResumeThread(info.hThread);CloseHandle(info.hThread);reader.start(output,limit);
        }catch(...){close();throw;}
    }
    ~NativeChannel()override{close();}
    void close(){if(job){CloseHandle(job);job=nullptr;}if(process){WaitForSingleObject(process,2000);CloseHandle(process);process=nullptr;}reader.stop();if(input){CloseHandle(input);input=nullptr;}if(output){CloseHandle(output);output=nullptr;}if(!directory.empty()){std::error_code ignored;std::filesystem::remove_all(directory,ignored);directory.clear();}}
    void send(const V& packet)override{std::string bytes=packet.dump()+"\n";need(bytes.size()<=32*1024*1024,"Claude Code request exceeded its transport bound.");stopped(cancel);std::mutex mutex;std::condition_variable ready;bool done=false,ok=true;std::thread writer([&]{DWORD written=0;std::size_t offset=0;while(offset<bytes.size()){if(!WriteFile(input,bytes.data()+offset,(DWORD)std::min<std::size_t>(65536,bytes.size()-offset),&written,nullptr)||!written){ok=false;break;}offset+=written;}{std::lock_guard lock(mutex);done=true;}ready.notify_one();});const auto deadline=Clock::now()+std::chrono::seconds(timeoutSeconds);bool aborted=false,cancelled=false;{std::unique_lock lock(mutex);while(!done){if((cancel&&cancel->load())||Clock::now()>=deadline){aborted=true;cancelled=cancel&&cancel->load();TerminateJobObject(job,ERROR_CANCELLED);CancelSynchronousIo(writer.native_handle());break;}ready.wait_for(lock,std::chrono::milliseconds(10));}}writer.join();
        need(!aborted&&ok,"Claude Code stdin write "+(cancelled?std::string("was cancelled"):aborted?"timed out: the CLI read no input for "+std::to_string(timeoutSeconds)+" s and was stopped":std::string("failed: the CLI disconnected"))+"; prior host receipts remain authoritative."+exitDetail());}
    std::optional<V> receive(std::chrono::milliseconds wait)override{
        const auto deadline=Clock::now()+wait;
        for(;;){auto newline=pending.find('\n');if(newline!=std::string::npos){std::string line=pending.substr(0,newline);pending.erase(0,newline+1);if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;V packet;need(arkjson::parse(line,packet)&&packet.t==V::Obj,"Claude Code returned malformed stream JSON; no partial packet was executed.");return packet;}
            need(pending.size()<=limit,"Claude Code packet exceeded the configured response bound.");
            if(!reader.take(pending,std::max(std::chrono::milliseconds(0),std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now())))){need(WaitForSingleObject(process,100)==WAIT_TIMEOUT,"Claude Code exited before completing the request."+exitDetail());throw AssistantTransportError("Claude Code stdout disconnected; prior receipts retained."+exitDetail());}
            if(pending.find('\n')==std::string::npos&&Clock::now()>=deadline)return {};
        }
    }
};
#endif
std::unique_ptr<AssistantAcpChannel> channel(const AssistantConfig& config,const V& tools,const std::string& systemPrompt,const std::atomic_bool* cancel){if(config.acpChannelFactory)return config.acpChannelFactory();
#ifdef _WIN32
    return std::make_unique<NativeChannel>(config,tools,systemPrompt,cancel);
#else
    (void)tools;(void)systemPrompt;(void)cancel;throw AssistantTransportError("The native Claude Code transport is currently available on Windows only.");
#endif
}
struct Session {
    const AssistantConfig& config;const V& tools;const AssistantTool& callTool;const AssistantEvents& events;const std::atomic_bool* cancel;const AssistantInbox& inbox;const AssistantHistory& history;const AssistantRetryHooks& hooks;
    AssistantResult result;V catalog=array(),initResponse,pendingMessages=array();std::set<std::string> allowed,controlIds,mcpIds,pendingFeedback;std::unique_ptr<AssistantAcpChannel> io;
    bool initDone=false,serverInitialized=false,listed=false,verified=false,active=false,restart=false,inspectionRequired=false,feedbackReminded=false;
    std::string promptText,feedbackProject,lastCompletionIssue,negotiatedModel;unsigned interrupts=0,calls=0;Clock::time_point lastPacket=Clock::now(),promptBegan=Clock::now();
    void checkpoint(){if(history)history(result.conversation);}
    void emit(AssistantEvent::Kind kind,const std::string& text,bool retry=false){if(events){AssistantEvent event;event.kind=kind;event.text=text;event.usage=result.usage;event.performance=result.performance;event.providerRetry=retry;events(event);}}
    void track(const V& receipt){const auto* data=receipt.find("structuredContent");if(!data)data=&receipt;if(const auto* p=data->find("activity_project_id");p&&p->t==V::Str&&p->str!=feedbackProject){feedbackProject=p->str;pendingFeedback.clear();}const auto* required=data->find("feedback_required");if(const auto* p=data->find("activity_id");p&&p->t==V::Str&&required&&required->t==V::Bool&&required->b)pendingFeedback.insert(p->str);if(const auto* p=data->find("rated_ids");p&&p->t==V::Arr)for(const auto& id:p->arr)pendingFeedback.erase(id.asStr());if(const auto* p=data->find("pending_feedback_ids");p&&p->t==V::Arr)for(const auto& id:p->arr)pendingFeedback.insert(id.asStr());}
    void begin(const V& input){
        if(input.t==V::Str){need(!input.str.empty(),"Enter a Claude Code message first.");result.conversation.arr.push_back(message("user",input.str));}else{need(input.t==V::Arr&&!input.arr.empty(),"Claude Code requires a nonempty prompt/history.");result.conversation=input;}
        for(const auto& row:result.conversation.arr)if(str(row,"role")=="user")validateAssistantUserMessage(row);
        if(!config.onboardingPack.empty()){bool loaded=false;for(const auto& row:result.conversation.arr)if(str(row,"role")=="developer"&&str(row,"content").rfind("ARK_PREBAKED_GUIDE_V1\n",0)==0)loaded=true;if(!loaded)result.conversation.arr.insert(result.conversation.arr.begin(),message("developer","ARK_PREBAKED_GUIDE_V1\n"+config.onboardingPack));}
        for(const auto& function:assistantFunctionTools(tools).arr){auto entry=obj();const auto name=str(function,"name");need(allowed.insert(name).second,"Duplicate ARK tool in the Claude Code role catalog.");entry.set("name",V::makeStr(name));if(function.find("description"))entry.set("description",*function.find("description"));entry.set("inputSchema",*function.find("parameters"));for(const auto& source:tools.arr)if(str(source,"name")==name&&source.find("annotations"))entry.set("annotations",*source.find("annotations"));catalog.arr.push_back(entry);}
        for(const auto& row:result.conversation.arr){const auto content=str(row,"content");if(content.rfind("ARK_ACP_INSPECT_REQUIRED",0)==0)inspectionRequired=true;else if(content.rfind("ARK_ACP_INSPECTION_CONFIRMED",0)==0)inspectionRequired=false;if(str(row,"type")=="function_call_output"){V receipt;if(arkjson::parse(str(row,"output"),receipt))track(receipt);}}
    }
    bool consume(){if(!inbox)return false;auto batch=inbox();need(batch.isNull()||batch.t==V::Arr,"Invalid Claude Code steering mailbox.");bool changed=false;if(batch.t==V::Arr)for(auto& row:batch.arr){const auto role=str(row,"role");need(role=="user"||role=="developer","Invalid Claude Code steering role.");result.conversation.arr.push_back(row);pendingMessages.arr.push_back(row);changed=true;}if(changed)checkpoint();return changed;}
    void prompt(bool first){stopped(cancel);if(hooks.checkpoint)hooks.checkpoint();consume();std::string text;V images=array();
        if(first){const auto request=assistantRequest(config,result.conversation,tools);text="Retained context and current input:\n"+detachImages(*request.find("input"),images).dump();}
        else{need(!pendingMessages.arr.empty(),"Claude Code continuation has no new host input.");text="New ARK host input since your last reply:\n"+detachImages(pendingMessages,images).dump();}
        if(!images.arr.empty())text+="\n"+std::to_string(images.arr.size())+" image(s) follow this text in order; input_image attached_image N is the Nth.";
        pendingMessages=array();need(text.size()<=config.maxConversationBytes,"Claude Code prompt exceeded the configured conversation bound.");
        auto user=message("user",text);if(!images.arr.empty()){auto content=array(),part=obj();part.set("type",V::makeStr("text"));part.set("text",V::makeStr(text));content.arr.push_back(std::move(part));for(auto& image:images.arr)content.arr.push_back(std::move(image));user.set("content",content);}
        auto packet=obj();packet.set("type",V::makeStr("user"));packet.set("message",user);packet.set("parent_tool_use_id",V{});packet.set("session_id",V::makeStr("default"));
        ++result.rounds;++result.performance.providerRequests;result.performance.lastRequestBytes=packet.dump().size();result.performance.requestBytes+=result.performance.lastRequestBytes;result.performance.maxRequestBytes=std::max(result.performance.maxRequestBytes,result.performance.lastRequestBytes);result.performance.toolSchemaBytes=catalog.dump().size();
        io->send(packet);active=true;restart=false;promptText.clear();promptBegan=Clock::now();lastPacket=Clock::now();emit(AssistantEvent::Status,"Claude Code / CLI account / working");}
    V toolCall(const V& request,const V& requestId){
        need(active&&verified&&listed,"CAD tool requested outside an active verified Claude Code turn.");
        const auto* args=request.find("params");need(args&&args->t==V::Obj,"Incomplete MCP tool request.");const auto name=str(*args,"name");const auto* arguments=args->find("arguments");
        need(allowed.count(name)&&arguments&&arguments->t==V::Obj,"Unsupported role tool or incomplete arguments; no tool executed.");
        const auto* meta=args->find("_meta");auto useId=meta?str(*meta,"claudecode/toolUseId"):"";if(!validId(useId)||useId.size()>128)useId=std::to_string(result.rounds)+"_"+requestId.dump();
        const auto callId="claude_"+useId+"_"+std::to_string(++calls);auto call=obj();call.set("type",V::makeStr("function_call"));call.set("call_id",V::makeStr(callId));call.set("name",V::makeStr(name));call.set("arguments",V::makeStr(arguments->dump()));result.conversation.arr.push_back(call);checkpoint();
        V receipt;
        if(restart||(hooks.requestCurrent&&!hooks.requestCurrent()))receipt=toolError("New user steering invalidated this old Claude Code tool request; inspect current state before another write.");
        else if(inspectionRequired&&!assistantToolReadOnly(tools,name,*arguments))receipt=toolError("A previous CLI turn was interrupted. Read current doc_info successfully before any further write; never replay retained edits.");
        else{emit(AssistantEvent::ToolStart,name);const auto began=Clock::now();try{need((bool)callTool,"Missing host ARK tool executor.");receipt=callTool(name,*arguments);}catch(const std::exception&){receipt=toolError("Host tool execution was interrupted; it may have completed. Inspect the current revision before retrying.");inspectionRequired=true;}
            ++result.performance.toolCalls;result.performance.lastToolMilliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-began).count();result.performance.toolMilliseconds+=result.performance.lastToolMilliseconds;emit(AssistantEvent::ToolEnd,name);}
        bool confirmed=false;if(name=="doc_info"&&inspectionRequired){const auto* data=receipt.find("structuredContent");if(!data)data=&receipt;const auto* revision=data->find("revision");const auto* failed=receipt.find("isError");if(failed&&failed->t==V::Bool&&!failed->b&&revision&&revision->t==V::Num&&std::isfinite(revision->num)&&revision->num>=0&&std::floor(revision->num)==revision->num){inspectionRequired=false;confirmed=true;}}
        track(receipt);if(receipt.dump().size()>config.maxResponseBytes)receipt=toolError("Tool result exceeded the Claude Code bound; execution may already have completed. Inspect compact current evidence.");
        auto retained=obj();retained.set("type",V::makeStr("function_call_output"));retained.set("call_id",V::makeStr(callId));retained.set("output",V::makeStr(receipt.dump()));result.conversation.arr.push_back(retained);
        if(confirmed)result.conversation.arr.push_back(message("developer","ARK_ACP_INSPECTION_CONFIRMED: a real host doc_info receipt established the current revision."));
        checkpoint();lastPacket=Clock::now();stopped(cancel);return receipt;}
    V mcp(const V& request){
        need(str(request,"server_name")=="ark","Claude Code attempted to access a foreign SDK MCP server; the session was stopped.");
        const auto* m=request.find("message");need(m&&m->t==V::Obj&&str(*m,"jsonrpc")=="2.0","Incomplete SDK MCP message; no tool was dispatched.");
        const auto method=str(*m,"method");const auto* id=m->find("id");auto envelope=obj();
        if(!id){auto ack=obj();ack.set("jsonrpc",V::makeStr("2.0"));ack.set("result",obj());envelope.set("mcp_response",ack);return envelope;}
        need(validMcpId(*id),"Invalid SDK MCP request ID.");need(mcpIds.insert(id->dump()).second,"Repeated MCP request ID; no operation was replayed.");V response;
        if(method=="initialize"){serverInitialized=true;auto r=obj(),caps=obj(),info=obj();const auto* params=m->find("params");const auto version=params?str(*params,"protocolVersion"):"";caps.set("tools",obj());info.set("name",V::makeStr("ark-role"));info.set("version",V::makeStr("1"));r.set("protocolVersion",V::makeStr(version.empty()?"2025-11-25":version));r.set("capabilities",caps);r.set("serverInfo",info);response=mcpReply(*id,r);}
        else if(method=="tools/list"){need(serverInitialized,"MCP discovery before initialization.");listed=true;auto r=obj();r.set("tools",catalog);response=mcpReply(*id,r);}
        else if(method=="ping")response=mcpReply(*id,obj());
        else if(method=="tools/call")response=mcpReply(*id,forCli(toolCall(*m,*id)));
        else response=mcpError(*id,-32601,"MCP method is not exposed by this ARK role.");
        mcpIds.erase(id->dump());envelope.set("mcp_response",response);return envelope;}
    void systemPacket(const V& packet){const auto subtype=str(packet,"subtype");
        if(subtype=="init"){const auto* list=packet.find("tools");need(list&&list->t==V::Arr,"Claude Code omitted its session tool list; no CAD tool was authorized.");
            for(const auto& tool:list->arr){const auto name=tool.asStr();need(name.rfind("mcp__ark__",0)==0&&allowed.count(name.substr(10)),"Claude Code exposed a tool outside the ARK role ("+clean(name)+"); the session was stopped before any CAD call.");}
            const auto* servers=packet.find("mcp_servers");need(servers&&servers->t==V::Arr&&servers->arr.size()==1&&str(servers->arr[0],"name")=="ark"&&str(servers->arr[0],"status")=="connected","Claude Code did not report exactly the connected ARK role server.");
            need(str(packet,"permissionMode")=="dontAsk","Claude Code did not apply the dontAsk permission mode.");if(!str(packet,"model").empty())negotiatedModel=clean(str(packet,"model"));verified=true;return;}
        if(subtype=="api_retry")emit(AssistantEvent::Status,"Claude Code is retrying a provider request",true);}
    void control(const V& packet){const auto id=str(packet,"request_id");need(validId(id),"Invalid Claude Code control request ID.");need(controlIds.insert(id).second,"Repeated Claude Code control request ID; no operation was replayed.");
        const auto* request=packet.find("request");need(request&&request->t==V::Obj,"Incomplete Claude Code control request.");const auto subtype=str(*request,"subtype");
        if(subtype=="mcp_message"){io->send(controlSuccess(id,mcp(*request)));return;}
        if(subtype=="can_use_tool"){emit(AssistantEvent::Status,"Claude Code permission request denied: ARK grants only its role-bound tools");auto decision=obj();decision.set("behavior",V::makeStr("deny"));decision.set("message",V::makeStr("ARK grants only its role-bound tools; built-in file, shell, web and subagent tools are unavailable."));io->send(controlSuccess(id,decision));return;}
        io->send(controlError(id,"ARK does not serve this Claude Code control request; hooks and external tools are disabled."));}
    void controlReply(const V& packet){const auto* body=packet.find("response");need(body&&body->t==V::Obj,"Incomplete Claude Code control response.");const auto id=str(*body,"request_id");
        if(id=="ark_init"){need(!initDone,"Repeated Claude Code initialize response.");need(str(*body,"subtype")=="success","Claude Code rejected the SDK session initialize request; no prompt was sent.");const auto* data=body->find("response");need(data&&data->t==V::Obj,"Claude Code initialize response is incomplete.");initResponse=*data;initDone=true;return;}
        if(id.rfind("ark_interrupt_",0)==0)return;
        throw AssistantTransportError("Unexpected Claude Code control response; no foreign response was accepted.");}
    bool finished(const V& packet){
        need(active,"Claude Code returned a result outside an active turn.");active=false;
        const auto* usage=packet.find("usage");const auto input=count(usage,"input_tokens"),output=count(usage,"output_tokens");
        if(input&&output){AssistantUsage delta;const auto cacheRead=count(usage,"cache_read_input_tokens").value_or(0);delta.inputTokens=*input+count(usage,"cache_creation_input_tokens").value_or(0)+cacheRead;delta.outputTokens=*output;delta.cachedInputTokens=cacheRead;if(const auto* details=usage->find("output_tokens_details"))delta.reasoningTokens=std::min<std::uint64_t>(*output,count(details,"thinking_tokens").value_or(0));delta.totalTokens=delta.inputTokens+delta.outputTokens;delta.reportedResponses=1;accumulateAssistantUsage(result.usage,delta);}else result.usage.missingResponses++;
        result.performance.lastProviderMilliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-promptBegan).count();result.performance.providerMilliseconds+=result.performance.lastProviderMilliseconds;emit(AssistantEvent::Usage,assistantUsageLabel(result.usage));
        const auto subtype=str(packet,"subtype");const auto* isError=packet.find("is_error");const bool failed=subtype!="success"||(isError&&isError->t==V::Bool&&isError->b);auto text=str(packet,"result");
        if(failed&&!restart){const bool known=subtype=="success"||subtype=="error_max_turns"||subtype=="error_during_execution"||subtype=="error_max_budget_usd"||subtype=="error_max_structured_output_retries";const auto status=count(&packet,"api_error_status");
            if(text.find("/login")!=std::string::npos||text.find("Not logged in")!=std::string::npos||(status&&*status==401))throw AssistantTransportError("Claude Code is not signed in. Run claude in a terminal and complete /login; no API fallback was used. Prior receipts remain.");
            throw AssistantTransportError("Claude Code did not complete its turn (result "+(known?subtype:std::string("unsupported"))+(status?", provider status "+std::to_string(*status):std::string())+"). Prior receipts remain; goal completion was not declared.");}
        if((!hooks.inputPending||hooks.inputPending())&&consume())restart=true;
        need(restart||!hooks.requestCurrent||hooks.requestCurrent(),"Host steering/ownership changed; the old Claude Code final reply was not accepted. Inspect current context before continuing.");
        if(!restart){if(text.empty())text=promptText;need(!text.empty(),"Claude Code ended without a visible final response; inspect prior tool receipts.");promptText=text;need(promptText.size()<=config.maxResponseBytes,"Claude Code text exceeded the configured response bound.");result.conversation.arr.push_back(message("assistant",promptText));checkpoint();
            if(!pendingFeedback.empty()&&allowed.count("tool_feedback")){need(!feedbackReminded,"Claude Code omitted required tool feedback after its reminder; prior work retained.");feedbackReminded=true;auto ids=array();for(const auto& key:pendingFeedback)ids.arr.push_back(V::makeStr(key));auto reminder=message("developer","Submit tool_feedback for the actual pending activity IDs: "+ids.dump());result.conversation.arr.push_back(reminder);pendingMessages.arr.push_back(reminder);restart=true;}
            if(!restart&&hooks.completionCheck){auto check=hooks.completionCheck();const auto* accepted=check.find("allowed");need(accepted&&accepted->t==V::Bool,"ARK completion check unavailable; prior work retained.");if(!accepted->b){auto issue=str(check,"error");need(!issue.empty()&&issue!=lastCompletionIssue,"ARK finalization remains blocked; inspect the retained evidence.");lastCompletionIssue=issue;auto reminder=message("developer",issue);result.conversation.arr.push_back(reminder);pendingMessages.arr.push_back(reminder);restart=true;}else if(!str(check,"summary").empty())promptText+="\n\n"+str(check,"summary");}
            if(!restart&&hooks.acceptCompletion&&!hooks.acceptCompletion())restart=true;}
        if(restart){checkpoint();if(config.maxRounds&&result.rounds>=config.maxRounds)throw AssistantRoundLimit(config.maxRounds);prompt(false);return false;}
        result.text=promptText;emit(AssistantEvent::Text,result.text);return true;}
    bool handle(const V& packet){const auto type=str(packet,"type");
        if(type=="control_request"){control(packet);return false;}
        if(type=="control_response"){controlReply(packet);return false;}
        if(type=="system"){systemPacket(packet);return false;}
        if(type=="assistant"&&active){const auto* body=packet.find("message");const auto* content=body?body->find("content"):nullptr;std::string text;if(content&&content->t==V::Arr)for(const auto& block:content->arr)if(str(block,"type")=="text")text+=str(block,"text");if(!text.empty()){need(text.size()<=config.maxResponseBytes,"Claude Code text exceeded the configured response bound.");promptText=text;}return false;}
        if(type=="result")return finished(packet);
        if(type=="rate_limit_event"){const auto* info=packet.find("rate_limit_info");const auto status=info?clean(str(*info,"status")):"";if(!status.empty()&&status!="allowed")emit(AssistantEvent::Status,"Claude Code rate limit status: "+status);}
        return false;} // User echoes, partial stream events and progress keep the session alive only.
    void handshake(){auto request=obj();request.set("subtype",V::makeStr("initialize"));request.set("hooks",V{});io->send(controlRequest("ark_init",request));
        const auto deadline=Clock::now()+std::chrono::seconds(config.timeoutSeconds);
        while(!(initDone&&listed)){stopped(cancel);need(Clock::now()<deadline,"Claude Code did not finish its SDK MCP handshake; no prompt or CAD call was sent.");auto packet=io->receive(std::chrono::milliseconds(20));if(packet)need(!handle(*packet),"Claude Code produced a result before any prompt.");}}
    void negotiate(){const auto* models=initResponse.find("models");need(models&&models->t==V::Arr,"Claude Code did not advertise its model catalog; update the CLI.");
        const V* chosen=nullptr;const auto wanted=config.model.empty()?std::string("default"):config.model;for(const auto& model:models->arr)if(str(model,"value")==wanted||str(model,"resolvedModel")==wanted){chosen=&model;break;}
        if(!config.model.empty())need(chosen!=nullptr,"Selected model is absent from the Claude Code CLI catalog; no replacement or API fallback was used.");
        if(!config.reasoningEffort.empty()){const auto* levels=chosen?chosen->find("supportedEffortLevels"):nullptr;need(levels&&levels->t==V::Arr&&std::any_of(levels->arr.begin(),levels->arr.end(),[&](const V& level){return level.asStr()==config.reasoningEffort;}),"The selected Claude Code model does not advertise this effort level; clear it or choose another.");}
        negotiatedModel=chosen?clean(str(*chosen,"resolvedModel").empty()?str(*chosen,"value"):str(*chosen,"resolvedModel")):clean(config.model);
        const auto* account=initResponse.find("account");const auto route=account?clean(str(*account,"apiProvider")):"";
        result.conversation.arr.push_back(message("developer","ARK_CLI_MODEL_NEGOTIATED: provider claude-code, CLI account"+(route.empty()?std::string():" ("+route+")")+", model "+(negotiatedModel.empty()?std::string("CLI default"):negotiatedModel)+". Usage comes from CLI result receipts."));}
    void loop(){for(;;){stopped(cancel);if(hooks.checkpoint)hooks.checkpoint();
        if(active&&!restart&&(!hooks.inputPending||hooks.inputPending())&&consume()){auto request=obj();request.set("subtype",V::makeStr("interrupt"));io->send(controlRequest("ark_interrupt_"+std::to_string(++interrupts),request));restart=true;emit(AssistantEvent::Status,"User steering received; interrupting the old Claude Code turn before further writes");}
        auto packet=io->receive(std::chrono::milliseconds(20));
        if(!packet){need(Clock::now()-lastPacket<std::chrono::seconds(config.timeoutSeconds),"Claude Code inactivity timeout; prior receipts retained. Inspect the current revision.");continue;}
        lastPacket=Clock::now();if(handle(*packet))return;}}
    void interrupted(){if(active){result.usage.missingResponses++;emit(AssistantEvent::Usage,assistantUsageLabel(result.usage));}if(!result.rounds)return;
        std::set<std::string> missing;for(const auto& row:result.conversation.arr){if(str(row,"type")=="function_call")missing.insert(str(row,"call_id"));else if(str(row,"type")=="function_call_output")missing.erase(str(row,"call_id"));}
        for(const auto& id:missing){auto receipt=obj();receipt.set("type",V::makeStr("function_call_output"));receipt.set("call_id",V::makeStr(id));receipt.set("output",V::makeStr(toolError("Claude Code interrupted before a retained receipt; execution may have completed. Inspect current revision before any further write.").dump()));result.conversation.arr.push_back(receipt);}
        result.conversation.arr.push_back(message("developer","ARK_ACP_INSPECT_REQUIRED: the Claude Code turn did not finish successfully. Earlier host receipts may reflect committed work. Read current doc_info before further writes; goal completion was not declared."));checkpoint();}
};
V probeTools(){V tools;arkjson::parse(R"([{"name":"doc_info","description":"Probe-only placeholder; no call is made.","inputSchema":{"type":"object","properties":{}},"annotations":{"readOnlyHint":true}}])",tools);return tools;}
}
void validateAssistantClaudeCodeConfig(const AssistantConfig& config){
    need(config.provider=="claude-code"&&config.authMode==AssistantAuthMode::CliAccount&&config.protocol==AssistantProtocol::ClaudeCode&&config.apiKeyEnv.empty(),"Claude Code requires the CLI-account route with no API-key variable; the CLI keeps its own login.");
    need(!config.endpoint.empty()&&config.endpoint.size()<=2048&&config.endpoint.find_first_of("\r\n\t\"")==std::string::npos,"Invalid Claude Code CLI path.");
    need(config.model.size()<=128&&std::all_of(config.model.begin(),config.model.end(),[](unsigned char c){return std::isalnum(c)||c=='-'||c=='.'||c=='_'||c==':'||c=='['||c==']'||c=='@';}),"Invalid Claude Code model name.");
    need(config.reasoningEffort.empty()||config.reasoningEffort=="low"||config.reasoningEffort=="medium"||config.reasoningEffort=="high"||config.reasoningEffort=="xhigh"||config.reasoningEffort=="max","Claude Code effort must be blank (CLI default) or low, medium, high, xhigh or max.");
    need(config.maxRounds<=32,"Assistant round limit must be zero (unlimited) or 1..32.");
    need(config.timeoutSeconds>=1&&config.timeoutSeconds<=300,"Assistant timeout must be between 1 and 300 seconds.");
    need(config.maxResponseBytes>=1024&&config.maxResponseBytes<=16*1024*1024,"Invalid assistant response size limit.");
    need(config.maxConversationBytes>=1024&&config.maxConversationBytes<=32*1024*1024,"Invalid assistant conversation size limit.");
}
std::string assistantClaudeCodeExecutable(const AssistantConfig& config){
    std::filesystem::path path;std::error_code ignored;
    if(config.endpoint=="claude"){path=discoveredCli();need(!path.empty(),"Claude Code CLI is unavailable. Install it and sign in with claude, or choose the native claude executable's full path.");}
    else{path=std::filesystem::u8path(config.endpoint);need(path.is_absolute()&&std::filesystem::is_regular_file(path,ignored),"Claude Code CLI path is unavailable. Choose the installed native claude executable's full path.");
#ifdef _WIN32
        auto extension=path.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return (char)std::tolower(c);});need(extension==".exe","Choose the native claude.exe; script shims cannot be started without a shell.");
#endif
    }
    const auto utf8=path.u8string();return std::string(utf8.begin(),utf8.end());
}
bool assistantClaudeCodeAvailable(const AssistantConfig& config){try{validateAssistantClaudeCodeConfig(config);if(config.acpChannelFactory)return true;(void)assistantClaudeCodeExecutable(config);return true;}catch(...){return false;}}
bool assistantClaudeCodeInheritsEnvironment(const std::string& name){std::string upper=name;std::transform(upper.begin(),upper.end(),upper.begin(),[](unsigned char c){return (char)std::toupper(c);});
    // Markers a parent Claude Code session or SDK host sets for its own children (observed
    // from a 2.1.x IDE session). Inherited, they bind ARK's role to that session's identity,
    // messaging socket/token and host features. User sign-in and routing settings still pass.
    for(const char* blocked:{"CLAUDECODE","CLAUDE_PID","CLAUDE_AGENT_SDK_VERSION","CLAUDE_CODE_ENTRYPOINT","CLAUDE_CODE_SSE_PORT","CLAUDE_CODE_SIMPLE","CLAUDE_CODE_CHILD_SESSION","CLAUDE_CODE_EMIT_STARTUP_TIMING","CLAUDE_CODE_ENABLE_SDK_FILE_CHECKPOINTING","CLAUDE_CODE_ENABLE_TASKS","CLAUDE_CODE_QUESTION_PREVIEW_FORMAT"})if(upper==blocked)return false;
    for(const char* prefix:{"CLAUDE_CODE_SESSION_","CLAUDE_CODE_MESSAGING_"})if(upper.rfind(prefix,0)==0)return false;return true;}
std::vector<std::string> assistantClaudeCodeArguments(const AssistantConfig& config,const V& roleTools,const std::string& systemPromptFile){
    validateAssistantClaudeCodeConfig(config);std::string allowed;
    for(const auto& function:assistantFunctionTools(roleTools).arr){const auto name=str(function,"name");need(!name.empty()&&name.size()<=54&&std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}),"Invalid ARK role tool name for Claude Code.");allowed+=(allowed.empty()?"":",")+std::string("mcp__ark__")+name;}
    need(!allowed.empty(),"Claude Code needs at least one ARK role tool.");need(!systemPromptFile.empty(),"Claude Code needs its isolated role prompt file.");
    std::vector<std::string> args{"-p","--input-format","stream-json","--output-format","stream-json","--verbose","--include-partial-messages",
        "--tools","","--strict-mcp-config","--mcp-config",R"({"mcpServers":{"ark":{"type":"sdk","name":"ark"}}})","--allowedTools",allowed,
        "--disallowedTools","Bash,PowerShell,Read,Write,Edit,NotebookEdit,Glob,Grep,WebFetch,WebSearch,Task,Agent,Skill",
        "--permission-mode","dontAsk","--no-session-persistence","--disable-slash-commands","--system-prompt-file",systemPromptFile};
    if(!config.model.empty()){args.push_back("--model");args.push_back(config.model);}
    if(!config.reasoningEffort.empty()){args.push_back("--effort");args.push_back(config.reasoningEffort);}
    return args;
}
V assistantClaudeCodeProbe(const AssistantConfig& config){
    validateAssistantClaudeCodeConfig(config);const auto tools=probeTools();const AssistantTool none;const AssistantEvents quiet;const AssistantInbox empty;const AssistantHistory ignored;const AssistantRetryHooks hooks;
    Session session{config,tools,none,quiet,nullptr,empty,ignored,hooks};session.begin(V::makeStr("probe"));session.io=channel(config,tools,kRolePreamble,nullptr);need((bool)session.io,"Missing Claude Code channel.");session.handshake();
    auto out=obj(),models=array();if(const auto* list=session.initResponse.find("models");list&&list->t==V::Arr)for(const auto& model:list->arr){auto row=obj();row.set("value",V::makeStr(clean(str(model,"value"))));row.set("resolved_model",V::makeStr(clean(str(model,"resolvedModel"))));auto levels=array();if(const auto* p=model.find("supportedEffortLevels");p&&p->t==V::Arr)for(const auto& level:p->arr)levels.arr.push_back(V::makeStr(clean(level.asStr())));row.set("effort_levels",levels);models.arr.push_back(row);}
    const auto* account=session.initResponse.find("account");out.set("cli_version",V::makeStr(clean(str(session.initResponse,"claude_code_version"))));out.set("api_provider",V::makeStr(account?clean(str(*account,"apiProvider")):""));out.set("subscription_type",V::makeStr(account?clean(str(*account,"subscriptionType")):""));
    out.set("models",models);out.set("sdk_mcp_listed",V::makeBool(session.listed));out.set("prompt_sent",V::makeBool(false));return out;
}
std::vector<std::string> assistantClaudeCodeModels(const AssistantConfig& config){const auto probe=assistantClaudeCodeProbe(config);std::vector<std::string> ids;if(const auto* models=probe.find("models"))for(const char* key:{"value","resolved_model"})for(const auto& model:models->arr){const auto id=str(model,key);if(!id.empty()&&std::find(ids.begin(),ids.end(),id)==ids.end())ids.push_back(id);}return ids;}
AssistantResult runAssistantClaudeCode(const AssistantConfig& config,const V& input,const V& tools,const AssistantTool& callTool,const AssistantEvents& events,const std::atomic_bool* cancel,const AssistantInbox& inbox,const AssistantHistory& history,const AssistantRetryHooks& hooks){
    validateAssistantClaudeCodeConfig(config);stopped(cancel);
    Session session{config,tools,callTool,events,cancel,inbox,history,hooks};session.begin(input);
    const auto request=assistantRequest(config,session.result.conversation,tools);const auto systemPrompt=kRolePreamble+str(request,"instructions");
    need(systemPrompt.size()<=config.maxConversationBytes,"Claude Code role instructions exceeded the configured conversation bound.");
    session.io=channel(config,tools,systemPrompt,cancel);need((bool)session.io,"Missing Claude Code channel.");
    try{session.checkpoint();session.handshake();session.negotiate();session.prompt(true);session.loop();return session.result;}
    catch(...){session.interrupted();throw;}
}
}
