#include "ark/live_session.hpp"
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
    HANDLE pipe=INVALID_HANDLE_VALUE,stopped=nullptr;
    std::thread worker;
    void run(Dispatch dispatch) {
        while(WaitForSingleObject(stopped,0)!=WAIT_OBJECT_0) {
            Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr)); if(!event.value)break;
            OVERLAPPED ov{};ov.hEvent=event.value;DWORD unused=0;
            BOOL connected=ConnectNamedPipe(pipe,&ov);DWORD code=connected?ERROR_SUCCESS:GetLastError();
            if(!connected&&code==ERROR_IO_PENDING)
                connected=finish(pipe,ov,stopped,Clock::now()+std::chrono::hours(24),unused);
            else if(code==ERROR_PIPE_CONNECTED)connected=TRUE;
            if(!connected){DisconnectNamedPipe(pipe);continue;}
            const auto deadline=Clock::now()+std::chrono::seconds(30);
            std::string text;V req;
            if(readFrame(pipe,text,requestLimit,stopped,deadline)&&arkjson::parse(text,req)) {
                auto name=req.find("tool"),args=req.find("arguments");
                V result=failure("Invalid live session request");
                if(name&&name->t==V::Str&&args&&args->t==V::Obj) {
                    auto reply=std::make_shared<Reply>();
                    try {
                        dispatch(name->str,*args,[reply](V value){
                            std::lock_guard lock(reply->mutex);
                            if(!reply->value&&!reply->cancellation.stop_requested())reply->value=std::move(value);
                            reply->changed.notify_all();
                        },reply->cancellation.get_token());
                        std::unique_lock lock(reply->mutex);
                        while(!reply->value&&remaining(deadline)&&WaitForSingleObject(stopped,0)!=WAIT_OBJECT_0)
                            reply->changed.wait_for(lock,std::chrono::milliseconds(10));
                        if(reply->value)result=std::move(*reply->value);
                        else result=failure("Studio call cancelled or timed out; outcome may be unknown. Inspect revision before retrying a mutation.");
                    }catch(const std::exception& e){result=failure(std::string("Studio dispatch failed: ")+e.what());}
                    catch(...){result=failure("Studio dispatch failed");}
                    reply->cancellation.request_stop();
                }
                text=result.dump();
                if(text.size()>responseLimit)text=failure("Studio result exceeds 32 MiB; request compact inspection instead").dump();
                if(writeFrame(pipe,text,stopped,deadline)) {
                    // A byte-mode pipe discards unread bytes on DisconnectNamedPipe.
                    // An explicit bounded receipt avoids blocking FlushFileBuffers.
                    char received=0;transfer(pipe,&received,1,false,stopped,deadline);
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
bool LiveSessionBridge::start(Dispatch dispatch,std::string name) {
    stop();impl_->error.clear();
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
        impl_->pipe=CreateNamedPipeA(pipePath.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
                                    PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&attrs);
        LocalFree(security);
        if(impl_->pipe==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create exclusive local Studio pipe (Windows error "+std::to_string(GetLastError())+")");
        impl_->stopped=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!impl_->stopped)throw std::runtime_error("Cannot create Studio cancellation event");
        impl_->name=std::move(name);
        impl_->worker=std::thread([this,dispatch=std::move(dispatch)]{impl_->run(dispatch);});
        return true;
    }catch(const std::exception& e){impl_->error=e.what();stop();return false;}
#else
    (void)dispatch;(void)name;impl_->error="Live Studio connections currently require Windows";return false;
#endif
}
void LiveSessionBridge::stop()noexcept {
#ifdef _WIN32
    if(impl_->stopped)SetEvent(impl_->stopped);
    if(impl_->worker.joinable())impl_->worker.join();
    if(impl_->pipe!=INVALID_HANDLE_VALUE){CloseHandle(impl_->pipe);impl_->pipe=INVALID_HANDLE_VALUE;}
    if(impl_->stopped){CloseHandle(impl_->stopped);impl_->stopped=nullptr;}
#endif
    impl_->name.clear();
}
V callLiveSession(const std::string& endpoint,const std::string& tool,const V& args,unsigned timeoutMs) {
    V request=V::makeObj();request.set("tool",V::makeStr(tool));request.set("arguments",args);
    std::string text=request.dump();
    if(text.size()>requestLimit)throw std::runtime_error("Live Studio request exceeds 4 MiB");
#ifdef _WIN32
    if(timeoutMs<1||timeoutMs>30000)throw std::runtime_error("Live Studio timeout must be 1..30000 ms");
    std::string pipePath=path(endpoint);const auto deadline=Clock::now()+std::chrono::milliseconds(timeoutMs);
    HANDLE raw=INVALID_HANDLE_VALUE;
    while(remaining(deadline)) {
        raw=CreateFileA(pipePath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
        if(raw!=INVALID_HANDLE_VALUE)break;
        DWORD code=GetLastError();
        if(code!=ERROR_PIPE_BUSY)throw std::runtime_error("Studio disconnected or unavailable (Windows error "+std::to_string(code)+"). No separate document was created.");
        WaitNamedPipeA(pipePath.c_str(),remaining(deadline));
    }
    if(raw==INVALID_HANDLE_VALUE)throw std::runtime_error("Timed out waiting for Studio connection");
    Handle pipe(raw);
    if(!writeFrame(pipe.value,text,nullptr,deadline)||!readFrame(pipe.value,text,responseLimit,nullptr,deadline))
        throw std::runtime_error("Studio connection lost or timed out; outcome may be unknown. Inspect document revision before retrying a mutation.");
    char received=1;transfer(pipe.value,&received,1,true,nullptr,deadline);
    V response;if(!arkjson::parse(text,response)||response.t!=V::Obj)throw std::runtime_error("Invalid Studio tool response");
    return response;
#else
    (void)endpoint;(void)timeoutMs;throw std::runtime_error("Live Studio connections currently require Windows");
#endif
}
}
