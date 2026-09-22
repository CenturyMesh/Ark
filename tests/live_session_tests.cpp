#include "ark/live_session.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#endif
using V=arkjson::Value;
int failures=0,checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';++failures;}}while(0)
template<class F>bool throws(F f){try{f();return false;}catch(const std::exception&){return true;}}
int main(){
#ifdef _WIN32
    ark::LiveSessionBridge bridge;
    CHECK(!bridge.start({},"unused"));
    CHECK(!bridge.start([](auto&,auto&,auto,std::stop_token){},"bad/name"));
    CHECK(bridge.start([](const std::string& tool,const V& args,auto done,std::stop_token){
        V result=V::makeObj();result.set("tool",V::makeStr(tool));result.set("arguments",args);done(result);
    }));
    const auto endpoint=bridge.endpoint();CHECK(!endpoint.empty());
    {
        // Inspect the kernel object's DACL instead of assuming that the SDDL
        // construction actually produced the intended single-user permission.
        HANDLE pipe=CreateFileA(("\\\\.\\pipe\\"+endpoint).c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,0,nullptr,OPEN_EXISTING,0,nullptr);
        CHECK(pipe!=INVALID_HANDLE_VALUE);
        if(pipe!=INVALID_HANDLE_VALUE) {
            PACL acl=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
            CHECK(GetSecurityInfo(pipe,SE_KERNEL_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,&descriptor)==ERROR_SUCCESS);
            CHECK(acl&&acl->AceCount==1);
            if(acl&&acl->AceCount==1) {
                void* ace=nullptr;CHECK(GetAce(acl,0,&ace)!=FALSE);
                auto* allowed=static_cast<ACCESS_ALLOWED_ACE*>(ace);
                CHECK(allowed&&allowed->Header.AceType==ACCESS_ALLOWED_ACE_TYPE);
                HANDLE token=nullptr;CHECK(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)!=FALSE);
                DWORD length=0;GetTokenInformation(token,TokenUser,nullptr,0,&length);std::vector<char> bytes(length);
                CHECK(GetTokenInformation(token,TokenUser,bytes.data(),length,&length)!=FALSE);
                CHECK(EqualSid(&allowed->SidStart,reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid)!=FALSE);
                CloseHandle(token);
            }
            if(descriptor)LocalFree(descriptor);
            // Send an invalid size directly; the server must reject before
            // allocating a payload or calling the document dispatcher.
            DWORD length=4*1024*1024+1,written=0;CHECK(WriteFile(pipe,&length,sizeof(length),&written,nullptr)!=FALSE);
            char byte=0;DWORD read=0;CHECK(!ReadFile(pipe,&byte,1,&read,nullptr));CloseHandle(pipe);
        }
    }
    ark::LiveSessionBridge duplicate;
    CHECK(!duplicate.start([](auto&,auto&,auto,std::stop_token){},endpoint));
    V args=V::makeObj();args.set("revision",V::makeNum(42));
    for(int i=0;i<5;++i){auto result=ark::callLiveSession(endpoint,"doc_info",args);CHECK(result.find("tool")->asStr()=="doc_info");CHECK(result.find("arguments")->find("revision")->num==42);}
    // Large multi-buffer reply proves the server waits for client receipt before
    // disconnecting, rather than discarding a queued response.
    args.set("payload",V::makeStr(std::string(200000,'x')));
    CHECK(ark::callLiveSession(endpoint,"echo",args).find("arguments")->find("payload")->str.size()==200000);
    args.set("payload",V::makeStr(std::string(4*1024*1024,'x')));
    CHECK(throws([&]{ark::callLiveSession(endpoint,"echo",args);}));
    bridge.stop();CHECK(bridge.endpoint().empty());
    CHECK(throws([&]{ark::callLiveSession(endpoint,"doc_info",V::makeObj(),100);}));
    // Idle accept is cancelled without waiting for a client.
    CHECK(bridge.start([](auto&,auto&,auto,std::stop_token){}));
    auto start=std::chrono::steady_clock::now();bridge.stop();
    CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(2));
    // A queued UI callback does not need to run to let shutdown complete.
    std::promise<void> entered;ark::LiveSessionBridge::Completion delayed;std::stop_token token;
    CHECK(bridge.start([&](auto&,auto&,auto done,std::stop_token cancelled){delayed=std::move(done);token=cancelled;entered.set_value();}));
    const auto pendingEndpoint=bridge.endpoint();
    auto pending=std::async(std::launch::async,[&]{
        try {auto result=ark::callLiveSession(pendingEndpoint,"doc_apply",V::makeObj());auto error=result.find("isError");return error&&error->b;}
        catch(const std::exception&){return true;}
    });
    CHECK(entered.get_future().wait_for(std::chrono::seconds(2))==std::future_status::ready);
    start=std::chrono::steady_clock::now();bridge.stop();
    CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(2));
    CHECK(token.stop_requested());CHECK(pending.get());delayed(V::makeObj());
    // Timeout never switches to a fresh document or replays a mutation.
    std::atomic<int> calls=0;
    CHECK(bridge.start([&](auto&,auto&,auto,std::stop_token){++calls;}));
    CHECK(throws([&]{ark::callLiveSession(bridge.endpoint(),"doc_apply",V::makeObj(),100);}));
    CHECK(calls==1);bridge.stop();
#else
    ark::LiveSessionBridge bridge;CHECK(!bridge.start([](auto&,auto&,auto,std::stop_token){}));
    CHECK(throws([&]{ark::callLiveSession("unavailable","doc_info",V::makeObj());}));
#endif
    std::cout<<checks<<" live bridge checks, "<<failures<<" failures\n";return failures?1:0;
}
