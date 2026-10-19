#include "ark/live_session.hpp"
#include "ark/agent.hpp"
#include "ark/agent_team.hpp"
#include "ark/io.hpp"
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
static V parse(const std::string& text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Invalid synthetic diagnostic fixture");return value;}
static V diagnosticCall(const char* id,const char* name,const char* args){V result=parse(R"({"status":"completed","output":[{"type":"function_call"}]})");auto output=*result.find("output");output.arr[0].set("call_id",V::makeStr(id));output.arr[0].set("name",V::makeStr(name));output.arr[0].set("arguments",V::makeStr(args));result.set("output",output);return result;}
static void diagnosticContracts(){
    CHECK(ark::isStudioDiagnosticsTool("studio_team_diagnostics"));CHECK(!ark::isStudioDiagnosticsTool("team_context"));
    const auto schema=ark::studioDiagnosticsToolSchema();CHECK(schema.find("annotations")->find("readOnlyHint")->b);
    bool agentExposed=false;for(const auto& tool:ark::AgentSession::tools().arr)if(tool.find("name")->str=="studio_team_diagnostics")agentExposed=true;CHECK(!agentExposed);
    ark::AgentSession session;const auto authoring=ark::documentToJson(session.document);const auto revision=session.revision;
    ark::AgentTeam team;ark::AgentTeamConfig config;ark::AgentTeamRole role;role.id="inspector";role.label="Synthetic inspector";role.assistant.model="offline-diagnostics";int round=0;
    std::promise<void> entered,release;auto released=release.get_future().share();
    role.transport=[&](auto&,auto&,auto*){switch(++round){case 1:return diagnosticCall("missing","reference_inspect",R"({"id":7,"find":{"kind":"planar","normal":"-Z"}})");case 2:return diagnosticCall("corrected","reference_inspect",R"({"id":7,"reference":"cap.end","detail":"frame"})");case 3:return diagnosticCall("active","doc_info",R"({"detail":"revision"})");default:return parse(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Synthetic inspection finished."}]}]})");}};config.roles={role};
    team.start(config,"Read synthetic diagnostic receipts only",ark::AgentSession::tools(),[&](const auto& name,const V& args){
        if(name=="memory_context")return parse(R"({"isError":false,"structuredContent":{"unavailable":true,"next_action":"Continue with the current synthetic source."}})");
        if(name=="reference_inspect")return args.find("find")?parse(R"({"isError":true,"structuredContent":{"error_code":"EVALUATION_FAILED","error":"No reference matched","revision":0,"document_unchanged":true,"recovery":{"action":"inspect_topology","tool":"reference_inspect","args":{"id":7}}}})"):parse(R"({"isError":false,"structuredContent":{"revision":0,"reference":"cap.end","kind":"face"}})");
        if(name!="doc_info")throw std::runtime_error("Unexpected synthetic diagnostic tool");
        entered.set_value();if(released.wait_for(std::chrono::seconds(5))!=std::future_status::ready)throw std::runtime_error("Synthetic diagnostic gate timed out");return session.call(name,args);
    });
    CHECK(entered.get_future().wait_for(std::chrono::seconds(3))==std::future_status::ready);
    const auto snapshot=team.snapshot(false,UINT64_MAX,true);CHECK(snapshot.roles.size()==1);const auto& observed=snapshot.roles[0];CHECK(observed.currentPhase=="tool"&&observed.activeTool=="doc_info"&&observed.activeToolStartedMs<=snapshot.elapsedMs);CHECK(observed.rejectedReads.size()==1&&observed.laterReadEvidence.size()==1);
    const auto output=ark::studioTeamDiagnostics(snapshot,V::makeObj());const auto& data=*output.find("structuredContent");const auto& exported=data.find("roles")->arr[0];CHECK(!output.find("isError")->b&&data.find("document_unchanged")->b);CHECK(exported.find("rejected_reads")->arr[0].find("args")->find("find")->find("normal")->str=="-Z");CHECK(exported.find("rejected_reads")->arr[0].find("recovery")->find("args")->find("id")->num==7);CHECK(exported.find("later_read_evidence")->arr[0].find("args")->find("reference")->str=="cap.end");CHECK(exported.find("later_read_evidence")->arr[0].find("read_sequence")->num>exported.find("rejected_reads")->arr[0].find("read_sequence")->num);CHECK(exported.find("later_read_evidence")->arr[0].find("same_subject_rejected_read_sequences")->arr[0].num==exported.find("rejected_reads")->arr[0].find("read_sequence")->num);
#ifdef _WIN32
    ark::LiveSessionBridge diagnosticBridge;CHECK(diagnosticBridge.start([&](const auto& tool,const V& args,auto done,std::stop_token){if(ark::isStudioDiagnosticsTool(tool))done(ark::studioTeamDiagnostics(team.snapshot(false,UINT64_MAX,true),args));else done(parse(R"({"isError":true})"));}));
    const auto live=ark::callLiveSession(diagnosticBridge.endpoint(),"studio_team_diagnostics",parse(R"({"role_id":"inspector","max_records":1})"));CHECK(!live.find("isError")->b&&live.find("structuredContent")->find("roles")->arr[0].find("active_tool")->str=="doc_info");diagnosticBridge.stop();
#endif
    release.set_value();CHECK(team.waitIdleFor(std::chrono::seconds(3)));CHECK(session.revision==revision&&ark::documentToJson(session.document)==authoring);
    for(const auto* invalid:{R"({"max_records":0})",R"({"max_records":9})",R"({"role_id":"missing"})",R"({"commands":[{"op":"param.set","name":"forbidden","expr":1}]})"})CHECK(ark::studioTeamDiagnostics(snapshot,parse(invalid)).find("isError")->b);
    auto hostile=snapshot;auto& projected=hostile.roles[0];projected.conversation=parse(R"([{"role":"user","content":"PRIVATE_CONVERSATION_SENTINEL"}])");V sensitive=projected.rejectedReads[0],secretArgs=*sensitive.find("args");secretArgs.set("apiKey",V::makeStr("CREDENTIAL_SENTINEL"));sensitive.set("args",secretArgs);projected.rejectedReads.push_back(sensitive);sensitive=*projected.rejectedReads.begin();V imageArgs=*sensitive.find("args");imageArgs.set("image_url",V::makeStr("IMAGE_SENTINEL"));sensitive.set("args",imageArgs);projected.rejectedReads.push_back(sensitive);projected.rejectedReadCount=3;
    V mutation=projected.rejectedReads[0];mutation.set("tool",V::makeStr("doc_apply"));mutation.set("args",parse(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"WRITE_SENTINEL","expr":1}]})"));projected.rejectedReads.push_back(mutation);V oversized=projected.rejectedReads[0],largeRecovery=V::makeObj();largeRecovery.set("hint",V::makeStr(std::string(9000,'x')));oversized.set("recovery",largeRecovery);projected.rejectedReads.push_back(oversized);projected.rejectedReadCount=5;
    for(int i=0;i<30;++i)projected.laterReadEvidence.push_back(projected.laterReadEvidence[0]);projected.laterReadEvidenceCount=(unsigned)projected.laterReadEvidence.size();const auto bounded=ark::studioTeamDiagnostics(hostile,V::makeObj());const auto bytes=bounded.dump();CHECK(bytes.size()<=256*1024&&bytes.find("PRIVATE_CONVERSATION_SENTINEL")==std::string::npos&&bytes.find("CREDENTIAL_SENTINEL")==std::string::npos&&bytes.find("IMAGE_SENTINEL")==std::string::npos&&bytes.find("WRITE_SENTINEL")==std::string::npos);const auto& boundedRole=bounded.find("structuredContent")->find("roles")->arr[0];CHECK(boundedRole.find("partial")->b&&boundedRole.find("omitted_sensitive_records")->num==2&&boundedRole.find("omitted_rejected_reads")->num==4&&boundedRole.find("later_read_evidence")->arr.size()==8);
}
int main(){
    diagnosticContracts();
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
    // A CAD evaluation may outlast the 30-second transfer window: its result is
    // delivered instead of an unknown-outcome timeout.
    CHECK(bridge.start([](auto&,auto&,auto done,std::stop_token){std::thread([done]{std::this_thread::sleep_for(std::chrono::seconds(31));V slow=V::makeObj();slow.set("completed_after_ms",V::makeNum(31000));done(slow);}).detach();}));
    const auto slowStarted=std::chrono::steady_clock::now();auto slow=ark::callLiveSession(bridge.endpoint(),"doc_preview",V::makeObj());
    CHECK(slow.find("completed_after_ms")&&slow.find("completed_after_ms")->num==31000);CHECK(std::chrono::steady_clock::now()-slowStarted>=std::chrono::seconds(31));
    CHECK(throws([&]{ark::callLiveSession(bridge.endpoint(),"doc_info",V::makeObj(),660001);}));bridge.stop();
    // 042D: a client that asks for progress receives notes before the single
    // result; a client that does not ask receives only the result, as before.
    CHECK(bridge.start([](auto&,auto&,auto done,std::stop_token){std::thread([done]{std::this_thread::sleep_for(std::chrono::milliseconds(4500));V finished=V::makeObj();finished.set("finished",V::makeBool(true));done(finished);}).detach();},{},[]{return std::string("Feature 3/5 #12 evaluating");}));
    std::vector<V> notes;const auto noted=ark::callLiveSession(bridge.endpoint(),"doc_preview",V::makeObj(),660000,[&](const V& note){notes.push_back(note);});
    CHECK(noted.find("finished")&&notes.size()>=2);
    CHECK(notes.size()>=2&&notes[0].find("message")&&notes[0].find("message")->str=="Feature 3/5 #12 evaluating"&&notes[1].find("elapsed_ms")->num>notes[0].find("elapsed_ms")->num);
    CHECK(ark::callLiveSession(bridge.endpoint(),"doc_preview",V::makeObj()).find("finished"));bridge.stop();
    // 042D: a second client connects and reaches the dispatcher while a long call
    // is still running, instead of waiting out the 30-second connect window.
    {std::atomic<int> dispatched=0;std::promise<void> releasePromise;auto release=releasePromise.get_future().share();
        CHECK(bridge.start([&](auto&,auto&,auto done,std::stop_token){++dispatched;std::thread([done,release]{release.wait();done(V::makeObj());}).detach();}));
        const auto concurrentEndpoint=bridge.endpoint();
        auto first=std::async(std::launch::async,[&]{return ark::callLiveSession(concurrentEndpoint,"doc_preview",V::makeObj());});
        auto second=std::async(std::launch::async,[&]{return ark::callLiveSession(concurrentEndpoint,"doc_info",V::makeObj());});
        const auto waited=std::chrono::steady_clock::now();while(dispatched<2&&std::chrono::steady_clock::now()-waited<std::chrono::seconds(5))std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(dispatched==2);releasePromise.set_value();CHECK(!first.get().find("isError")&&!second.get().find("isError"));bridge.stop();}
#else
    ark::LiveSessionBridge bridge;CHECK(!bridge.start([](auto&,auto&,auto,std::stop_token){}));
    CHECK(throws([&]{ark::callLiveSession("unavailable","doc_info",V::makeObj());}));
#endif
    std::cout<<checks<<" live bridge checks, "<<failures<<" failures\n";return failures?1:0;
}
