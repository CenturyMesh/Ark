#include "ark/agent_team.hpp"
#include "ark/agent.hpp"
#include "ark/agent_activity.hpp"
#include "ark/onboarding.hpp"
#include "ark/chatgpt_auth.hpp"
#include "ark/io.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
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
#include "agent_team_image_tests.inl"
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
    check(team.snapshot().roles[0].performance.providerRequests==1&&team.snapshot().roles[0].performance.toolCalls==1,"cancellation after a completed executor retains its measured provider and tool activity");
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
static void sharedAuthoringSchemas(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("engineer",true)};config.maxCommandsPerStep=5;V commands,conditions;unsigned owners=0;
    config.roles[0].transport=[&](const auto&,const V& request,const auto*){
        for(const auto& tool:request.find("tools")->arr)if(tool.find("name")->str=="doc_apply"){const auto* props=tool.find("parameters")->find("properties");commands=*props->find("commands")->find("items");conditions=*props->find("checks");}
        check(commands.find("properties")->find("expr")&&conditions.find("items")->find("properties")->find("query"),"document authoring contract is typed");
        for(const auto& tool:request.find("tools")->arr){const auto name=tool.find("name")->str;if(name=="doc_apply"||name=="team_apply_step"||name=="team_propose"){const auto* props=tool.find("parameters")->find("properties");const auto* items=props->find("commands")->find("items");const auto* fields=items->find("properties");check(fields&&fields->find("op")&&items->find("required")->dump()==commands.find("required")->dump(),"team aliases retain required operation and refer advanced fields to canonical discovery");for(const auto& realField:commands.find("properties")->obj)check(fields->find(realField.first.c_str())!=nullptr,"team authoring aliases advertise every canonical command field, including native/sketch/part/plane/placement");for(const char* field:{"id","p","expr","type","name","inputs"})check(fields->find(field)->find("type")->dump()==commands.find("properties")->find(field)->find("type")->dump(),"team command target/parameter types match document writes");check(fields->find("p")->find("additionalProperties")->dump()==commands.find("properties")->find("p")->find("additionalProperties")->dump(),"typed extensible parameter map is preserved");const auto* checks=props->find("checks")->find("items");check(checks->find("required")->dump()==conditions.find("items")->find("required")->dump()&&checks->find("properties")->find("query")->find("required")->dump()==conditions.find("items")->find("properties")->find("query")->find("required")->dump(),"team check contracts require nested measurement path and operator");check(props->find("commands")->find("maxItems")->num==5,"team schema retains caller construction-step bound");++owners;}}
        for(const auto& advertised:request.find("tools")->arr)if(advertised.find("name")->str=="doc_preview"){const auto* fields=advertised.find("parameters")->find("properties")->find("commands")->find("items")->find("properties");for(const char* key:{"native","sketch","part","material","plane"})check(fields->find(key)!=nullptr,"preview exposes real advanced command fields instead of a six-field subset");}
        return done("Schema contract inspected; no authoring claimed.");
    };
    team.start(config,"Inspect advertised authoring contracts",ark::AgentSession::tools(),{});complete(team);check(owners==3&&team.snapshot().roles[0].state==ark::AgentTeamState::Idle,"all three team write contracts inspected without provider failure");
}
static void drawingTeamScope(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("designer",true),role("reviewer")};
    config.roles[0].discipline=ark::AgentTeamDiscipline::PartDesigner;config.roles[1].discipline=ark::AgentTeamDiscipline::PhysicsReviewer;
    bool designerWrites=false,reviewerRead=false,reviewerNoWrite=false;
    config.roles[0].transport=[&](const auto&,const V& request,const auto*){
        for(const auto& tool:request.find("tools")->arr)if(tool.find("name")->str=="team_apply_step"){
            const auto* choices=tool.find("parameters")->find("properties")->find("commands")->find("items")->find("properties")->find("op")->find("enum");
            bool set=false,refresh=false,remove=false;for(const auto& operation:choices->arr){set|=operation.str=="drawing.set";refresh|=operation.str=="drawing.refresh";remove|=operation.str=="drawing.remove";}designerWrites=set&&refresh&&remove;
        }return done("Drawing authoring scope inspected.");
    };
    config.roles[1].transport=[&](const auto&,const V& request,const auto*){
        reviewerNoWrite=true;for(const auto& tool:request.find("tools")->arr){const auto& name=tool.find("name")->str;reviewerRead|=name=="drawing_inspect";if(name=="team_apply_step"||name=="doc_apply")reviewerNoWrite=false;}
        return done("Read-only drawing inspection scope inspected.");
    };
    team.start(config,"Inspect drawing authoring and review scopes",ark::AgentSession::tools(),{});complete(team);
    check(designerWrites&&reviewerRead&&reviewerNoWrite,"designer can author revision-bound drawings; physics reviewer can inspect but cannot write");
}
static void conversationMemoryBound(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("reviewer")};int rounds=0;bool compactReceipt=false;V tools=json(R"([{"name":"doc_info","inputSchema":{"type":"object"},"annotations":{"readOnlyHint":true}}])");
    // The outgoing request cap includes instructions/schemas; exact host history is separate.
    // Measure this role's actual envelope, then reserve 1 KiB for the small call/receipt/final response.
    std::size_t envelopeBytes=0,schemaBytes=0,maxRequestBytes=0;
    config.roles[0].assistant.maxConversationBytes=64*1024;
    config.roles[0].transport=[&](auto&,const V& request,auto*){envelopeBytes=request.dump().size();schemaBytes=request.find("tools")->dump().size();return done("Envelope measured; no tool executed.");};
    team.start(config,"Inspect compact evidence",tools,{});complete(team);check(envelopeBytes>0&&schemaBytes>0,"actual bounded-memory fixture envelope measured from the provider request");
    const auto limit=envelopeBytes+1024;config.roles[0].assistant.maxConversationBytes=limit;
    config.roles[0].transport=[&](const auto&,const V& request,const auto*){maxRequestBytes=std::max(maxRequestBytes,request.dump().size());if(++rounds==1)return call("large","doc_info","{}");compactReceipt=request.find("input")->dump().size()<=limit&&request.find("input")->dump().find("ARK_REQUEST_HISTORY_PROJECTION_V1")!=std::string::npos;return done("Compact inspection required");};
    team.start(config,"Inspect compact evidence",tools,[&](auto&,auto&){V result=V::makeObj();result.set("large_payload",V::makeStr(std::string(limit*3,'z')));return result;});complete(team);auto compact=team.snapshot(true);
    check(compactReceipt&&rounds==2&&compact.roles[0].state==ark::AgentTeamState::Idle&&maxRequestBytes<=limit&&compact.roles[0].conversation.dump().size()>limit&&compact.roles[0].conversation.dump().find(std::string(limit*3,'z'))!=std::string::npos,"oversized real tool result retains its original evidence with a bounded outgoing projection");
    V report=V::makeObj();report.set("initial_request_bytes",V::makeNum((double)envelopeBytes));report.set("tool_schema_bytes",V::makeNum((double)schemaBytes));report.set("fixture_limit_bytes",V::makeNum((double)limit));report.set("largest_request_bytes",V::makeNum((double)maxRequestBytes));std::cout<<"AGENT_MEMORY_BOUND "<<report.dump()<<'\n';
    config.roles[0].transport=[&](auto&,auto&,auto*){return done(std::string(limit*2,'z'));};team.start(config,"Keep conversation bounded",tools,{});complete(team);auto snapshot=team.snapshot(true);check(snapshot.roles[0].state==ark::AgentTeamState::Idle&&snapshot.usage.totalTokens==12&&snapshot.roles[0].conversation.dump().size()>limit,"completed output retains full history and usage separately from outgoing request limit");
}
static void incrementalStepsAndScopes(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.maxFeaturesPerStep=config.maxPlacementsPerStep=1;config.roles={role("designer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::PartDesigner;int rounds=0;std::atomic_bool attributed=true;ark::AgentSession session;
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
    std::atomic_int providerCalls=0,docs=0;std::atomic_bool validPack=true,quietReads=true,leadReceivedCompletion=false;
    config.roles.push_back(role("engineer",true));config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;
    config.roles[0].assistant.onboardingPack=ark::agentOnboardingPack({"overview","assembly"});
    for(int i=1;i<8;++i){auto specialist=role("part"+std::to_string(i),i!=7);specialist.discipline=i==7?ark::AgentTeamDiscipline::PhysicsReviewer:ark::AgentTeamDiscipline::PartDesigner;config.roles.push_back(specialist);}
    int orchestratorRound=0;
    config.roles[0].transport=[&](auto&,const V& request,auto*){
        ++providerCalls;const auto input=request.find("input")->dump();if(input.find("Teammate part")!=std::string::npos&&input.find("Assignment turn ")!=std::string::npos)leadReceivedCompletion=true;int markers=0;for(const auto& item:request.find("input")->arr)if(auto text=item.find("content");text&&text->t==V::Str&&text->str.find("ARK_PREBAKED_GUIDE_V1")!=std::string::npos)++markers;if(markers!=1)validPack=false;
        int n=++orchestratorRound;
        if(n<=40)return call("inspection_"+std::to_string(n),"doc_info","{}");
        if(n<=47)return call("assignment_"+std::to_string(n),"team_send","{\"to\":\"part"+std::to_string(n-40)+"\",\"message\":\"Work on your distinct assigned component.\"}");
        simultaneous.enter();return done("Assignments coordinated");
    };
    for(int i=1;i<8;++i)config.roles[i].transport=[&](auto&,const V& request,auto*){++providerCalls;if(request.find("input")->dump().find("Teammate update from engineer: doc_info")!=std::string::npos)quietReads=false;simultaneous.enter();return done("Assigned work inspected");};
    team.start(config,"Build separately assigned parts",ark::AgentSession::tools(),[&](auto&,auto&){++docs;return V::makeObj();});
    simultaneous.await(8);auto working=team.snapshot();check(working.roles.size()==8&&working.running,"eight configured roles run simultaneously after explicit assignments");
    for(const auto& r:working.roles)check(r.state==ark::AgentTeamState::Working,"all assigned workers active concurrently");
    simultaneous.open();complete(team);check(docs==40&&providerCalls>=56&&validPack,"unlimited run exceeds the old round limit and retains one baked guide");check(quietReads,"forty engineer read receipts are not duplicated into seven specialist histories");
    check(team.snapshot().roles[0].state==ark::AgentTeamState::Idle,"unlimited completion does not request Continue");
    auto completedTurns=team.snapshot().roles[0].turns;check(leadReceivedCompletion,"assembly orchestrator receives actual specialist completion before its next provider request; Parallel may integrate within the same turn");
    for(int i=0;i<12;++i){check(team.steer("Inspect the next assembly checkpoint","engineer"),"unlimited role accepts subsequent assignments");complete(team);}check(team.snapshot().roles[0].turns==completedTurns+12&&validPack,"turns continue past the old eight-turn window without duplicating onboarding");
    auto baked=json(config.roles[0].assistant.onboardingPack);for(const auto& guide:baked.find("topics")->arr)check(!guide.find("document"),"prebaked documentation contains no stale document revision");
}
static void parallelAutomaticCompletionBoundaries(){
    for(bool leadWorking:{true,false}){
        ark::AgentTeam team;ark::AgentTeamConfig config;config.orchestratedStart=true;config.focusedWorkflow=false;config.maxWorkers=2;config.roles={role("engineer",true),role("part")};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;
        Gate leadBoundary,specialistBoundary,completionDelivered,leadReturned;std::atomic_bool received=false;int leadRequests=0;
        config.roles[0].transport=[&](auto&,const V& request,auto*){int n=++leadRequests;if(n==1)return call("assign_parallel_part","team_send",R"({"to":"part","message":"Inspect the assigned part and report measured evidence."})");if(request.find("input")->dump().find("Assignment turn ")!=std::string::npos)received=true;if(leadWorking&&n==2)leadBoundary.enter();return done("Lead inspected received completion evidence.");};
        config.roles[1].transport=[&](auto&,auto&,auto*){specialistBoundary.enter();return done("Assigned part evidence inspected.");};
        team.start(config,"Coordinate the requested part inspection",V::makeArr(),{},[&](const auto& event){if(event.roleId!="engineer")return;if(event.kind==ark::AgentTeamEvent::Message&&event.text.find("Assignment turn ")!=std::string::npos){std::lock_guard lock(completionDelivered.mutex);++completionDelivered.arrivals;completionDelivered.condition.notify_all();}if(event.kind==ark::AgentTeamEvent::Status&&event.text.find("Turn finished")==0){std::lock_guard lock(leadReturned.mutex);++leadReturned.arrivals;leadReturned.condition.notify_all();}});
        if(leadWorking){leadBoundary.await(1);specialistBoundary.await(1);specialistBoundary.open();completionDelivered.await(1);leadBoundary.open();}else{specialistBoundary.await(1);leadReturned.await(1);specialistBoundary.open();}
        complete(team);check(received&&leadRequests>=3,"automatic completion evidence reaches a subsequent lead provider request in either scheduling order");check(team.snapshot().roles[0].turns==(leadWorking?1u:2u),"Parallel coalesces completion in an active turn and starts a new turn after the lead has returned");
    }
}
static void sharedFailureEvidence(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("designer",true),role("reviewer")};
    std::atomic_bool failed=false,authorSaw=false,peerSaw=false,eventSaw=false;int designerRound=0,reviewerRound=0;
    const auto rejection=json(R"({"isError":true,"structuredContent":{"error_code":"NATIVE_SINGLE_SOLID_REQUIRED","error":"Result contains 2 solids","observed_solid_count":2,"revision":7,"document_unchanged":true,"recovery":{"inspect_before_retry":true}}})");
    config.roles[0].transport=[&](auto&,const V& request,auto*){if(++designerRound==1)return call("bad_union","doc_apply",R"({"expected_revision":7,"commands":[{"op":"feature.add","type":"native_solid"}]})");auto text=request.find("input")->dump();authorSaw=text.find("NATIVE_SINGLE_SOLID_REQUIRED")!=std::string::npos&&text.find("inspect_before_retry")!=std::string::npos;return done();};
    config.roles[1].transport=[&](auto&,const V& request,auto*){if(++reviewerRound==1){auto deadline=std::chrono::steady_clock::now()+2s;while(!failed&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);return call("review_context","team_context","{}");}auto text=request.find("input")->dump();peerSaw=text.find("last_tool_failure")!=std::string::npos&&text.find("NATIVE_SINGLE_SOLID_REQUIRED")!=std::string::npos&&text.find("inspect_before_retry")!=std::string::npos;return done();};
    team.start(config,"Recover a failed CAD operation",ark::AgentSession::tools(),[&](auto&,auto&){return rejection;},[&](const auto& event){if(event.kind==ark::AgentTeamEvent::ToolEnd&&event.toolError){eventSaw=event.text.find("2 solids")!=std::string::npos;failed=true;}});
    complete(team);check(authorSaw&&peerSaw&&eventSaw,"author, teammate context and UI event receive actionable rejection evidence");check(team.snapshot().roles[0].stepsApplied==0,"failed step never advances accepted construction count");
}
static void focusedSequentialAssignments(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.maxWorkers=8;
    config.roles={role("engineer",true),role("designer",true),role("reviewer")};
    config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;config.roles[1].discipline=ark::AgentTeamDiscipline::PartDesigner;config.roles[2].discipline=ark::AgentTeamDiscipline::PhysicsReviewer;
    Gate handoff;std::atomic_int engineerRounds=0,designerRounds=0,reviewerRounds=0,live=0,peak=0;std::atomic_bool truthfulQueue=false;
    struct Request {std::atomic_int& live;Request(std::atomic_int& count,std::atomic_int& peak):live(count){int now=++live,old=peak;while(now>old&&!peak.compare_exchange_weak(old,now)){};}~Request(){--live;}};
    config.roles[0].transport=[&](auto&,const V& request,auto*){Request running(live,peak);switch(++engineerRounds){
        case 1:return call("lead_inspection","doc_info","{}");
        case 2:return call("specific_component","team_send",R"({"to":"designer","message":"Design the mounting bracket only; retain the main assembly interfaces."})");
        case 3:truthfulQueue=request.find("input")->dump().find("recipient runs after the current turn ends")!=std::string::npos;handoff.enter();return done("Main design handed off for the bracket.");
        default:return done("Integrated the reported evidence.");}};
    config.roles[1].transport=[&](auto&,auto&,auto*){Request running(live,peak);switch(++designerRounds){
        case 1:return call("specific_review","team_send",R"({"to":"reviewer","message":"Review the bracket load assumptions at revision 0 only."})");
        case 2:return call("report_bracket","team_send",R"({"to":"engineer","message":"Bracket review queued; no geometry or physical qualification is claimed."})");
        default:return done("Bracket assignment reported.");}};
    config.roles[2].transport=[&](auto&,auto&,auto*){Request running(live,peak);++reviewerRounds;return done("Unknown load is the remaining bracket assumption.");};
    team.start(config,"Design one coherent assembly",ark::AgentSession::tools(),[](auto&,auto&){return json(R"({"revision":0})");});
    handoff.await(1);auto queued=team.snapshot();check(queued.roles[0].state==ark::AgentTeamState::Working&&queued.roles[1].state==ark::AgentTeamState::Queued&&queued.roles[2].turns==0,"focused start runs only the lead and queues the explicitly assigned designer");
    check(designerRounds==0&&reviewerRounds==0&&peak==1,"a queued specialist cannot start while the engineer conversation is active even with eight configured workers");handoff.open();complete(team);
    auto finished=team.snapshot(true);check(peak==1&&designerRounds==3&&reviewerRounds==1&&finished.roles[0].turns==2,"nested assignments drain sequentially and measured completion wakes the engineer once");
    check(truthfulQueue&&finished.roles[0].conversation.dump().find("do not poll team_context or wait for a queued teammate")!=std::string::npos,"sequential handoff explicitly tells the provider to finish instead of polling unavailable work");
    check(finished.roles[1].conversation.dump().find("doc_info returned result")==std::string::npos,"successful peer reads do not add automatic receipt chatter");
    int designerReports=0;for(const auto& event:finished.events)if(event.kind==ark::AgentTeamEvent::Message&&event.roleId=="engineer"&&event.text.find("Teammate designer -> engineer")!=std::string::npos)++designerReports;
    check(designerReports==1,"explicit specialist report replaces duplicate automatic completion notification");
    check(team.steer("Change the main design width"),"focused goal update accepted without naming a recipient");complete(team);auto steered=team.snapshot();
    check(steered.roles[0].turns==finished.roles[0].turns+1&&steered.roles[1].turns==finished.roles[1].turns&&steered.roles[2].turns==finished.roles[2].turns,"default focused steering wakes the engineer without restarting designers or reviewer");
    check(team.steer("Shared interface decision","all"),"explicit entire-team steering remains available");complete(team);auto broadcast=team.snapshot();
    check(broadcast.roles[1].turns==steered.roles[1].turns+1&&broadcast.roles[2].turns==steered.roles[2].turns+1&&peak==1,"explicit broadcasts reach all specialists while retaining focused provider concurrency");
}
static void leadEngineerAuthorsPartsAndAssembly(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;
    int rounds=0,rejected=0;bool chainReachedKernel=false;ark::AgentSession session;
    config.roles[0].transport=[&](auto&,auto&,auto*){switch(++rounds){
        case 1:return call("lead_sketch","team_apply_step",R"({"expected_revision":0,"commands":[{"op":"sketch.set","name":"Profile","sketch":{"plane":"XY","entities":[{"kind":"circle","name":"rim","dof":[0,0,2]}],"constraints":[]}}]})");
        case 2:return call("lead_part","team_apply_step",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"width","expr":10},{"op":"material.set","name":"Lead material","density":2.7},{"op":"feature.add","type":"box","name":"Lead part","p":{"dx":"width","dy":12,"dz":4},"material":"Lead material"}]})");
        case 3:return call("lead_anchor","team_apply_step",R"({"expected_revision":2,"commands":[{"op":"assembly.add","name":"Anchor","feature":"Lead part","fixed":true}]})");
        case 4:return call("lead_follower","team_apply_step",R"({"expected_revision":3,"commands":[{"op":"assembly.add","name":"Follower","feature":"Lead part","p":{"tx":20}}]})");
        case 5:return call("lead_mate","team_apply_step",R"({"expected_revision":4,"commands":[{"op":"mate.add","a":"Anchor","b":"Follower","type":"coincident"}]})");
        case 6:return call("invalid_part","team_apply_step",R"({"expected_revision":5,"commands":[{"op":"param.set","name":"rollback","expr":999},{"op":"feature.edit","id":"Missing geometry","p":{"dx":20}}]})");
        case 7:return call("stale_lead","team_apply_step",R"({"expected_revision":4,"commands":[{"op":"param.set","name":"width","expr":99}]})");
        case 8:return call("chain_authority","team_apply_step",R"({"expected_revision":5,"commands":[{"op":"drag_chain.remove","name":"Missing chain"}]})");
        default:return done("Part and assembly authored through measured transactions.");}};
    team.start(config,"Engineer the geometry and assembly",ark::AgentSession::tools(),[&](auto& name,auto& args){auto result=session.call(name,args);if(result.find("isError")->b)++rejected;if(result.dump().find("DRAG_CHAIN_NOT_FOUND")!=std::string::npos)chainReachedKernel=true;return result;});complete(team);
    check(session.revision==5&&session.document.features.size()==1&&session.document.assembly.size()==2&&session.document.mates.size()==1&&session.document.sketches.size()==1&&session.document.materials.size()==1,"lead engineer can actually author sketches, materials, parts, placements and mates");
    check(rejected==3&&chainReachedKernel&&session.document.params.size()==1&&session.document.getParam("width")==10&&team.snapshot().roles[0].stepsApplied==5,"engineer authority includes drag chains and retains atomic evaluation rollback and stale-revision rejection");
}
static void parallelProgressStillReportsCompletion(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.orchestratedStart=true;config.roles={role("engineer",true),role("designer",true)};
    config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;config.roles[1].discipline=ark::AgentTeamDiscipline::PartDesigner;
    Gate finishDesigner,progressConsumed;std::atomic_int engineerRounds=0,designerRounds=0;std::atomic_bool sawProgress=false;
    config.roles[0].transport=[&](auto&,const V& request,auto*){if(request.find("input")->dump().find("Progress only: bracket still in development")!=std::string::npos)sawProgress=true;
        if(++engineerRounds==1)return call("assign_parallel","team_send",R"({"to":"designer","message":"Develop the bracket independently."})");return done("Progress inspected; waiting for the completed component.");};
    config.roles[1].transport=[&](auto&,auto&,auto*){if(++designerRounds==1)return call("parallel_progress","team_send",R"({"to":"engineer","message":"Progress only: bracket still in development"})");finishDesigner.enter();return done("Bracket completed after the earlier progress message.");};
    team.start(config,"Parallel component assignment",V::makeArr(),{},[&](const auto& event){if(event.kind==ark::AgentTeamEvent::Status&&event.roleId=="engineer"&&event.text.find("Turn finished")==0&&sawProgress){std::lock_guard lock(progressConsumed.mutex);++progressConsumed.arrivals;progressConsumed.condition.notify_all();}});
    finishDesigner.await(1);progressConsumed.await(1);auto earlier=team.snapshot();check(earlier.roles[0].state==ark::AgentTeamState::Idle&&earlier.roles[1].state==ark::AgentTeamState::Working,"parallel lead may consume a progress message while its designer remains active");check(!earlier.goalDeclared&&earlier.goalState!=ark::AgentGoalState::Completed,"Idle lead and a progress message do not finish the team goal");finishDesigner.open();complete(team);
    auto completed=team.snapshot();int reports=0;for(const auto& event:completed.events)if(event.kind==ark::AgentTeamEvent::Message&&event.roleId=="engineer"&&event.text.find("Teammate designer -> engineer")!=std::string::npos)++reports;
    check(reports==2&&completed.roles[0].turns==earlier.roles[0].turns+1,"parallel completion wakes the lead even when an earlier message already reported progress");
}
static void coherentBatchBenchmark(){
    const auto commands=json(R"([
        {"op":"feature.add","type":"cylinder","name":"Cylinder","p":{"r":3,"h":12}},
        {"op":"assembly.add","name":"Cylinder instance","feature":"Cylinder"},
        {"op":"feature.add","type":"sphere","name":"Sphere","p":{"r":4}},
        {"op":"assembly.add","name":"Sphere instance","feature":"Sphere","p":{"tx":20}},
        {"op":"feature.add","type":"cone","name":"Cone","p":{"rb":4,"rt":2,"h":10}},
        {"op":"assembly.add","name":"Cone instance","feature":"Cone","p":{"tx":40}},
        {"op":"feature.add","type":"torus","name":"Torus","p":{"R":8,"r":2}},
        {"op":"assembly.add","name":"Torus instance","feature":"Torus","p":{"tx":60}}
    ])");
    struct Measurement{ark::AgentTeamSnapshot snapshot;ark::Document document;unsigned revisions=0;};
    auto run=[&](bool singleFeature,bool focused){ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=focused;
        if(singleFeature)config.maxFeaturesPerStep=config.maxPlacementsPerStep=1;
        config.roles={role("engineer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;config.roles[0].assistant.onboardingPack=ark::agentOnboardingPack({"overview"});
        int round=0;ark::AgentSession session;
        config.roles[0].transport=[&](auto&,auto&,auto*){if(++round>(singleFeature?4:1))return done("The four curved display fixtures and placements are committed.");V args=V::makeObj(),batch=V::makeArr();args.set("expected_revision",V::makeNum(round-1));
            if(singleFeature){batch.arr.push_back(commands.arr[(round-1)*2]);batch.arr.push_back(commands.arr[(round-1)*2+1]);}else batch=commands;args.set("commands",batch);return call("coherent_"+std::to_string(round),"team_apply_step",args.dump());};
        team.start(config,"Construct these four explicit fixture parts and placements coherently",ark::AgentSession::tools(),[&](auto& name,auto& args){return session.call(name,args);});complete(team);
        auto snapshot=team.snapshot();check(snapshot.roles[0].state==ark::AgentTeamState::Idle&&session.document.features.size()==4&&session.document.assembly.size()==4,"coherent multi-feature construction reaches actual geometry and all four placements");
        check(snapshot.roles[0].performance.providerRequests==(singleFeature?5:2)&&snapshot.roles[0].performance.toolCalls==(singleFeature?4:1)&&snapshot.performance.requestBytes==snapshot.roles[0].performance.requestBytes,"role and team metrics count measured calls/bytes once across actual execution");
        return Measurement{snapshot,session.document,static_cast<unsigned>(session.revision)};
    };
    const auto earlier=run(true,true),focused=run(false,true),parallel=run(false,false);
    check(earlier.revisions==4&&focused.revisions==1&&parallel.revisions==1&&focused.snapshot.roles[0].stepsApplied==1,"one coherent eight-command transaction replaces four forced feature transactions in both team modes");
    for(std::size_t i=0;i<4;++i)check(earlier.document.features[i].type==focused.document.features[i].type&&earlier.document.features[i].p==focused.document.features[i].p&&earlier.document.assembly[i].feature==focused.document.assembly[i].feature&&earlier.document.assembly[i].p==focused.document.assembly[i].p,"batching preserves authored geometry parameters and physical placements");
    check(focused.snapshot.performance.requestBytes<earlier.snapshot.performance.requestBytes&&parallel.snapshot.performance.providerRequests==focused.snapshot.performance.providerRequests,"fewer tool round trips lower transmitted bytes without restricting Parallel authoring");
    V report=V::makeObj();report.set("legacy_provider_requests",V::makeNum(earlier.snapshot.performance.providerRequests));report.set("batch_provider_requests",V::makeNum(focused.snapshot.performance.providerRequests));report.set("legacy_commits",V::makeNum(earlier.revisions));report.set("batch_commits",V::makeNum(focused.revisions));report.set("legacy_request_bytes",V::makeNum(earlier.snapshot.performance.requestBytes));report.set("batch_request_bytes",V::makeNum(focused.snapshot.performance.requestBytes));report.set("parallel_request_bytes",V::makeNum(parallel.snapshot.performance.requestBytes));std::cout<<"AGENT_BATCH_BENCHMARK "<<report.dump()<<'\n';
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("engineer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;ark::AgentSession session;int rounds=0;
    config.roles[0].transport=[&](auto&,auto&,auto*){if(++rounds==1)return call("atomic_negative","team_apply_step",R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"sphere","name":"Uncommitted part","p":{"r":4}},{"op":"feature.add","type":"torus","name":"Broken dependent part","p":{"R":"unknown_dimension","r":2}}]})");return done("The rejected batch leaves no partial part.");};
    team.start(config,"Reject an invalid coherent batch",ark::AgentSession::tools(),[&](auto& name,auto& args){return session.call(name,args);});complete(team);check(session.revision==0&&session.document.features.empty()&&team.snapshot().roles[0].stepsApplied==0,"larger batches retain evaluation rollback and never publish a valid prefix of rejected geometry");
}
static void restoredDocumentAndCanvasTools(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer",true),role("reviewer")};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;config.roles[1].discipline=ark::AgentTeamDiscipline::PhysicsReviewer;
    int round=0,rejected=0;std::vector<std::string> executed;ark::AgentSession session;bool allAdvertised=false;
    config.roles[0].transport=[&](auto&,const V& request,auto*){if(++round==1){std::set<std::string> names;for(const auto& tool:request.find("tools")->arr)names.insert(tool.find("name")->str);allAdvertised=names.count("doc_new")&&names.count("doc_undo")&&names.count("doc_redo")&&names.count("import_mesh")&&names.count("import_step")&&names.count("import_sheet_step");
            return call("canvas_part","doc_apply",R"({"expected_revision":0,"commands":[{"op":"canvas.set","name":"Concept reference","path":"isolated-test-reference.png","plane":"XY","width":120,"height":80},{"op":"feature.add","type":"sphere","name":"Editable concept","p":{"r":4},"manufacturing":{"route":"unspecified"}}]})");}
        switch(round){
            case 2:return call("lead_undo","doc_undo",R"({"expected_revision":1})");
            case 3:return call("lead_redo","doc_redo",R"({"expected_revision":2})");
            case 4:return call("lead_new","doc_new",R"({"expected_revision":3})");
            case 5:return call("undo_requested_new","doc_undo",R"({"expected_revision":4})");
            case 6:return call("missing_mesh","import_mesh",R"({"expected_revision":5,"path":"ARK_TEST_MISSING_SOURCE_4B39E294.obj"})");
            case 7:return call("missing_step","import_step",R"({"expected_revision":5,"path":"ARK_TEST_MISSING_SOURCE_4B39E294.step"})");
            case 8:return call("missing_sheet","import_sheet_step",R"({"expected_revision":5,"path":"ARK_TEST_MISSING_SOURCE_4B39E294.step","k_factor":0.5})");
            case 9:return call("stale_undo","doc_undo",R"({"expected_revision":4})");
            default:return done("Requested concept edit/history operations completed; the explicitly missing test imports rejected.");}};
    config.roles[1].transport=[](auto&,auto&,auto*){return done("Independent review waits for a requested checkpoint.");};
    team.start(config,"Exercise the concept reference, undo/redo/new and explicitly missing test-source imports; no fabrication claim",ark::AgentSession::tools(),[&](auto& name,auto& args){executed.push_back(name);auto result=session.call(name,args);if(result.find("isError")->b)++rejected;return result;});complete(team);auto snapshot=team.snapshot(true);
    check(allAdvertised&&executed.size()==9&&rejected==4,"lead engineer can reach public history/import tools and receives actual source/revision errors rather than a role denial");
    check(session.revision==5&&session.document.features.size()==1&&session.document.canvases.size()==1&&snapshot.roles[0].stepsApplied==5,"restored undo/redo/new/canvas operations preserve real history and reject stale or missing-source mutations atomically");
    const auto lead=snapshot.roles[0].conversation.dump(),review=snapshot.roles[1].conversation.dump();
    check(lead.find("first create a coherent whole editable design")!=std::string::npos&&lead.find("explicitly unspecified")!=std::string::npos&&review.find("do not impose fabrication routes")!=std::string::npos&&snapshot.roles[1].turns==0,"concept-first lead and targeted reviewer instructions do not manufacture a premature fabrication checkpoint");
    config.roles={role("designer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::PartDesigner;round=0;ark::AgentSession designer;
    config.roles[0].transport=[&](auto&,auto&,auto*){if(++round==1)return call("designer_reference","doc_apply",R"({"expected_revision":0,"commands":[{"op":"canvas.set","name":"Assigned reference","path":"isolated-test-reference.png","plane":"XY","width":80,"height":50}]})");return done("Assigned reference created.");};
    team.start(config,"Author the assigned reference canvas only",ark::AgentSession::tools(),[&](auto& name,auto& args){return designer.call(name,args);});complete(team);check(designer.revision==1&&designer.document.canvases.size()==1,"part designer retains canvas authoring without acquiring document-wide history/import authority");
}
static void ownedPreparedCandidates(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;
    ark::AgentSession session;int round=0,executors=0,preparedCommits=0;std::string candidate;
    config.roles[0].transport=[&](auto&,const V& request,auto*){switch(++round){
        case 1:return call("inspect_candidate","doc_info","{}");
        case 2:return call("prepare_candidate","doc_preview",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"radius","expr":4},{"op":"feature.add","type":"sphere","name":"Cached concept","p":{"r":"radius"}},{"op":"assembly.add","name":"Cached placement","feature":"Cached concept"}],"checks":[{"query":{"tool":"eval_expr","args":{"expr":"radius"},"path":"value"},"op":"eq","value":4,"tolerance":0.000000001}]})");
        case 3:{V response;arkjson::parse(request.find("input")->arr.back().find("output")->str,response);candidate=response.find("structuredContent")->find("candidate_id")->str;check(session.revision==0&&session.document.features.empty(),"successful team preview has not committed document geometry");return call("no_rebase_candidate","team_apply_step","{\"expected_revision\":1,\"candidate_id\":\""+candidate+"\"}");}
        case 4:return call("commit_candidate","doc_apply","{\"expected_revision\":0,\"candidate_id\":\""+candidate+"\"}");
        case 5:return call("no_replay_candidate","team_apply_step","{\"expected_revision\":1,\"candidate_id\":\""+candidate+"\"}");
        case 6:return call("failed_candidate","doc_preview",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"radius","expr":5}],"checks":[{"query":{"tool":"eval_expr","args":{"expr":"radius"},"path":"value"},"op":"eq","value":999}]})");
        case 7:return call("unknown_candidate","doc_apply",R"({"expected_revision":1,"candidate_id":"not-a-measured-preview"})");
        default:return done("The owned measured candidate committed without a second geometry evaluation.");}};
    team.start(config,"Preview and commit a coherent concept at the observed revision",ark::AgentSession::tools(),[&](auto& name,auto& args){++executors;auto result=session.call(name,args);const auto* data=result.find("structuredContent");if(data&&data->find("applied_prepared")&&data->find("applied_prepared")->b){++preparedCommits;check(!data->find("geometry_recomputed")->b&&data->find("features_evaluated")->num==0,"cached team commit reuses the actual evaluated candidate");}return result;});complete(team);auto snapshot=team.snapshot();
    check(executors==4&&preparedCommits==1&&session.revision==1&&session.document.features.size()==1&&session.document.assembly.size()==1&&session.document.getParam("radius")==4,"owned cached commit retains original checked commands, while rebase/replay/unknown candidates never reach executor");
    check(snapshot.roles[0].stepsApplied==1,"only actual cached publication increments construction count");
    bool attributed=false;for(const auto& event:snapshot.events)if(event.kind==ark::AgentTeamEvent::ConstructionEnd&&!event.toolError)attributed=event.operations.size()==3&&event.featureNames.size()==1&&event.featureNames[0]=="Cached concept";
    check(attributed,"cached construction events expose the actual previously inspected recipe instead of hidden operations");
    config.roles={role("engineer",true),role("designer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;config.roles[1].discipline=ark::AgentTeamDiscipline::PartDesigner;
    ark::AgentSession isolated;round=0;int designerRound=0,sharedExecutors=0;std::string otherCandidate,ownCandidate;bool borrowedRejected=false,scopeRejected=false,filtered=false;
    config.roles[0].transport=[&](auto&,const V& request,auto*){switch(++round){
        case 1:return call("lead_preview","doc_preview",R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"sphere","name":"Lead preview","p":{"r":4}},{"op":"assembly.add","name":"Lead instance","feature":"Lead preview"}]})");
        case 2:{V response;arkjson::parse(request.find("input")->arr.back().find("output")->str,response);otherCandidate=response.find("structuredContent")->find("candidate_id")->str;return call("assign_designer","team_send",R"({"to":"designer","message":"Work only on your own component and report actual evidence."})");}
        default:return done("Specific component assigned.");}};
    config.roles[1].transport=[&](auto&,const V& request,auto*){switch(++designerRound){
        case 1:{for(const auto& tool:request.find("tools")->arr)if(tool.find("name")->str=="doc_apply"){const auto choices=tool.find("parameters")->find("properties")->find("commands")->find("items")->find("properties")->find("op")->find("enum")->dump();filtered=choices.find("assembly.add")==std::string::npos&&choices.find("canvas.set")!=std::string::npos;}return call("borrowed_candidate","doc_apply","{\"expected_revision\":0,\"candidate_id\":\""+otherCandidate+"\"}");}
        case 2:borrowedRejected=request.find("input")->arr.back().find("output")->str.find("owned by this role")!=std::string::npos;return call("wrong_scope_preview","doc_preview",R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"sphere","name":"Wrong scope","p":{"r":4}},{"op":"assembly.add","name":"Foreign placement","feature":"Wrong scope"}]})");
        case 3:scopeRejected=request.find("input")->arr.back().find("output")->str.find("different team discipline")!=std::string::npos;return call("own_preview","doc_preview",R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"cylinder","name":"Designer concept","p":{"r":2,"h":5}}]})");
        case 4:{V response;arkjson::parse(request.find("input")->arr.back().find("output")->str,response);ownCandidate=response.find("structuredContent")->find("candidate_id")->str;return call("own_commit","team_apply_step","{\"expected_revision\":0,\"candidate_id\":\""+ownCandidate+"\"}");}
        default:return done("Own component committed.");}};
    team.start(config,"Preserve designer ownership and measured preview scope",ark::AgentSession::tools(),[&](auto& name,auto& args){++sharedExecutors;return isolated.call(name,args);});complete(team);auto roles=team.snapshot(true);
    check(filtered&&borrowedRejected&&scopeRejected&&sharedExecutors==3&&isolated.revision==1&&isolated.document.features.size()==1&&isolated.document.features[0].name=="Designer concept"&&isolated.document.assembly.empty(),"role-filtered schema and runtime reject borrowed or foreign-operation candidates while allowing an owned designer cache commit");
    check(roles.roles[1].conversation.dump().find("new features and 8 placements")==std::string::npos,"generic designer workflow no longer advertises forbidden assembly placements");
}
// By default a retained failed read stays visible but does not block completion;
// a failed write still does. Agents spent whole runs chasing read errors.
static void readFailureDoesNotBlockCompletion(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;int round=0;bool accepted=false;
    config.roles[0].transport=[&](auto&,const V& request,auto*){switch(++round){
        case 1:return call("failed_read","collision_inspect",R"({"a":"Missing part","b":"Other part"})");
        case 2:return call("finish_with_read_failure","team_finish_goal",R"({"state":"completed","summary":"Concept complete; one collision query used a wrong selector."})");
        case 3:{const auto& output=request.find("input")->arr.back();accepted=output.find("output")&&output.find("output")->str.find("Unresolved tool")==std::string::npos;return done("Reported.");}
        default:return done("Done.");}};
    team.start(config,"Finish a concept after a rejected read",ark::AgentSession::tools(),[](const std::string& name,const V&){if(name=="collision_inspect")return json(R"({"isError":true,"structuredContent":{"revision":0,"error_code":"EVALUATION_FAILED","error":"Collision feature not found"}})");return json(R"({"revision":0})");});
    complete(team);const auto snapshot=team.snapshot();
    check(accepted&&snapshot.goalState==ark::AgentGoalState::Completed&&snapshot.unresolvedErrors==1,"a retained failed read stays visible without blocking the goal report");
}
// 047D: a committed feature.remove supersedes rejected reads of that feature
// (no later same-subject read can succeed); reads of surviving features remain.
static void removedSubjectSupersedesRejectedRead047d(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;int round=0;bool reported=false;
    config.roles[0].transport=[&](auto&,const V& request,auto*){switch(++round){
        case 1:return call("probe_read","frame_inspect",R"({"id":2})");
        case 2:return call("kept_read","frame_inspect",R"({"id":3})");
        case 3:return call("remove_probe","doc_apply",R"({"expected_revision":0,"commands":[{"op":"feature.remove","id":2}]})");
        case 4:{for(const auto& item:request.find("input")->arr)if(const auto* output=item.find("output");output&&output->t==V::Str&&output->str.find("subject_removed")!=std::string::npos)reported=true;return done("Removed the probe.");}
        default:return done("Done.");}};
    team.start(config,"Remove a probe frame after a rejected read",ark::AgentSession::tools(),[](const std::string& name,const V&){if(name=="frame_inspect")return json(R"({"isError":true,"structuredContent":{"revision":0,"error_code":"QUERY_BATCH_ROW_TOO_LARGE","error":"Result is 3967 bytes, above max_bytes 3000"}})");return json(R"({"revision":1})");});
    complete(team);const auto snapshot=team.snapshot();
    check(reported,"the committing receipt reports the subject_removed supersession");
    check(snapshot.roles[0].unresolvedToolErrors==1&&snapshot.unresolvedErrors==1,"only the rejected read of the removed feature is superseded");
}
// Studio asks the lead once for a review checkpoint before completing when the
// read-only reviewer was never assigned; repeating the report still completes.
static void reviewerCheckpointReminder(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.requireReviewerCheckpoint=true;
    config.roles={role("engineer",true),role("reviewer",false)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;config.roles[1].discipline=ark::AgentTeamDiscipline::PhysicsReviewer;
    int round=0;std::string reminder,second;
    config.roles[0].transport=[&](auto&,const V& request,auto*){auto last=[&]{const auto& output=request.find("input")->arr.back();return output.find("output")?output.find("output")->str:std::string();};switch(++round){
        case 1:return call("finish_unreviewed","team_finish_goal",R"({"state":"completed","summary":"Concept placed as an assembly."})");
        case 2:reminder=last();return call("finish_review_skipped","team_finish_goal",R"({"state":"completed","summary":"Concept placed as an assembly; the user declined a review, so it was skipped."})");
        case 3:second=last();return done("Reported.");
        default:return done("Done.");}};
    config.roles[1].transport=[](auto&,auto&,auto*){return done("Not assigned.");};
    team.start(config,"Finish an unreviewed concept",ark::AgentSession::tools(),[](auto&,auto&){return json(R"({"revision":0})");});
    complete(team);const auto snapshot=team.snapshot();
    check(reminder.find("Reviewer reviewer has not checked")!=std::string::npos&&reminder.find("team_send kind:assignment")!=std::string::npos,"the first unreviewed completion asks for a reviewer checkpoint");
    check(second.find("has not checked")==std::string::npos&&snapshot.goalState==ark::AgentGoalState::Completed,"the reminder is sent once and the repeated report completes");
}
// Every provider round re-sends tools, instructions and history. Measure the
// first request of the Studio's three roles with their real catalogues and
// onboarding packs as Studio configures them (specialist tools deferred), so
// catalogue growth is visible in the test output.
static void teamRequestSizes(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.maxWorkers=1;
    const std::pair<const char*,ark::AgentTeamDiscipline> roles[]={{"engineer",ark::AgentTeamDiscipline::Orchestrator},{"designer",ark::AgentTeamDiscipline::PartDesigner},{"reviewer",ark::AgentTeamDiscipline::PhysicsReviewer}};
    std::mutex mutex;std::map<std::string,V> first;
    for(const auto& [id,discipline]:roles){auto item=role(id,discipline!=ark::AgentTeamDiscipline::PhysicsReviewer);item.discipline=discipline;item.assistant.deferSpecialistTools=true;item.assistant.onboardingPack=discipline==ark::AgentTeamDiscipline::PhysicsReviewer?ark::agentOnboardingPack({"overview","physics"}):ark::agentAuthoringOnboardingPack();
        const std::string roleId=id;auto rounds=std::make_shared<int>(0);
        item.transport=[&,roleId,rounds](const auto&,const V& request,const auto*){{std::lock_guard lock(mutex);first.emplace(roleId,request);}
            if(++*rounds<=6)return call(roleId+"_read_"+std::to_string(*rounds),"doc_info",R"({"detail":"revision"})");return done(roleId+" measured");};config.roles.push_back(std::move(item));}
    team.start(config,"Measure the request envelope",ark::AgentSession::tools(),[](auto&,auto&){return json(R"({"revision":0})");});complete(team);
    std::cout<<"TEAM_REQUEST_BYTES {";bool comma=false;
    for(const auto& [id,request]:first){const auto tools=request.find("tools")?request.find("tools")->dump().size():0;const auto input=request.find("input")?request.find("input")->dump().size():0;const auto instructions=request.find("instructions")?request.find("instructions")->dump().size():0;
        std::size_t largest=0;std::string largestName;if(const auto* list=request.find("tools"))for(const auto& tool:list->arr){const auto size=tool.dump().size();if(size>largest){largest=size;largestName=tool.find("name")?tool.find("name")->asStr():"";}}
        std::cout<<(comma?",":"")<<"\""<<id<<"\":{\"request\":"<<request.dump().size()<<",\"tools\":"<<tools<<",\"tool_count\":"<<(request.find("tools")?request.find("tools")->arr.size():0)<<",\"largest_tool\":\""<<largestName<<"\",\"largest_tool_bytes\":"<<largest<<",\"instructions\":"<<instructions<<",\"input\":"<<input<<"}";comma=true;}
    std::cout<<"}\n";check(first.size()==3,"each Studio role sends a measured first request");
    for(const auto& role:team.snapshot().roles){const auto metrics=ark::assistantContextMetrics(role.performance);std::cout<<"TEAM_PREFIX_REUSE "<<role.id<<" "<<metrics.dump()<<"\n";
        check(metrics.find("prefix_compared_requests")->num>=6&&metrics.find("prefix_reuse_ratio")->num>.9,"tool rounds of one turn keep the previous request as a cacheable prefix");}
    for(const auto& [id,request]:first){std::vector<std::pair<std::size_t,std::string>> sizes;if(const auto* list=request.find("tools"))for(const auto& tool:list->arr)sizes.push_back({tool.dump().size(),tool.find("name")?tool.find("name")->asStr():""});std::sort(sizes.rbegin(),sizes.rend());
        std::cout<<"TEAM_LARGEST_TOOLS "<<id<<":";for(std::size_t i=0;i<sizes.size()&&i<12;++i)std::cout<<" "<<sizes[i].second<<"="<<sizes[i].first;std::cout<<"\n";}
}
static void singleAssistantPreviewSchema(){
    const auto full=ark::AgentSession::tools(),compact=ark::compactAuthoringPreviewTools(full);
    auto tool=[](const V& tools,const char* name)->const V&{for(const auto& item:tools.arr)if(item.find("name")->asStr()==name)return item;throw std::runtime_error("missing tool");};
    const auto& preview=tool(compact,"doc_preview");const auto& fullPreview=tool(full,"doc_preview");
    check(tool(compact,"doc_apply").dump()==tool(full,"doc_apply").dump(),"doc_apply keeps the canonical nested command schema");
    check(preview.dump().size()*2<fullPreview.dump().size(),"single assistant doc_preview drops the duplicated nested command schema");
    const auto& fields=*preview.find("inputSchema")->find("properties")->find("commands")->find("items")->find("properties");const auto& original=*fullPreview.find("inputSchema")->find("properties")->find("commands")->find("items")->find("properties");
    check(fields.obj.size()==original.obj.size(),"every real command field remains advertised with a shallow hint");
    check(ark::compactAuthoringPreviewTools(V::makeArr()).arr.empty(),"lists without doc_apply are unchanged");
}
static void unsupportedCandidateRecovery040d(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;
    ark::AgentSession session;session.suppressFailureCapture();int round=0,previews=0,applies=0;std::string original,fresh;bool guidance=false,staleRejected=false;
    const auto recipe=json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"PreviewRadius","expr":4},{"op":"material.set","name":"PreviewMaterial","density":1}],"checks":[{"query":{"tool":"eval_expr","args":{"expr":"PreviewRadius"},"path":"value"},"op":"eq","value":4}]})");
    auto receipt=[](const V& request){V result;check(arkjson::parse(request.find("input")->arr.back().find("output")->str,result),"candidate recovery receives the actual preceding tool receipt");return result;};
    config.roles[0].transport=[&](auto&,const V& request,auto*){switch(++round){
        case 1:{const auto startup=request.find("input")->dump();guidance=startup.find("refresh_supported:false")!=std::string::npos&&startup.find("refresh_reason")!=std::string::npos&&startup.find("original authored commands/checks")!=std::string::npos&&startup.find("receipt reports refresh_supported:true")!=std::string::npos;return call("initial_authored_preview","doc_preview",recipe.dump());}
        case 2:{const auto data=*receipt(request).find("structuredContent");check(!data.find("refresh_supported")->b&&data.find("refresh_reason")&&session.revision==0&&session.document.sketches.empty(),"sketch/parameter preview explicitly rejects refresh support without publishing");original=data.find("candidate_id")->str;
            const auto peer=session.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"PeerSetting","expr":9}]})"));check(!peer.find("isError")->b&&session.revision==1,"unrelated synthetic peer edit advances the actual revision");return call("inspect_peer_revision","doc_info",R"({"detail":"revision"})");}
        case 3:{check(receipt(request).find("structuredContent")->find("revision")->num==1,"recovery uses the freshly inspected current revision");V args=V::makeObj();args.set("expected_revision",V::makeNum(1));args.set("candidate_id",V::makeStr(original));return call("stale_candidate_stays_rejected","doc_apply",args.dump());}
        case 4:{const auto rejected=receipt(request);staleRejected=rejected.find("isError")&&rejected.find("isError")->b;check(session.revision==1&&session.document.sketches.empty()&&session.document.params.size()==1,"stale candidate rejection preserves current peer work atomically");V args=recipe;args.set("expected_revision",V::makeNum(1));return call("explicit_authored_repreview","doc_preview",args.dump());}
        case 5:{const auto data=*receipt(request).find("structuredContent");fresh=data.find("candidate_id")->str;check(fresh!=original&&!data.find("refresh_supported")->b&&data.find("recipe")->find("commands")->dump()==recipe.find("commands")->dump()&&data.find("recipe")->find("checks")->dump()==recipe.find("checks")->dump(),"fresh preview preserves original authored intent and atomic checks without unsupported refresh");V args=V::makeObj();args.set("expected_revision",V::makeNum(1));args.set("candidate_id",V::makeStr(fresh));return call("publish_fresh_authored_preview","doc_apply",args.dump());}
        default:{const auto result=receipt(request);check(!result.find("isError")->b,"fresh current-revision candidate publishes successfully");return done("The explicit authored preview preserves unrelated peer work.");}}};
    team.start(config,"Recover a synthetic unsupported-refresh preview after an unrelated edit",ark::AgentSession::tools(),[&](const auto& name,const V& args){if(name=="doc_preview"){++previews;check(!args.find("candidate_id"),"unsupported preview recovery submits explicit authored commands, never candidate-ID refresh");}if(name=="doc_apply")++applies;return session.call(name,args);});complete(team);
    check(guidance&&staleRejected&&previews==2&&applies==1,"team guidance selects explicit re-preview while stale candidate publication remains rejected before execution");
    check(session.revision==2&&session.document.getParam("PeerSetting")==9&&session.document.getParam("PreviewRadius")==4&&session.document.materials.size()==1&&team.snapshot().roles[0].stepsApplied==1,"only the fresh checked candidate publishes and retains the unrelated current edit");
}
static void explicitGoalCompletion(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;int round=0;
    config.roles[0].transport=[&](auto&,auto&,auto*){switch(++round){case 1:return done("Done, all work complete.");case 2:return call("finished_goal","team_finish_goal",R"({"state":"completed","summary":"Requested read-only inspection finished; no geometry or manufacturing qualification claimed."})");case 4:return call("input_goal","team_finish_goal",R"({"state":"needs_input","summary":"Choose the requested material before the design can continue."})");default:return done("Reported current goal state.");}};
    team.start(config,"Inspect a concept",V::makeArr(),{});complete(team);auto idle=team.snapshot();check(!idle.goalDeclared&&idle.goalState==ark::AgentGoalState::NeedsInput&&idle.outstandingTasks==1,"Idle and confident model text never establish task or goal completion");
    check(team.steer("Finish the inspection explicitly"),"new goal decision accepted");complete(team);auto completed=team.snapshot();check(completed.goalDeclared&&completed.goalState==ark::AgentGoalState::Completed&&completed.outstandingTasks==0&&!completed.goalSummary.empty(),"explicit lead completion publishes the summary after actual queue/task checks");
    check(team.steer("Author another concept after I choose a material"),"next user goal reopens completion");complete(team);auto input=team.snapshot();check(input.goalDeclared&&input.goalState==ark::AgentGoalState::NeedsInput&&input.outstandingTasks==1,"new user goal cannot inherit preceding completed state and exposes requested input");
    int lead=0,designer=0;bool rejected=false;config.roles.push_back(role("designer",true));config.roles[1].discipline=ark::AgentTeamDiscipline::PartDesigner;
    config.roles[0].transport=[&](auto&,const V& request,auto*){switch(++lead){case 1:return call("assign","team_send",R"({"to":"designer","message":"Inspect the current component and report your assignment."})");case 2:return done("Specific assignment handed off.");case 3:return call("premature","team_finish_goal",R"({"state":"completed","summary":"Claimed finished despite an unreported task."})");case 4:rejected=request.find("input")->arr.back().find("output")->str.find("not been explicitly reported")!=std::string::npos;return call("ask_report","team_send",R"({"to":"designer","message":"Report the assignment state and evidence explicitly."})");case 5:return done("Await the assignment report.");case 6:return call("actual_finish","team_finish_goal",R"({"state":"completed","summary":"Component inspection integrated; exact reported scope only."})");default:return done("Lead final summary.");}};
    config.roles[1].transport=[&](auto&,auto&,auto*){switch(++designer){case 1:return done("Turn completed; this alone is not a task receipt.");case 2:return call("task_report","team_report_task",R"({"state":"completed","summary":"Assigned inspection complete; no write requested."})");case 3:return call("task_to_lead","team_send",R"({"to":"engineer","message":"Assignment explicitly completed; inspection only, no manufacturing qualification."})");default:return done("Assignment report delivered.");}};
    team.start(config,"Coordinate one specific inspection",V::makeArr(),{});complete(team);check(rejected&&team.snapshot().goalState==ark::AgentGoalState::Completed&&team.snapshot().outstandingTasks==0,"lead completion rejects Idle specialist until explicit assignment report and integration");
    config.roles.resize(1);round=0;int writes=0;bool errorRejected=false;
    config.roles[0].transport=[&](auto&,const V& request,auto*){switch(++round){case 1:return call("failed_write","doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":"10"}]})");case 2:return call("bad_finish","team_finish_goal",R"({"state":"completed","summary":"Ignore the rejected operation."})");case 3:errorRejected=request.find("input")->arr.back().find("output")->str.find("Unresolved tool")!=std::string::npos;return call("corrected_write","doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":"20"}]})");case 4:return call("checked_finish","team_finish_goal",R"({"state":"completed","summary":"Corrected operation returned a measured successful commit."})");default:return done("Final corrected summary.");}};
    team.start(config,"Resolve a rejected operation before finishing",ark::AgentSession::tools(),[&](auto&,auto&){if(++writes==1)return json(R"({"isError":true,"error":"Rejected geometry; document unchanged"})");return json(R"({"revision":1})");});complete(team);check(errorRejected&&writes==2&&team.snapshot().goalState==ark::AgentGoalState::Completed&&team.snapshot().unresolvedErrors==0,"unresolved tool failure blocks completion until a real successful corrected operation");
    team.cancel();complete(team);check(team.snapshot().goalState==ark::AgentGoalState::Interrupted&&!team.snapshot().goalDeclared,"cancellation is interruption even after a prior goal report");
}
#include "agent_team_context_tests.inl"
#include "agent_team_initial_checkpoint_037d_tests.inl"
static void explicitInputWithRetainedToolFailure(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;int round=0;
    config.roles[0].transport=[&](auto&,auto&,auto*){switch(++round){
        case 1:return call("blocked_review","bom_review",R"({"stage":"detailed","expected_revision":0})");
        case 2:return call("task_input","team_report_task",R"({"state":"needs_input","summary":"A required material source is unavailable."})");
        case 3:return call("goal_input","team_finish_goal",R"({"state":"needs_input","summary":"Provide the missing material source before detailed completion."})");
        default:return done("Waiting for the requested input.");}};
    team.start(config,"Inspect an authored concept without inventing missing sources",ark::AgentSession::tools(),[](auto&,auto&){return json(R"({"isError":true,"structuredContent":{"error_code":"REVIEW_BLOCKED","error":"A material source remains unresolved."}})");});complete(team);
    const auto snapshot=team.snapshot();
    check(snapshot.goalDeclared&&snapshot.goalState==ark::AgentGoalState::NeedsInput&&snapshot.unresolvedErrors==1,"explicit needs_input survives a retained tool rejection without pretending the provider was interrupted");
    check(snapshot.roles[0].state==ark::AgentTeamState::Idle&&snapshot.roles[0].taskState=="needs_input","normal provider completion retains the explicit input request");
    check(snapshot.roles[0].status.find("Waiting for input")!=std::string::npos&&snapshot.roles[0].unresolvedToolErrors==1&&snapshot.roles[0].toolFailures.empty(),"normal UI polling exposes blocker count without copying diagnostic/history payloads");
    const auto details=team.snapshot(false,~std::uint64_t(0),true);
    check(details.roles[0].conversation.arr.empty()&&details.roles[0].toolFailures.size()==1&&details.roles[0].toolFailures[0].find("tool")->str=="bom_review"&&details.roles[0].toolFailures[0].find("error_code")->str=="REVIEW_BLOCKED","opt-in diagnostics retain actual rejected tool and reason without provider history");
    team.cancel();complete(team);check(!team.snapshot().goalDeclared&&team.snapshot().goalState==ark::AgentGoalState::Interrupted,"actual cancellation still overrides a prior input report");
}
#include "agent_team_write_window_tests.inl"
#include "agent_team_contracts_032d_tests.inl"
#include "agent_team_workflow_033d_tests.inl"
#include "agent_team_graph_034d_tests.inl"
#include "agent_team_candidate_refresh_031b_tests.inl"
#include "agent_team_steering_tests.inl"
#include "agent_team_read_recovery_038d_tests.inl"
#include "agent_team_read_progress_041d_tests.inl"
#include "agent_team_report_feedback_042d_tests.inl"
int main(){try{reportRatingsReplaceFeedbackRound042d();rejectedReportRatingsRecordNothing042d();progressReportCarriesRatings042d();handoffNoticeCarriesReport042d();teamRequestSizes();readFailureDoesNotBlockCompletion();removedSubjectSupersedesRejectedRead047d();reviewerCheckpointReminder();singleAssistantPreviewSchema();inspectionProgressProjection041d();inspectionActionPriorities041d();nativeReadThenGeometry041d();observedReadProgress041d();unsupportedCandidateRecovery040d();projectedBatchReadRecovery038d();exactReferenceReadRecovery038d();staleReadRevisionRecovery038d();correctedPreviewRecovery038d();batchReceiptAssociationRecovery038d();initialCheckpointNeedsSketchGeometry037d();initialLeadAuthoringCheckpoint037d();initialCheckpointLifecycle037d();coherentConstructionBounds037d();semanticProgress036a();semanticHandoff036a();sharedGraph034d();workflow033d();cooperativeReceiptContracts032d();expiredReceiptOwnership032d();independentTeamCandidateRefresh031b();teamCandidateDependencyConflict031b();teamCandidateProjectBoundary031b();teamCandidateRefreshReceipt031b();explicitInputWithRetainedToolFailure();staleLeadCannotAssignDownstream();inFlightImageTeamSteering();snapshotsDuringPeerWriteWindow();userImageTeamChecks();cooperativeWriteWindows();cooperativeWriteWindowLifecycle();cooperativeWriteWindowPermissionsAndAudit();oversizedHistoryResume();sharedAuthoringSchemas();drawingTeamScope();explicitGoalCompletion();ownedPreparedCandidates();restoredDocumentAndCanvasTools();coherentBatchBenchmark();focusedSequentialAssignments();leadEngineerAuthorsPartsAndAssembly();parallelProgressStillReportsCompletion();sharedFailureEvidence();parallelAutomaticCompletionBoundaries();unlimitedEightRoleAssignments();configChecks();concurrencyAndRevision();steeringAndPause();collaboration();cancellationAndFailures();interruptedBatchHistory();limitsAndMutationSchemas();conversationMemoryBound();incrementalStepsAndScopes();frameDisciplineScope();proposalsAndPhysicsRole();concurrentQueuedDrafts();yieldedAndInterruptedContinuation();std::cout<<"Agent team checks passed: "<<checks<<"\n";return 0;}catch(const std::exception& error){std::cerr<<"Agent team checks failed: "<<error.what()<<"\n";return 1;}}
