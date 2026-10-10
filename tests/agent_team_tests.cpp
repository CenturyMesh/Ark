#include "ark/agent_team.hpp"
#include "ark/agent.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
using V=arkjson::Value;
using namespace std::chrono_literals;
static int checks=0;
static void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
static V json(const std::string& text){V v;if(!arkjson::parse(text,v))throw std::runtime_error("test JSON");return v;}
template<class F>static void fails(F action,const char* why){bool caught=false;try{action();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
static V usageResponse(V response,bool usage=true){if(usage)response.set("usage",json(R"({"input_tokens":10,"output_tokens":2,"total_tokens":12,"input_tokens_details":{"cached_tokens":3},"output_tokens_details":{"reasoning_tokens":1}})"));return response;}
static V done(const std::string& text="Done",bool usage=true){V response=json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text"}]}]})");auto output=*response.find("output");auto content=*output.arr[0].find("content");content.arr[0].set("text",V::makeStr(text));output.arr[0].set("content",content);response.set("output",output);return usageResponse(response,usage);}
static V call(const std::string& id,const std::string& name,const std::string& args){V response=json(R"({"status":"completed","output":[]})"),item=V::makeObj();item.set("type",V::makeStr("function_call"));item.set("call_id",V::makeStr(id));item.set("name",V::makeStr(name));item.set("arguments",V::makeStr(args));auto output=*response.find("output");output.arr.push_back(item);response.set("output",output);return usageResponse(response);}
static ark::AgentTeamRole role(const std::string& id,bool writable=false){ark::AgentTeamRole role;role.id=id;role.label=id;role.instructions="Review measured design evidence.";role.assistant.model="offline-"+id;role.assistant.reasoningEffort="low";role.canWrite=writable;return role;}
static void complete(ark::AgentTeam& team){check(team.waitIdleFor(5s),"bounded team completion");}
struct Gate {
    std::mutex mutex;std::condition_variable condition;int arrivals=0;bool release=false;
    void enter(){std::unique_lock lock(mutex);++arrivals;condition.notify_all();if(!condition.wait_for(lock,3s,[&]{return release;}))throw std::runtime_error("test gate timeout");}
    void await(int n){std::unique_lock lock(mutex);if(!condition.wait_for(lock,3s,[&]{return arrivals>=n;}))throw std::runtime_error("test arrival timeout");}
    void open(){std::lock_guard lock(mutex);release=true;condition.notify_all();}
};
static void configChecks(){
    ark::AgentTeam team;ark::AgentTeamConfig config;V tools=V::makeArr();
    fails([&]{team.start(config,"task",tools,{});},"empty team rejected");
    config.roles={role("engineer")};config.roles[0].assistant.model.clear();fails([&]{team.start(config,"task",tools,{});},"no implicit paid model");
    config.roles={role("engineer"),role("engineer")};fails([&]{team.start(config,"task",tools,{});},"duplicate roles rejected");
    config.roles={role("engineer")};config.maxWorkers=0;fails([&]{team.start(config,"task",tools,{});},"worker bound enforced");
}
static void concurrencyAndRevision(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.maxWorkers=2;config.roles={role("engineer",true),role("designer",true),role("reviewer")};
    Gate first;std::atomic_int live=0,peak=0,toolLive=0,toolPeak=0,dispatches=0,success=0,conflicts=0;std::atomic_bool preserved=true;
    for(auto& item:config.roles){auto id=item.id;auto rounds=std::make_shared<int>(0);item.transport=[&,id,rounds](const auto& cfg,const V& request,const auto*){
        if(cfg.model!="offline-"+id||cfg.reasoningEffort!="low"||request.find("input")->arr.front().find("content")->str.find("role "+id)==std::string::npos)preserved=false;
        auto now=++live;int old=peak;while(now>old&&!peak.compare_exchange_weak(old,now)){}
        if(++*rounds==1){if(id!="reviewer")first.enter();--live;return call("write_"+id,"doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":20}]})");}
        std::this_thread::sleep_for(5ms);--live;return done(id+" completed");
    };}
    ark::AgentSession session;
    team.start(config,"Collaborate on the same drawing",ark::AgentSession::tools(),[&](const auto& name,const V& args){
        auto now=++toolLive;int old=toolPeak;while(now>old&&!toolPeak.compare_exchange_weak(old,now)){};++dispatches;std::this_thread::sleep_for(8ms);V result=session.call(name,args);--toolLive;if(result.find("isError")->b)++conflicts;else ++success;return result;
    });
    first.await(2);check(team.snapshot().running,"actual transport workers concurrently active");first.open();complete(team);auto snapshot=team.snapshot(true);
    check(peak==2,"provider concurrency reaches configured bound");check(toolPeak==1,"shared session callbacks serialized");check(dispatches==2&&success==1&&conflicts==1&&session.revision==1,"same-revision race commits once and rejects stale write");
    check(preserved,"independent role model/effort/instructions preserved");check(snapshot.roles.size()==3&&snapshot.roles[0].conversation.arr.front().find("content")->str!=snapshot.roles[1].conversation.arr.front().find("content")->str,"separate conversations");
    check(snapshot.usage.totalTokens==60&&snapshot.usage.inputTokens==50&&snapshot.usage.outputTokens==10&&snapshot.usage.cachedInputTokens==15&&snapshot.usage.reasoningTokens==5&&snapshot.usage.reportedResponses==5,"provider cumulative team accounting without adding subsets");
    int errors=0;std::uint64_t sequence=0;for(const auto& event:snapshot.events){check(event.sequence>sequence,"real events have monotonic sequence");sequence=event.sequence;if(event.kind==ark::AgentTeamEvent::ToolEnd&&event.toolError)++errors;}
    check(errors==1&&snapshot.roles[2].state==ark::AgentTeamState::Failed,"stale write is a tool error and unadvertised reviewer write rejects response");check(team.snapshot().roles[0].conversation.arr.empty(),"UI snapshots omit full histories by default");check(team.snapshot(false,sequence).events.empty(),"incremental snapshot excludes old events");
}
static void steeringAndPause(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("engineer",true)};Gate gate;int rounds=0;std::atomic_int writes=0;bool sawSteering=false;
    config.roles[0].transport=[&](const auto&,const V& request,const auto*){if(++rounds==1){gate.enter();return call("obsolete","doc_apply",R"({"expected_revision":0,"commands":[]})");}sawSteering=request.find("input")->dump().find("Use width 35")!=std::string::npos;return done("Updated proposal");};
    team.start(config,"Use width 20",ark::AgentSession::tools(),[&](auto&,auto&){++writes;return V::makeObj();});gate.await(1);check(team.steer("Use width 35","engineer"),"steering delivered to active role");gate.open();complete(team);check(writes==0&&sawSteering,"in-flight stale write refused and steering injected next request");
    auto history=team.snapshot(true).roles[0].conversation.dump();check(history.find("Use width 35")!=std::string::npos,"steering retained in real history");
    Gate pauseGate;int pauseRounds=0;std::atomic_int tools=0;
    config.roles[0].transport=[&](auto&,auto&,auto*){if(++pauseRounds==1){pauseGate.enter();return call("read","doc_info","{}");}return done();};
    team.start(config,"Inspect",ark::AgentSession::tools(),[&](auto&,auto&){++tools;return V::makeObj();});pauseGate.await(1);team.pause();pauseGate.open();check(!team.waitIdleFor(80ms)&&team.snapshot().paused&&tools==0,"pause gates next tool without inventing completion");team.pause(false);complete(team);check(tools==1,"resume executes queued tool exactly once");
}
static void collaboration(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("engineer",true),role("reviewer")};Gate engineer;std::atomic_int reviewerRounds=0,engineerRounds=0;std::atomic_bool sawPeer=false,sawContext=false;
    config.roles[0].transport=[&](const auto&,const V& request,const auto*){if(++engineerRounds==1){engineer.enter();return call("inspect","doc_info","{}");}sawPeer=request.find("input")->dump().find("Check mounting clearance at revision 0")!=std::string::npos;sawContext=request.find("input")->dump().find("Teammate update from reviewer")!=std::string::npos;return done("Engineering review completed");};
    config.roles[1].transport=[&](auto&,auto&,auto*){if(++reviewerRounds==1){engineer.await(1);return call("message","team_send",R"({"to":"engineer","message":"Check mounting clearance at revision 0"})");}return done("Clearance is a review requirement");};
    team.start(config,"Coordinate design review",ark::AgentSession::tools(),[](auto&,auto&){return json(R"({"revision":0})");},[&](const auto& event){if(event.kind==ark::AgentTeamEvent::Text&&event.roleId=="reviewer")engineer.open();});complete(team);
    check(sawPeer,"team_send reaches a teammate's next provider request");
    check(sawContext,"automatic real teammate text enters next provider request");
    auto snapshot=team.snapshot();bool peerEvent=false;for(auto& event:snapshot.events)if(event.kind==ark::AgentTeamEvent::Message&&event.text.find("Teammate reviewer -> engineer")!=std::string::npos)peerEvent=true;check(peerEvent,"team messaging produces real delivered-message event");
    check(team.steer("Review bolt material","reviewer"),"idle role schedules a new user turn");complete(team);check(team.snapshot().roles[1].turns==2,"independent conversation continues on user steering");
    check(!team.send("unknown","engineer","bad")&&!team.send("reviewer","missing","bad"),"unknown sender/recipient rejected");
}
static void interruptedBatchHistory(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("engineer",true)};ark::AgentSession session;int executed=0;
    config.roles[0].transport=[](auto&,auto&,auto*){V response=call("first","doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":10}]})");V second=call("second","doc_apply",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"height","expr":30}]})");auto output=*response.find("output");output.arr.push_back(second.find("output")->arr[0]);response.set("output",output);return response;};
    team.start(config,"Apply two batches",ark::AgentSession::tools(),[&](auto& name,auto& args){++executed;auto result=session.call(name,args);team.cancel();return result;});complete(team);
    auto history=team.snapshot(true).roles[0].conversation;int calls=0,outputs=0;for(const auto& item:history.arr){auto type=item.find("type");if(type&&type->str=="function_call")++calls;if(type&&type->str=="function_call_output")++outputs;}
    check(executed==1&&session.revision==1&&calls==2&&outputs==2,"mid-batch cancellation preserves protocol-complete history and suppresses second mutation");
    check(history.dump().find("Execution may have completed")!=std::string::npos,"interrupted output records unknown execution instead of invented failure");
    auto continued=ark::runAssistant(config.roles[0].assistant,history,ark::AgentSession::tools(),{}, {},nullptr,[](auto&,const V& request,auto*){
        const auto& input=request.find("input")->arr;for(const auto& item:input)if(item.find("type")&&item.find("type")->str=="function_call"){auto id=item.find("call_id")->str;bool answered=false;for(const auto& result:input)if(result.find("type")&&result.find("type")->str=="function_call_output"&&result.find("call_id")->str==id)answered=true;if(!answered)throw std::runtime_error("unanswered continuation call");}return done("Inspect before continuing");});
    check(continued.text=="Inspect before continuing","retained interrupted history can safely continue through real request builder");
}
static void cancellationAndFailures(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("a"),role("b"),role("queued")};config.maxWorkers=2;std::atomic_int requests=0;Gate entered;
    for(auto& item:config.roles)item.transport=[&](auto&,auto&,const std::atomic_bool* cancel){++requests;{std::lock_guard lock(entered.mutex);++entered.arrivals;entered.condition.notify_all();}while(!cancel->load())std::this_thread::sleep_for(1ms);return done("Returned after cancel");};
    team.start(config,"Cancellation test",V::makeArr(),{});entered.await(2);team.cancel();complete(team);auto cancelled=team.snapshot();
    check(requests==2&&cancelled.cancelled&&!cancelled.running,"cancellation stops active transports and suppresses queued work");check(cancelled.usage.totalTokens==24&&cancelled.usage.reportedResponses==2,"billed returned responses retained after cancellation");for(auto& item:cancelled.roles)check(item.state==ark::AgentTeamState::Cancelled,"cancelled role terminal status");check(!team.steer("late"),"steering rejected after cancellation");
    config.roles={role("failed",true),role("missing")};int failedRounds=0;config.roles[0].transport=[&](auto&,auto&,auto*){if(++failedRounds==1)return call("write","doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":10}]})");return usageResponse(json(R"({"status":"incomplete","output":[]})"));};config.roles[1].transport=[](auto&,auto&,auto*){return done("Usage not supplied",false);};ark::AgentSession session;
    team.start(config,"Failure test",ark::AgentSession::tools(),[&](auto& name,auto& args){return session.call(name,args);});complete(team);auto failure=team.snapshot(true);
    check(session.revision==1&&failure.roles[0].state==ark::AgentTeamState::Failed&&failure.roles[1].state==ark::AgentTeamState::Idle,"one role failure preserves prior mutation without failing peer");check(failure.usage.totalTokens==24&&failure.usage.reportedResponses==2&&failure.usage.missingResponses==1,"failure usage preserved and missing provider receipts stay unknown");
    check(failure.roles[0].conversation.dump().find("function_call_output")!=std::string::npos&&failure.roles[0].conversation.dump().find("preceding team turn failed")!=std::string::npos,"failure retains tool history plus explicit recovery instruction");
}
static void limitsAndMutationSchemas(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("engineer",true)};config.maxEvents=3;config.maxTurnsPerRole=1;int rounds=0,executed=0;bool guardedSchemas=false;
    config.roles[0].transport=[&](const auto&,const V& request,const auto*){if(++rounds==1){auto tools=request.find("tools");for(const auto& tool:tools->arr)if(tool.find("name")->str=="doc_undo"){auto params=tool.find("parameters");guardedSchemas=params->find("properties")->find("expected_revision")&&params->find("required")->dump().find("expected_revision")!=std::string::npos;}return call("unguarded","doc_undo","{}");}return done(std::string(5000,'x'));};
    team.start(config,"Guard imports and undo",ark::AgentSession::tools(),[&](auto&,auto&){++executed;return V::makeObj();},[](auto&){throw std::runtime_error("observer cannot break worker");});complete(team);auto snapshot=team.snapshot();
    check(guardedSchemas&&executed==0,"team advertises and enforces revision for undo/load/import tools");check(snapshot.events.size()==3&&snapshot.droppedEvents>0,"event retention is bounded with explicit drop count");for(auto& event:snapshot.events)check(event.text.size()<=config.maxEventTextBytes,"event text byte cap enforced");check(!team.steer("turn over budget"),"configured role turn bound rejects unbounded reruns");
    ark::AgentSession session;auto edit=json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":20}]})");check(!session.call("doc_apply",edit).find("isError")->b,"prepare real stale undo state");auto result=session.call("doc_undo",json(R"({"expected_revision":0})"));check(result.find("isError")->b&&session.revision==1,"stale undo cannot alter shared session");auto load=session.call("doc_load",json(R"({"expected_revision":0,"path":"does-not-exist.ark"})"));check(load.find("isError")->b&&session.revision==1,"stale load rejected before filesystem import");
}
static void conversationMemoryBound(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("reviewer")};config.roles[0].assistant.maxConversationBytes=8192;int rounds=0;bool compactReceipt=false;V tools=json(R"([{"name":"doc_info","inputSchema":{"type":"object"},"annotations":{"readOnlyHint":true}}])");
    config.roles[0].transport=[&](const auto&,const V& request,const auto*){if(++rounds==1)return call("large","doc_info","{}");compactReceipt=request.find("input")->dump().size()<=8192&&request.find("input")->dump().find("Tool receipt omitted")!=std::string::npos;return done("Compact inspection required");};
    team.start(config,"Inspect compact evidence",tools,[](auto&,auto&){V result=V::makeObj();result.set("large_payload",V::makeStr(std::string(20000,'z')));return result;});complete(team);check(compactReceipt&&team.snapshot(true).roles[0].conversation.dump().size()<=8192,"oversized real tool result uses truthful compact receipt and bounded history");
    config.roles[0].transport=[](auto&,auto&,auto*){return done(std::string(12000,'z'));};team.start(config,"Keep conversation bounded",tools,{});complete(team);auto snapshot=team.snapshot(true);check(snapshot.roles[0].state==ark::AgentTeamState::Failed&&snapshot.usage.totalTokens==12&&snapshot.roles[0].conversation.dump().size()<=8192,"oversized completed provider output fails before history retention while preserving billed usage");
}
int main(){try{configChecks();concurrencyAndRevision();steeringAndPause();collaboration();cancellationAndFailures();interruptedBatchHistory();limitsAndMutationSchemas();conversationMemoryBound();std::cout<<"Agent team checks passed: "<<checks<<"\n";return 0;}catch(const std::exception& error){std::cerr<<"Agent team checks failed: "<<error.what()<<"\n";return 1;}}
