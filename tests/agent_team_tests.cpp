#include "ark/agent_team.hpp"
#include "ark/agent.hpp"
#include "ark/onboarding.hpp"
#include "ark/chatgpt_auth.hpp"
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
static void incrementalStepsAndScopes(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("designer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::PartDesigner;int rounds=0;std::atomic_bool attributed=true;ark::AgentSession session;
    config.roles[0].transport=[&](auto&,auto&,auto*){switch(++rounds){
        case 1:return call("skeleton","team_apply_step",R"({"expected_revision":0,"commands":[{"op":"sketch.set","name":"Live","sketch":{"plane":"XY","entities":[{"kind":"line","name":"a","dof":[0,0,10,0]}],"constraints":[]}}]})");
        case 2:return call("entity","team_apply_step",R"({"expected_revision":1,"commands":[{"op":"sketch.entity.set","name":"Live","entity":{"kind":"line","name":"b","dof":[0,0,0,10]}}]})");
        case 3:return call("part","team_apply_step",R"({"expected_revision":2,"commands":[{"op":"feature.add","type":"box","name":"Live part","p":{"dx":10,"dy":12,"dz":4}}]})");
        case 4:return call("wrong_owner","team_apply_step",R"({"expected_revision":3,"commands":[{"op":"assembly.add","name":"Not designer scope","feature":"Live part"}]})");
        case 5:return call("whole_assembly","team_apply_step",R"({"expected_revision":3,"commands":[{"op":"feature.add","type":"box","name":"A"},{"op":"feature.add","type":"box","name":"B"}]})");
        default:return done("Three actual incremental commits");}};
    team.start(config,"Construct valid sketch steps and one part",ark::AgentSession::tools(),[&](const auto& name,const V& args){if(ark::AgentTeam::currentRoleId()!="designer"||name!="doc_apply")attributed=false;return session.call(name,args);});complete(team);auto snapshot=team.snapshot();
    check(attributed&&ark::AgentTeam::currentRoleId().empty(),"host callbacks expose actual worker role only on that thread");check(session.revision==3&&session.document.features.size()==1&&session.document.assembly.empty()&&snapshot.roles[0].stepsApplied==3,"real sketch/entity/part commits counted while role and feature bounds reject writes");
    int queued=0,started=0,ended=0,drafts=0;for(const auto& event:snapshot.events){if(event.kind==ark::AgentTeamEvent::ConstructionQueued){++queued;if(event.draft){++drafts;check(!event.draftSketchCommands.arr.empty()&&event.draftSketchCommands.dump().size()<=config.maxDraftSketchBytes,"queued genuine requested sketch draft is bounded");}}if(event.kind==ark::AgentTeamEvent::ConstructionStart)++started;if(event.kind==ark::AgentTeamEvent::ConstructionEnd){++ended;check(!event.draft&&event.hasDocumentRevision&&event.documentRevision==event.expectedRevision+1&&!event.stepId.empty(),"construction end uses measured commit revision and correlation id");}}
    check(queued==3&&started==3&&ended==3&&drafts==2,"construction event sequence describes actual independently committed operations");
}
static void proposalsAndPhysicsRole(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("designer",true),role("reviewer")};config.roles[0].discipline=ark::AgentTeamDiscipline::PartDesigner;config.roles[1].discipline=ark::AgentTeamDiscipline::PhysicsReviewer;Gate ready;int designRounds=0,reviewRounds=0;std::atomic_bool sawMeasuredMath=false;ark::AgentSession session;
    config.roles[0].transport=[&](auto&,auto&,auto*){if(++designRounds==1){ready.enter();return call("apply_proposal","team_apply_step",R"({"expected_revision":0,"proposal_id":"proposal-1"})");}return done("Applied inspected correction");};
    config.roles[1].transport=[&](const auto&,const V& request,const auto*){switch(++reviewRounds){case 1:return call("inspect_revision","doc_info",R"({"detail":"design"})");case 2:return call("calculate","eval_expr",R"({"expr":"10*3"})");case 3:sawMeasuredMath=request.find("input")->dump().find("30")!=std::string::npos;return call("proposal","team_propose",R"({"to":"designer","expected_revision":0,"commands":[{"op":"param.set","name":"thickness","expr":4}],"reason":"Use declared thickness 4 for further mechanical study; actual arithmetic 10*3 returned 30, load assumptions remain unknown","evidence":"doc_info revision0 and eval_expr actual receipt; no FEA or qualification"})");default:return done("Calculation assumptions remain explicit");}};
    team.start(config,"Review and propose an actual revision",ark::AgentSession::tools(),[&](auto& name,auto& args){return session.call(name,args);},[&](const auto& event){if(event.kind==ark::AgentTeamEvent::Proposal)ready.open();});complete(team);auto snapshot=team.snapshot();
    check(sawMeasuredMath&&session.revision==1&&session.document.getParam("thickness")==4,"physics reviewer consumes real calculation receipt and proposes without writing");check(snapshot.proposals.size()==1&&snapshot.proposals[0].fromId=="reviewer"&&snapshot.proposals[0].state=="applied"&&snapshot.proposals[0].expectedRevision==0,"typed proposal retains sender/baseline and only actual receipt marks applied");
}
static void concurrentQueuedDrafts(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("first",true),role("second",true)};Gate native,queued;ark::AgentSession session;std::atomic_int executors=0;
    for(auto& item:config.roles){auto id=item.id;item.transport=[id](auto&,auto&,auto*){return call("draft_"+id,"doc_apply","{\"expected_revision\":0,\"commands\":[{\"op\":\"sketch.set\",\"name\":\""+id+"\",\"sketch\":{\"entities\":[{\"kind\":\"line\",\"name\":\"a\",\"dof\":[0,0,10,0]}],\"constraints\":[]}}]}");};}
    team.start(config,"Two concurrent genuine sketch intentions",ark::AgentSession::tools(),[&](auto& name,auto& args){++executors;native.enter();return session.call(name,args);},[&](const auto& event){if(event.kind==ark::AgentTeamEvent::ConstructionQueued){std::lock_guard lock(queued.mutex);++queued.arrivals;queued.condition.notify_all();}});
    native.await(1);queued.await(2);auto busy=team.snapshot();int drafts=0,starts=0;for(const auto& event:busy.events){if(event.kind==ark::AgentTeamEvent::ConstructionQueued&&event.draft)++drafts;if(event.kind==ark::AgentTeamEvent::ConstructionStart)++starts;}check(drafts==2&&starts==1&&executors==1,"both real draft intentions visible while one serialized executor is busy");team.cancel();native.open();complete(team);
    auto snapshot=team.snapshot();std::vector<std::string> pending,ends;for(const auto& event:snapshot.events){if(event.kind==ark::AgentTeamEvent::ConstructionQueued)pending.push_back(event.roleId+":"+event.stepId);if(event.kind==ark::AgentTeamEvent::ConstructionEnd){ends.push_back(event.roleId+":"+event.stepId);check(!event.draft&&event.draftSketchCommands.arr.empty(),"matching outcome clears only its own draft");}}std::sort(pending.begin(),pending.end());std::sort(ends.begin(),ends.end());check(pending==ends&&pending.size()==2&&executors==1,"cancelled queued role receives correlated end without executing its operation");
}
static void yieldedAndInterruptedContinuation(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("designer",true)};config.maxTurnsPerRole=1;config.roles[0].assistant.maxRounds=3;int rounds=0,writes=0;ark::AgentSession session;
    config.roles[0].transport=[&](auto&,auto&,auto*){int n=++rounds;if(n==1)return call("initial","doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":10}]})");if(n==4)return call("unsafe_retry","doc_apply",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"width","expr":99}]})");if(n==6)return call("remaining","doc_apply",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"height","expr":20}]})");if(n==8)return done("Remaining task completed");return call("inspect_"+std::to_string(n),"doc_info",R"({"detail":"design"})");};
    auto executor=[&](auto& name,auto& args){if(name=="doc_apply")++writes;return session.call(name,args);};team.start(config,"Bounded continuing task",ark::AgentSession::tools(),executor);complete(team);check(team.snapshot().roles[0].state==ark::AgentTeamState::AwaitingContinuation&&session.revision==1,"round and turn budgets yield after retaining one commit");check(!team.steer("ordinary over-budget input"),"ordinary steering cannot bypass bounded turn window");check(team.resume("designer"),"explicit resume renews a bounded exhausted turn window");complete(team);check(writes==2&&session.document.getParam("width")==10&&session.document.getParam("height")==20&&session.revision==2,"resume gates attempted replay until doc_info and applies only remaining mutation");check(team.resume("designer"),"a second explicit budget window can continue retained history");complete(team);check(team.snapshot().roles[0].turns==3&&writes==2,"lifetime turns retained across renewed windows without duplicate mutations");
    config.maxTurnsPerRole=8;config.roles[0].assistant.maxRounds=12;rounds=writes=0;ark::AgentSession interrupted;
    config.roles[0].transport=[&](auto&,auto&,auto*){switch(++rounds){case 1:return call("before_stream","doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":10}]})");case 2:return ark::parseChatGptSse("data: {\"type\":\"response.output_item.done\",\"output_index\":0,\"item\":{\"type\":\"function_call\",\"call_id\":\"untrusted_partial\",\"name\":\"doc_apply\",\"arguments\":\"{}\"}}\n\n");case 3:return call("blocked_after_stream","doc_apply",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"width","expr":99}]})");case 4:return call("after_stream_inspect","doc_info",R"({"detail":"design"})");case 5:return call("after_stream_remaining","doc_apply",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"height","expr":20}]})");default:return done("Resumed safely");}};
    team.start(config,"Interrupted provider stream",ark::AgentSession::tools(),[&](auto& name,auto& args){if(name=="doc_apply")++writes;return interrupted.call(name,args);});complete(team);auto failure=team.snapshot(true);check(failure.roles[0].state==ark::AgentTeamState::Interrupted&&failure.usage.totalTokens==12&&failure.usage.missingResponses==1&&writes==1,"missing terminal SSE rejects partial call and marks that request usage unknown");check(failure.roles[0].conversation.dump().find("untrusted_partial")==std::string::npos,"unaccepted partial stream call never enters executable history");check(team.resume("designer"),"interrupted transport explicitly resumes retained context");complete(team);check(writes==2&&interrupted.document.getParam("width")==10&&interrupted.document.getParam("height")==20&&team.snapshot().usage.missingResponses==1,"stream recovery preserves prior commit and unknown usage without replaying writes");
}

static void frameDisciplineScope(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("designer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::PartDesigner;
    int round=0,writes=0;config.roles[0].transport=[&](auto&,auto&,auto*){
        if(++round==1)return call("trim_frame","team_apply_step",R"({"expected_revision":0,"commands":[{"op":"frame.trim_extend","id":"Frame","joint":{"member":"Brace","end":"end","against":"Rail","max_extension":25}}]})");
        if(round==2)return call("weld_frame","team_apply_step",R"({"expected_revision":1,"commands":[{"op":"frame.weld.add","id":"Frame","weld":{"id":"Seam"}}]})");
        return done();
    };
    team.start(config,"Author frame details",ark::AgentSession::tools(),[&](auto& name,auto&){check(name=="doc_apply","frame commands route to actual transaction executor");V response=V::makeObj();response.set("revision",V::makeNum(++writes));return response;});complete(team);
    check(writes==2&&team.snapshot().roles[0].stepsApplied==2,"part designer can author bounded frame trim and weld steps");
}
static void unlimitedEightRoleAssignments(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.orchestratedStart=true;Gate simultaneous;
    std::atomic_int providerCalls=0,docs=0;std::atomic_bool validPack=true;
    config.roles.push_back(role("engineer",true));config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;
    config.roles[0].assistant.onboardingPack=ark::agentOnboardingPack({"overview","assembly"});
    for(int i=1;i<8;++i){auto specialist=role("part"+std::to_string(i),i!=7);specialist.discipline=i==7?ark::AgentTeamDiscipline::PhysicsReviewer:ark::AgentTeamDiscipline::PartDesigner;config.roles.push_back(specialist);}
    int orchestratorRound=0;
    config.roles[0].transport=[&](auto&,const V& request,auto*){
        ++providerCalls;int markers=0;for(const auto& item:request.find("input")->arr)if(auto text=item.find("content");text&&text->t==V::Str&&text->str.find("ARK_PREBAKED_GUIDE_V1")!=std::string::npos)++markers;if(markers!=1)validPack=false;
        int n=++orchestratorRound;
        if(n<=40)return call("inspection_"+std::to_string(n),"doc_info","{}");
        if(n<=47)return call("assignment_"+std::to_string(n),"team_send","{\"to\":\"part"+std::to_string(n-40)+"\",\"message\":\"Work on your distinct assigned component.\"}");
        simultaneous.enter();return done("Assignments coordinated");
    };
    for(int i=1;i<8;++i)config.roles[i].transport=[&](auto&,auto&,auto*){++providerCalls;simultaneous.enter();return done("Assigned work inspected");};
    team.start(config,"Build separately assigned parts",ark::AgentSession::tools(),[&](auto&,auto&){++docs;return V::makeObj();});
    simultaneous.await(8);auto working=team.snapshot();check(working.roles.size()==8&&working.running,"eight configured roles run simultaneously after explicit assignments");
    for(const auto& r:working.roles)check(r.state==ark::AgentTeamState::Working,"all assigned workers active concurrently");
    simultaneous.open();complete(team);check(docs==40&&providerCalls>=56&&validPack,"unlimited run exceeds the old round limit and retains one baked guide");
    check(team.snapshot().roles[0].state==ark::AgentTeamState::Idle,"unlimited completion does not request Continue");
    auto completedTurns=team.snapshot().roles[0].turns;check(completedTurns>=2,"specialist completion automatically wakes the assembly orchestrator");
    for(int i=0;i<12;++i){check(team.steer("Inspect the next assembly checkpoint","engineer"),"unlimited role accepts subsequent assignments");complete(team);}check(team.snapshot().roles[0].turns==completedTurns+12&&validPack,"turns continue past the old eight-turn window without duplicating onboarding");
    auto baked=json(config.roles[0].assistant.onboardingPack);for(const auto& guide:baked.find("topics")->arr)check(!guide.find("document"),"prebaked documentation contains no stale document revision");
}
int main(){try{unlimitedEightRoleAssignments();configChecks();concurrencyAndRevision();steeringAndPause();collaboration();cancellationAndFailures();interruptedBatchHistory();limitsAndMutationSchemas();conversationMemoryBound();incrementalStepsAndScopes();frameDisciplineScope();proposalsAndPhysicsRole();concurrentQueuedDrafts();yieldedAndInterruptedContinuation();std::cout<<"Agent team checks passed: "<<checks<<"\n";return 0;}catch(const std::exception& error){std::cerr<<"Agent team checks failed: "<<error.what()<<"\n";return 1;}}
