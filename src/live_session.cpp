#include "ark/live_session.hpp"
#include "ark/agent.hpp"
#include "ark/agent_team.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#endif

namespace ark {
using V = arkjson::Value;
namespace {
constexpr std::size_t requestLimit = 4 * 1024 * 1024;
constexpr std::size_t responseLimit = 32 * 1024 * 1024;
// Bounded wait for one tool's Studio result; request/reply transfers keep 30 s.
constexpr unsigned liveExecutionLimitMs = 600000;
constexpr unsigned liveCallLimitMs = liveExecutionLimitMs + 60000;
// Progress notes for clients that request them; heartbeat plus optional text.
constexpr unsigned liveProgressIntervalMs = 2000;
constexpr unsigned liveInstances = 4;
V failure(const std::string& text) {
    V result=V::makeObj(), content=V::makeArr(), item=V::makeObj();
    item.set("type",V::makeStr("text")); item.set("text",V::makeStr(text));
    content.arr.push_back(item); result.set("content",content);
    result.set("isError",V::makeBool(true)); return result;
}
#ifdef _WIN32
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);}
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};
std::string path(const std::string& name) {
    if(name.empty()||name.size()>160)throw std::runtime_error("Invalid local ARK pipe name");
    for(unsigned char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))
        throw std::runtime_error("ARK pipe name must contain only letters, numbers, '-' or '_'");
    return "\\\\.\\pipe\\"+name;
}
using Clock=std::chrono::steady_clock;
DWORD remaining(Clock::time_point deadline) {
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count();
    return ms<=0?0:static_cast<DWORD>(ms);
}
bool finish(HANDLE pipe, OVERLAPPED& ov, HANDLE stopped, Clock::time_point deadline, DWORD& count) {
    HANDLE events[2]={ov.hEvent,stopped};
    DWORD status=WaitForMultipleObjects(stopped?2:1,events,FALSE,remaining(deadline));
    if(status!=WAIT_OBJECT_0) {
        CancelIoEx(pipe,&ov);
        // Cancellation must finish before OVERLAPPED and its buffers leave scope.
        GetOverlappedResult(pipe,&ov,&count,TRUE);
        return false;
    }
    return GetOverlappedResult(pipe,&ov,&count,FALSE)!=FALSE;
}
bool transfer(HANDLE pipe, void* buffer, DWORD size, bool write, HANDLE stopped, Clock::time_point deadline) {
    auto* bytes=static_cast<char*>(buffer);
    while(size) {
        Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr)); if(!event.value)return false;
        OVERLAPPED ov{};ov.hEvent=event.value;DWORD count=0;
        BOOL ok=write?WriteFile(pipe,bytes,size,&count,&ov):ReadFile(pipe,bytes,size,&count,&ov);
        if(!ok&&(GetLastError()!=ERROR_IO_PENDING||!finish(pipe,ov,stopped,deadline,count)))return false;
        if(!count)return false;
        bytes+=count;size-=count;
    }
    return true;
}
bool readFrame(HANDLE pipe,std::string& text,std::size_t limit,HANDLE stopped,Clock::time_point deadline) {
    std::uint32_t length=0;
    if(!transfer(pipe,&length,sizeof(length),false,stopped,deadline)||!length||length>limit)return false;
    text.resize(length);return transfer(pipe,text.data(),length,false,stopped,deadline);
}
bool writeFrame(HANDLE pipe,const std::string& text,HANDLE stopped,Clock::time_point deadline) {
    auto length=static_cast<std::uint32_t>(text.size());
    return transfer(pipe,&length,sizeof(length),true,stopped,deadline)&&
           transfer(pipe,const_cast<char*>(text.data()),length,true,stopped,deadline);
}
struct Reply {
    std::mutex mutex;std::condition_variable changed;std::optional<V> value;
    std::stop_source cancellation;
};
#endif
}
struct LiveSessionBridge::Impl {
    std::string name,error;
#ifdef _WIN32
    // Several instances let further clients connect and queue while one call
    // runs; the document dispatcher still executes calls one at a time.
    std::vector<HANDLE> pipes;HANDLE stopped=nullptr;
    std::vector<std::thread> workers;
    Progress progress;
    // A progress frame is {"ark_progress":{elapsed_ms,message?}}, sent only to a
    // client whose request asked for it, before the single result frame.
    bool sendProgress(HANDLE pipe,Clock::time_point began) {
        V note=V::makeObj(),frame=V::makeObj();
        note.set("elapsed_ms",V::makeNum((double)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-began).count()));
        std::string message;if(progress)try{message=progress();}catch(...){}
        if(!message.empty())note.set("message",V::makeStr(message.substr(0,512)));
        frame.set("ark_progress",note);
        return writeFrame(pipe,frame.dump(),stopped,Clock::now()+std::chrono::seconds(5));
    }
    void run(Dispatch dispatch,HANDLE pipe) {
        while(WaitForSingleObject(stopped,0)!=WAIT_OBJECT_0) {
            Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr)); if(!event.value)break;
            OVERLAPPED ov{};ov.hEvent=event.value;DWORD unused=0;
            BOOL connected=ConnectNamedPipe(pipe,&ov);DWORD code=connected?ERROR_SUCCESS:GetLastError();
            if(!connected&&code==ERROR_IO_PENDING)
                connected=finish(pipe,ov,stopped,Clock::now()+std::chrono::hours(24),unused);
            else if(code==ERROR_PIPE_CONNECTED)connected=TRUE;
            if(!connected){DisconnectNamedPipe(pipe);continue;}
            // The request and reply transfers stay bounded; a CAD evaluation may
            // legitimately run longer than the 30-second transfer window.
            const auto requestDeadline=Clock::now()+std::chrono::seconds(30);
            std::string text;V req;
            if(readFrame(pipe,text,requestLimit,stopped,requestDeadline)&&arkjson::parse(text,req)) {
                const auto deadline=Clock::now()+std::chrono::milliseconds(liveExecutionLimitMs);
                auto name=req.find("tool"),args=req.find("arguments");const auto* wants=req.find("progress");
                bool progressOpen=wants&&wants->t==V::Bool&&wants->b;
                V result=failure("Invalid live session request");
                if(name&&name->t==V::Str&&args&&args->t==V::Obj) {
                    auto reply=std::make_shared<Reply>();
                    try {
                        dispatch(name->str,*args,[reply](V value){
                            std::lock_guard lock(reply->mutex);
                            if(!reply->value&&!reply->cancellation.stop_requested())reply->value=std::move(value);
                            reply->changed.notify_all();
                        },reply->cancellation.get_token());
                        const auto began=Clock::now();auto nextProgress=began+std::chrono::milliseconds(liveProgressIntervalMs);
                        std::unique_lock lock(reply->mutex);
                        while(!reply->value&&remaining(deadline)&&WaitForSingleObject(stopped,0)!=WAIT_OBJECT_0){
                            reply->changed.wait_for(lock,std::chrono::milliseconds(10));
                            if(progressOpen&&!reply->value&&Clock::now()>=nextProgress){lock.unlock();progressOpen=sendProgress(pipe,began);nextProgress=Clock::now()+std::chrono::milliseconds(liveProgressIntervalMs);lock.lock();}
                        }
                        if(reply->value)result=std::move(*reply->value);
                        else result=failure("Studio call cancelled or timed out; outcome may be unknown. Inspect revision before retrying a mutation.");
                    }catch(const std::exception& e){result=failure(std::string("Studio dispatch failed: ")+e.what());}
                    catch(...){result=failure("Studio dispatch failed");}
                    reply->cancellation.request_stop();
                }
                text=result.dump();
                if(text.size()>responseLimit)text=failure("Studio result exceeds 32 MiB; request compact inspection instead").dump();
                const auto replyDeadline=Clock::now()+std::chrono::seconds(30);
                if(writeFrame(pipe,text,stopped,replyDeadline)) {
                    // A byte-mode pipe discards unread bytes on DisconnectNamedPipe.
                    // An explicit bounded receipt avoids blocking FlushFileBuffers.
                    char received=0;transfer(pipe,&received,1,false,stopped,replyDeadline);
                }
            }
            // No FlushFileBuffers: an unresponsive client must never block close.
            DisconnectNamedPipe(pipe);
        }
    }
#endif
};
LiveSessionBridge::LiveSessionBridge():impl_(std::make_unique<Impl>()){}
LiveSessionBridge::~LiveSessionBridge(){stop();}
std::string LiveSessionBridge::endpoint()const{return impl_->name;}
std::string LiveSessionBridge::lastError()const{return impl_->error;}
bool LiveSessionBridge::start(Dispatch dispatch,std::string name,Progress progress) {
    stop();impl_->error.clear();
#ifdef _WIN32
    impl_->progress=std::move(progress);
#else
    (void)progress;
#endif
#ifdef _WIN32
    if(!dispatch){impl_->error="Missing Studio dispatcher";return false;}
    try {
        if(name.empty()) {
            std::random_device random;
            name="ark-studio-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(random())+"-"+std::to_string(GetTickCount64());
        }
        std::string pipePath=path(name);
        HANDLE rawToken=nullptr;
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&rawToken))throw std::runtime_error("Cannot query current-user token");
        Handle token(rawToken);DWORD size=0;GetTokenInformation(token.value,TokenUser,nullptr,0,&size);
        std::vector<char> data(size);
        if(!size||!GetTokenInformation(token.value,TokenUser,data.data(),size,&size))throw std::runtime_error("Cannot query current-user SID");
        LPSTR sid=nullptr;
        if(!ConvertSidToStringSidA(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid))throw std::runtime_error("Cannot encode current-user SID");
        std::string descriptor="D:P(A;;GA;;;"+std::string(sid)+")";LocalFree(sid);
        PSECURITY_DESCRIPTOR security=nullptr;
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorA(descriptor.c_str(),SDDL_REVISION_1,&security,nullptr))throw std::runtime_error("Cannot create current-user pipe ACL");
        SECURITY_ATTRIBUTES attrs{sizeof(SECURITY_ATTRIBUTES),security,FALSE};
        // The first instance claims the name exclusively; the others join it.
        for(unsigned i=0;i<liveInstances;++i){
            HANDLE instance=CreateNamedPipeA(pipePath.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|(i?0:FILE_FLAG_FIRST_PIPE_INSTANCE),
                                            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,liveInstances,65536,65536,0,&attrs);
            if(instance==INVALID_HANDLE_VALUE){const auto code=GetLastError();LocalFree(security);throw std::runtime_error(std::string(i?"Cannot create a further local Studio pipe instance":"Cannot create exclusive local Studio pipe")+" (Windows error "+std::to_string(code)+")");}
            impl_->pipes.push_back(instance);
        }
        LocalFree(security);
        impl_->stopped=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!impl_->stopped)throw std::runtime_error("Cannot create Studio cancellation event");
        impl_->name=std::move(name);
        for(HANDLE instance:impl_->pipes)impl_->workers.emplace_back([this,dispatch,instance]{impl_->run(dispatch,instance);});
        return true;
    }catch(const std::exception& e){impl_->error=e.what();stop();return false;}
#else
    (void)dispatch;(void)name;impl_->error="Live Studio connections currently require Windows";return false;
#endif
}
void LiveSessionBridge::stop()noexcept {
#ifdef _WIN32
    if(impl_->stopped)SetEvent(impl_->stopped);
    for(auto& worker:impl_->workers)if(worker.joinable())worker.join();
    impl_->workers.clear();
    for(HANDLE instance:impl_->pipes)if(instance!=INVALID_HANDLE_VALUE)CloseHandle(instance);
    impl_->pipes.clear();
    if(impl_->stopped){CloseHandle(impl_->stopped);impl_->stopped=nullptr;}
#endif
    impl_->name.clear();
}
bool isStudioDiagnosticsTool(const std::string& tool) noexcept{return tool=="studio_team_diagnostics";}
V studioDiagnosticsToolSchema(){
    V tool;arkjson::parse(R"({"name":"studio_team_diagnostics","description":"Read bounded local Studio team event phases and attributed rejected-read/later-read evidence. Human diagnostic only; no conversations, provider requests, CAD writes or failure resolution. Team-observed document revision may be historical.","inputSchema":{"type":"object","properties":{"role_id":{"type":"string","minLength":1,"maxLength":256},"max_records":{"type":"integer","minimum":1,"maximum":8}},"additionalProperties":false},"annotations":{"readOnlyHint":true,"destructiveHint":false,"idempotentHint":true}})",tool);return tool;
}
V studioTeamDiagnostics(const AgentTeamSnapshot& snapshot,const V& args){
    constexpr std::size_t maxDataBytes=64*1024,maxRecordBytes=8192;
    auto envelope=[](const V& data,bool error){V result=V::makeObj(),content=V::makeArr(),part=V::makeObj();result.set("isError",V::makeBool(error));result.set("structuredContent",data);part.set("type",V::makeStr("text"));part.set("text",V::makeStr(data.dump()));content.arr.push_back(part);result.set("content",content);return result;};
    auto invalid=[&](const char* message){V error=V::makeObj();error.set("error_code",V::makeStr("INVALID_ARGUMENT"));error.set("error",V::makeStr(message));error.set("document_unchanged",V::makeBool(true));return envelope(error,true);};
    if(args.t!=V::Obj)return invalid("Studio diagnostics arguments must be an object.");
    std::string selected;std::size_t limit=8;
    for(const auto& entry:args.obj){if(entry.first=="role_id"){if(entry.second.t!=V::Str||entry.second.str.empty()||entry.second.str.size()>256)return invalid("role_id must be bounded nonempty text.");selected=entry.second.str;}else if(entry.first=="max_records"){if(entry.second.t!=V::Num||!std::isfinite(entry.second.num)||entry.second.num<1||entry.second.num>8||entry.second.num!=std::floor(entry.second.num))return invalid("max_records must be an integer from 1 to 8.");limit=(std::size_t)entry.second.num;}else return invalid("Unknown Studio diagnostic argument; no writes or control operations are accepted.");}
    if(!selected.empty()&&std::none_of(snapshot.roles.begin(),snapshot.roles.end(),[&](const auto& role){return role.id==selected;}))return invalid("No such role exists in this Studio team.");
    auto sensitive=[](const V& value,auto&& self,unsigned depth)->bool{
        if(depth>16)return true;
        if(value.t==V::Str){auto text=value.str;std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return (char)std::tolower(c);});return text.find("data:image/")!=std::string::npos||text.find("bearer ")!=std::string::npos;}
        if(value.t==V::Obj)for(const auto& entry:value.obj){std::string key;for(unsigned char c:entry.first)if(std::isalnum(c))key+=(char)std::tolower(c);for(const char* denied:{"credential","password","secret","apikey","authorization","accesstoken","refreshtoken","image","base64","conversation","prompt"})if(key.find(denied)!=std::string::npos)return true;if(self(entry.second,self,depth+1))return true;}
        if(value.t==V::Arr)for(const auto& item:value.arr)if(self(item,self,depth+1))return true;
        return false;
    };
    static const V tools=AgentSession::tools();
    auto record=[&](const V& source){V result=V::makeObj();for(const char* key:{"tool","args","revision","read_sequence","project_epoch","same_subject_rejected_read_sequences","query_index","batch_tool","error_code","error","stage","operation","tool_succeeded","requested_fields","missing_fields","available_fields","recovery"})if(const auto* value=source.find(key))result.set(key,*value);return result;};
    V out=V::makeObj(),rows=V::makeArr();out.set("schema",V::makeStr("ark.studio-team-diagnostics.v1"));out.set("read_only",V::makeBool(true));out.set("document_unchanged",V::makeBool(true));out.set("team_started",V::makeBool(snapshot.started));out.set("running",V::makeBool(snapshot.running));out.set("paused",V::makeBool(snapshot.paused));out.set("cancelled",V::makeBool(snapshot.cancelled));out.set("goal_state",V::makeStr(agentGoalStateName(snapshot.goalState)));out.set("document_revision_observed",V::makeBool(snapshot.documentRevisionObserved));if(snapshot.documentRevisionObserved)out.set("document_revision",V::makeNum((double)snapshot.documentRevision));out.set("revision_scope",V::makeStr("Last document revision observed by this team; inspect doc_info for the current CAD revision."));out.set("elapsed_ms",V::makeNum((double)snapshot.elapsedMs));out.set("clock",V::makeStr("Monotonic milliseconds since team start."));out.set("phase_scope",V::makeStr("Observed host event phase. Assistant includes preparation and provider wait; this is not a provider-hang diagnosis."));out.set("active_tool_scope",V::makeStr("Host ToolStart through actual ToolEnd, including document queue wait."));out.set("evidence_scope",V::makeStr("Existing attributed read receipts only. Same-subject evidence does not itself resolve a failure or authorize CAD changes."));out.set("record_byte_limit",V::makeNum(maxRecordBytes));out.set("response_byte_limit",V::makeNum(256*1024));out.set("roles",rows);
    bool partial=false;std::size_t omittedRoles=0;
    for(const auto& role:snapshot.roles){if(!selected.empty()&&role.id!=selected)continue;if(rows.arr.size()>=8){++omittedRoles;partial=true;continue;}
        V row=V::makeObj(),failures=V::makeArr(),reads=V::makeArr();row.set("role_id",V::makeStr(role.id));row.set("state",V::makeStr(agentTeamStateName(role.state)));row.set("task_state",V::makeStr(role.taskState));row.set("phase",V::makeStr(role.currentPhase));row.set("phase_started_ms",V::makeNum((double)role.phaseStartedMs));row.set("last_event_ms",V::makeNum((double)role.lastEventMs));row.set("active_tool",V::makeStr(role.activeTool));if(!role.activeTool.empty())row.set("active_tool_started_ms",V::makeNum((double)role.activeToolStartedMs));row.set("unresolved_tool_errors",V::makeNum(role.unresolvedToolErrors));row.set("request_context",assistantContextMetrics(role.performance));row.set("rejected_reads",failures);row.set("later_read_evidence",reads);
        std::size_t omittedFailures=role.rejectedReadCount>role.rejectedReads.size()?role.rejectedReadCount-role.rejectedReads.size():0,omittedReads=role.laterReadEvidenceCount>role.laterReadEvidence.size()?role.laterReadEvidenceCount-role.laterReadEvidence.size():0,sensitiveRecords=0;
        auto append=[&](const std::vector<V>& source,const char* key,V& accepted,std::size_t& omitted){for(const auto& original:source){const auto* tool=original.find("tool"),*arguments=original.find("args");if(accepted.arr.size()>=limit||!tool||tool->t!=V::Str||!arguments||arguments->t!=V::Obj||arguments->dump().size()>2048||!assistantToolReadOnly(tools,tool->str,*arguments)){++omitted;continue;}V kept=record(original);if(sensitive(kept,sensitive,0)){++omitted;++sensitiveRecords;continue;}if(kept.dump().size()>maxRecordBytes){++omitted;continue;}V next=accepted;next.arr.push_back(kept);V candidate=row;candidate.set(key,next);V all=rows;all.arr.push_back(candidate);V measured=out;measured.set("roles",all);if(measured.dump().size()+4096>maxDataBytes){++omitted;continue;}accepted=std::move(next);row.set(key,accepted);}};
        append(role.rejectedReads,"rejected_reads",failures,omittedFailures);append(role.laterReadEvidence,"later_read_evidence",reads,omittedReads);row.set("omitted_rejected_reads",V::makeNum((double)omittedFailures));row.set("omitted_later_read_evidence",V::makeNum((double)omittedReads));row.set("omitted_sensitive_records",V::makeNum((double)sensitiveRecords));row.set("partial",V::makeBool(omittedFailures||omittedReads));partial=partial||omittedFailures||omittedReads;rows.arr.push_back(row);out.set("roles",rows);
    }
    out.set("partial",V::makeBool(partial));out.set("omitted_roles",V::makeNum((double)omittedRoles));return envelope(out,false);
}
V callLiveSession(const std::string& endpoint,const std::string& tool,const V& args,unsigned timeoutMs,const std::function<void(const V&)>& onProgress) {
    V request=V::makeObj();request.set("tool",V::makeStr(tool));request.set("arguments",args);if(onProgress)request.set("progress",V::makeBool(true));
    std::string text=request.dump();
    if(text.size()>requestLimit)throw std::runtime_error("Live Studio request exceeds 4 MiB");
#ifdef _WIN32
    if(timeoutMs<1||timeoutMs>liveCallLimitMs)throw std::runtime_error("Live Studio timeout must be 1..660000 ms");
    // Connecting stays bounded at 30 s (every pipe instance may be busy with
    // other clients' calls); the reply may wait for a long evaluation.
    std::string pipePath=path(endpoint);const auto began=Clock::now();
    const auto connectDeadline=began+std::chrono::milliseconds(std::min(timeoutMs,30000u)),deadline=began+std::chrono::milliseconds(timeoutMs);
    HANDLE raw=INVALID_HANDLE_VALUE;
    while(remaining(connectDeadline)) {
        raw=CreateFileA(pipePath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
        if(raw!=INVALID_HANDLE_VALUE)break;
        DWORD code=GetLastError();
        if(code!=ERROR_PIPE_BUSY)throw std::runtime_error("Studio disconnected or unavailable (Windows error "+std::to_string(code)+"). No separate document was created.");
        WaitNamedPipeA(pipePath.c_str(),remaining(connectDeadline));
    }
    if(raw==INVALID_HANDLE_VALUE)throw std::runtime_error("Timed out waiting for Studio connection; another live call may still be running. Nothing was sent.");
    Handle pipe(raw);
    const char* lost="Studio connection lost or timed out; outcome may be unknown. Inspect document revision before retrying a mutation.";
    if(!writeFrame(pipe.value,text,nullptr,deadline))throw std::runtime_error(lost);
    for(;;){
        if(!readFrame(pipe.value,text,responseLimit,nullptr,deadline))throw std::runtime_error(lost);
        V frame;if(!onProgress||!arkjson::parse(text,frame)||frame.t!=V::Obj||frame.obj.size()!=1||!frame.find("ark_progress"))break;
        try{onProgress(*frame.find("ark_progress"));}catch(...){} // A client display failure never changes the call outcome.
    }
    char received=1;transfer(pipe.value,&received,1,true,nullptr,deadline);
    V response;if(!arkjson::parse(text,response)||response.t!=V::Obj)throw std::runtime_error("Invalid Studio tool response");
    return response;
#else
    (void)endpoint;(void)timeoutMs;(void)onProgress;throw std::runtime_error("Live Studio connections currently require Windows");
#endif
}
}
