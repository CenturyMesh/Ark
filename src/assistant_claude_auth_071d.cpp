#include "ark/assistant_claude_auth_071d.hpp"
#include "ark/assistant_claude_code.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace ark {
namespace {
bool cancelled(const std::atomic_bool* value){return value&&value->load();}
bool credentialOverride(){for(const char* name:{"CLAUDE_CODE_OAUTH_TOKEN","ANTHROPIC_API_KEY","ANTHROPIC_AUTH_TOKEN"})if(const char* value=std::getenv(name);value&&*value)return true;return false;}
#ifdef _WIN32
struct Handle {HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}Handle()=default;explicit Handle(HANDLE h):value(h){};Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;};
std::wstring wide(const std::string& value){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),(int)value.size(),nullptr,0);if(n<=0)throw std::runtime_error("Invalid CLI path");std::wstring result(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),(int)value.size(),result.data(),n);return result;}
std::wstring quote(const std::wstring& value){std::wstring result=L"\"";unsigned slashes=0;for(wchar_t c:value){if(c==L'\\'){++slashes;continue;}result.append(c==L'"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;}result.append(slashes*2,L'\\');return result+L'"';}
std::wstring environment(){LPWCH block=GetEnvironmentStringsW();if(!block)throw std::runtime_error("Cannot read process environment");std::vector<std::wstring> rows;for(const wchar_t* p=block;*p;p+=wcslen(p)+1){std::wstring item(p);auto eq=item.find(L'=',1);std::string name;for(size_t i=0;i<(eq==std::wstring::npos?item.size():eq);++i)name+=item[i]<128?(char)item[i]:'?';if(eq==std::wstring::npos||assistantClaudeCodeInheritsEnvironment(name))rows.push_back(std::move(item));}FreeEnvironmentStringsW(block);std::sort(rows.begin(),rows.end());std::wstring result;for(const auto& row:rows){result+=row;result+=L'\0';}result+=L'\0';return result;}
struct Workspace {std::filesystem::path path;Workspace(){static std::atomic_uint sequence{0};path=std::filesystem::temp_directory_path()/("ark-claude-auth-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(++sequence));if(!std::filesystem::create_directory(path)||!std::filesystem::create_directory(path/".git"))throw std::runtime_error("Cannot create authentication workspace");}~Workspace(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}};
ClaudeAuthProcessResult071 nativeRun(const std::string& executable,const std::vector<std::string>& arguments,bool interactive,const std::atomic_bool* cancel){
    ClaudeAuthProcessResult071 result;if(cancelled(cancel)){result.cancelled=true;return result;}
    Workspace work;auto exe=wide(executable),command=quote(exe);for(const auto& arg:arguments)command+=L" "+quote(wide(arg));auto env=environment();
    SECURITY_ATTRIBUTES security{sizeof security,nullptr,TRUE};Handle output,childOutput,input,error,process,job;
    STARTUPINFOW start{};start.cb=sizeof start;DWORD flags=CREATE_UNICODE_ENVIRONMENT|CREATE_SUSPENDED;
    if(interactive){flags|=CREATE_NEW_CONSOLE;start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_SHOWNORMAL;}
    else{
        if(!CreatePipe(&output.value,&childOutput.value,&security,0)||!SetHandleInformation(output.value,HANDLE_FLAG_INHERIT,0))throw std::runtime_error("Cannot create status channel");
        input.value=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
        error.value=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
        if(input.value==INVALID_HANDLE_VALUE||error.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot isolate status console");
        flags|=CREATE_NO_WINDOW;start.dwFlags=STARTF_USESTDHANDLES;start.hStdInput=input.value;start.hStdOutput=childOutput.value;start.hStdError=error.value;
        job.value=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job.value||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof limits))throw std::runtime_error("Cannot isolate status process");
    }
    PROCESS_INFORMATION info{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,interactive?FALSE:TRUE,flags,env.data(),work.path.c_str(),&start,&info))throw std::runtime_error("Cannot launch native CLI");
    process.value=info.hProcess;Handle thread(info.hThread);
    if(!interactive&&!AssignProcessToJobObject(job.value,process.value)){TerminateProcess(process.value,1);throw std::runtime_error("Cannot isolate status process");}
    if(childOutput.value){CloseHandle(childOutput.value);childOutput.value=nullptr;}
    if(ResumeThread(thread.value)==(DWORD)-1){TerminateProcess(process.value,1);throw std::runtime_error("Cannot run native CLI");}
    const auto began=std::chrono::steady_clock::now();
    auto drain=[&]{if(interactive)return;for(;;){DWORD available=0;if(!PeekNamedPipe(output.value,nullptr,0,nullptr,&available,nullptr)||!available)return;char buffer[4096];DWORD count=0;if(!ReadFile(output.value,buffer,std::min<DWORD>(available,sizeof buffer),&count,nullptr)||!count)return;if(result.output.size()+count>65536)throw std::runtime_error("Status metadata too large");result.output.append(buffer,count);}};
    try{
        while(WaitForSingleObject(process.value,25)==WAIT_TIMEOUT){drain();if(cancelled(cancel)){result.cancelled=true;TerminateProcess(process.value,1);WaitForSingleObject(process.value,5000);return result;}if(!interactive&&std::chrono::steady_clock::now()-began>std::chrono::seconds(30)){TerminateProcess(process.value,1);WaitForSingleObject(process.value,5000);throw std::runtime_error("Status check timed out");}}
        drain();DWORD code=1;if(!GetExitCodeProcess(process.value,&code))throw std::runtime_error("Cannot read CLI completion");result.exitCode=(int)code;return result;
    }catch(...){if(WaitForSingleObject(process.value,0)==WAIT_TIMEOUT){TerminateProcess(process.value,1);WaitForSingleObject(process.value,5000);}throw;}
}
#else
ClaudeAuthProcessResult071 nativeRun(const std::string&,const std::vector<std::string>&,bool,const std::atomic_bool*){throw std::runtime_error("Native account login requires Windows");}
#endif
}
struct AssistantClaudeAuth071::Impl {
    ClaudeAuthDependencies071 dependencies;mutable std::mutex mutex;ClaudeAuthState071 state;std::atomic_bool cancellation{false};std::thread worker;
    explicit Impl(ClaudeAuthDependencies071 d):dependencies(std::move(d)){}
    void phase(ClaudeAuthPhase071 phase,const char* message,bool running=true){std::lock_guard lock(mutex);state.phase=phase;state.message=message;state.running=running;++state.generation;}
    void work(AssistantConfig config,bool interactive){
        try{
            const bool overrideBefore=credentialOverride();
            const auto executable=assistantClaudeCodeExecutable(config);
            auto run=dependencies.run?dependencies.run:ClaudeAuthRunner071(nativeRun);
            if(interactive){auto login=run(executable,{"auth","login"},true,&cancellation);{std::lock_guard lock(mutex);state.loginExitCode=login.exitCode;}if(login.cancelled||cancelled(&cancellation)){phase(ClaudeAuthPhase071::Cancelled,"Claude Code sign-in was cancelled. CAD work is retained.",false);return;}if(login.exitCode!=0){phase(ClaudeAuthPhase071::Failed,"The official Claude Code sign-in did not finish successfully. Reconnect when ready.",false);return;}}
            phase(ClaudeAuthPhase071::Checking,"Checking the CLI account and SDK connection without sending a prompt...");
            auto status=run(executable,{"auth","status"},false,&cancellation);
            if(status.cancelled||cancelled(&cancellation)){phase(ClaudeAuthPhase071::Cancelled,"Claude Code account check was cancelled.",false);return;}
            arkjson::Value parsed;bool valid=arkjson::parse(status.output,parsed)&&parsed.t==arkjson::Value::Obj;
            if(!status.output.empty()){std::fill(status.output.begin(),status.output.end(),'\0');status.output.clear();}
            const auto* logged=valid?parsed.find("loggedIn"):nullptr;const auto* method=valid?parsed.find("authMethod"):nullptr;
            if(status.exitCode==1&&logged&&logged->t==arkjson::Value::Bool&&!logged->b){phase(ClaudeAuthPhase071::NotConnected,"Claude Code reports no signed-in account. Use Reconnect Claude Code.",false);return;}
            if(status.exitCode!=0||!logged||logged->t!=arkjson::Value::Bool||!logged->b||!method||method->t!=arkjson::Value::Str)throw std::runtime_error("Invalid account metadata");
            static const std::vector<std::string> methods{"claude.ai","oauth_token","api_key","api_key_helper","third_party"};
            if(std::find(methods.begin(),methods.end(),method->str)==methods.end())throw std::runtime_error("Unsupported account metadata");
            {std::lock_guard lock(mutex);state.loggedIn=true;state.authMethod=method->str;}
            auto probe=dependencies.probe?dependencies.probe(config,&cancellation):assistantClaudeCodeProbe(config,&cancellation);
            if(cancelled(&cancellation)){phase(ClaudeAuthPhase071::Cancelled,"Claude Code account check was cancelled.",false);return;}
            const auto* models=probe.find("models");const auto* listed=probe.find("sdk_mcp_listed");const auto* prompt=probe.find("prompt_sent");
            if(!models||models->t!=arkjson::Value::Arr||models->arr.empty()||!listed||listed->t!=arkjson::Value::Bool||!listed->b||!prompt||prompt->t!=arkjson::Value::Bool||prompt->b)throw std::runtime_error("SDK metadata check failed");
            const bool overrideAfter=credentialOverride();bool alternate=false;
            {std::lock_guard lock(mutex);state.sdkReady=true;state.externalCredentialOverride=overrideBefore||overrideAfter;alternate=state.externalCredentialOverride||state.authMethod!="claude.ai";state.renewedSavedLogin=interactive&&!alternate;}
            phase(ClaudeAuthPhase071::Ready,alternate?"CLI metadata verified, but an external token/API/provider route remains active. Browser login does not replace that credential; renew it through its owner. No model access was tested.":"CLI account and SDK metadata verified. No inference or model access was tested; resume manually.",false);
        }catch(...){phase(cancelled(&cancellation)?ClaudeAuthPhase071::Cancelled:ClaudeAuthPhase071::Failed,cancelled(&cancellation)?"Claude Code account operation was cancelled.":"Could not verify the Claude Code account. Check the selected native CLI and reconnect; CAD work is retained.",false);}
    }
};
AssistantClaudeAuth071::AssistantClaudeAuth071(ClaudeAuthDependencies071 dependencies):impl_(std::make_unique<Impl>(std::move(dependencies))){}
AssistantClaudeAuth071::~AssistantClaudeAuth071(){shutdown();}
bool AssistantClaudeAuth071::start(const AssistantConfig& config,bool interactive){validateAssistantClaudeCodeConfig(config);{std::lock_guard lock(impl_->mutex);if(impl_->state.running)return false;}if(impl_->worker.joinable())impl_->worker.join();impl_->cancellation=false;{std::lock_guard lock(impl_->mutex);const unsigned generation=impl_->state.generation+1;impl_->state={};impl_->state.generation=generation;impl_->state.running=true;impl_->state.interactiveLogin=interactive;impl_->state.phase=interactive?ClaudeAuthPhase071::Login:ClaudeAuthPhase071::Checking;impl_->state.message=interactive?"Finish sign-in in the official Claude Code console/browser. ARK keeps your current drawing.":"Checking Claude Code account metadata without inference...";}impl_->worker=std::thread([this,config,interactive]{impl_->work(config,interactive);});return true;}
ClaudeAuthState071 AssistantClaudeAuth071::snapshot()const{std::lock_guard lock(impl_->mutex);return impl_->state;}
void AssistantClaudeAuth071::cancel()noexcept{impl_->cancellation=true;}
void AssistantClaudeAuth071::shutdown()noexcept{cancel();if(impl_->worker.joinable())impl_->worker.join();}
}
