#include "ark/agent.hpp"
#include "ark/assistant.hpp"
#include "ark/agent_team.hpp"
#include <iostream>
#include <stdexcept>
using namespace ark;using V=arkjson::Value;
static V json(const char* s){V v;if(!arkjson::parse(s,v))throw std::runtime_error("fixture JSON");return v;}
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static V call(AgentSession& s,const char* name,V args){auto r=s.call(name,args);if(r.find("isError")->b)throw std::runtime_error(r.find("structuredContent")->dump());return *r.find("structuredContent");}
static V response(const char* name,const char* id,V args){V r=json(R"({"status":"completed","output":[]})"),c=V::makeObj();c.set("type",V::makeStr("function_call"));c.set("name",V::makeStr(name));c.set("call_id",V::makeStr(id));c.set("arguments",V::makeStr(args.dump()));V out=V::makeArr();out.arr.push_back(c);r.set("output",out);return r;}
static V done(){return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Design complete."}]}]})");}
static void missingInputIsNotCompletion(){
    AgentSession s;call(s,"doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"Mount","part":{"part_number":"TEST-MOUNT","description":"Synthetic mount"}}]})"));
    AssistantConfig config;config.model="offline-bom-input";AssistantRetryHooks hooks;hooks.completionCheck=[&]{return s.bomCompletionCheck();};int rounds=0;
    auto run=runAssistant(config,V::makeStr("Finish the detailed assembly"),AgentSession::tools(),{}, {},nullptr,[&](auto&,auto&,auto*){++rounds;return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"needs_input: What load must this mount support? The assembly remains unfinished."}]}]})");},{},{},hooks);
    check(rounds==1&&run.text.rfind("needs_input:",0)==0&&!s.bomCompletionCheck().find("allowed")->b,"an explicit request for missing input is delivered without approving BOM completion");
    rounds=0;auto afterProgress=runAssistant(config,V::makeStr("Continue detailed work"),AgentSession::tools(),[&](const std::string& name,const V& args){return s.call(name,args);},{},nullptr,[&](auto&,auto&,auto*){
        if(++rounds==1){auto r=response("bom_inspect","inspect-progress",V::makeObj());auto messages=*r.find("output");messages.arr.push_back(json(R"({"type":"message","role":"assistant","content":[{"type":"output_text","text":"Inspecting the mount before proceeding."}]})"));r.set("output",messages);return r;}
        return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"needs_input: What load is required?"}]}]})");
    },{},{},hooks);
    check(afterProgress.text.rfind("needs_input:",0)==0&&afterProgress.text.find("Inspecting the mount")!=std::string::npos&&afterProgress.text.find("What load is required?")!=std::string::npos,"input state survives earlier progress without losing either message");
}
static void recoveryMakesProgress(){
    AgentSession s;call(s,"doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"Known","part":{"part_number":"TEST-KNOWN","description":"Synthetic known part"}},{"op":"feature.add","type":"box","name":"Unidentified"}]})"));
    auto gate=s.bomCompletionCheck();auto* repair=gate.find("recovery");
    check(repair&&repair->find("uninspected_count")->num==2&&repair->find("missing_identity_count")->num==1&&repair->find("missing_identity")->arr[0].find("feature")->num==2,"completion rejection identifies current unread and missing identity definitions");
    AssistantConfig config;config.model="offline-bom-recovery";AssistantRetryHooks hooks;hooks.completionCheck=[&]{return s.bomCompletionCheck();};int round=0;std::string visible;
    auto run=runAssistant(config,V::makeStr("Review BOM"),AgentSession::tools(),[&](const std::string& name,const V& args){return s.call(name,args);},[&](const AssistantEvent& e){if(e.kind==AssistantEvent::Text)visible+=e.text;},nullptr,[&](auto&,const V& request,auto*){
        ++round;if(round==2){bool offered=false;for(const auto& tool:request.find("tools")->arr)if(tool.find("name")&&tool.find("name")->str=="bom_review")offered=true;check(offered&&request.dump().find("missing_identity_count")!=std::string::npos,"recovery offers deferred BOM tools and includes structured blockers");return response("bom_inspect","first-page",json(R"({"ids":[1]})"));}
        if(round==4)return response("bom_inspect","second-page",json(R"({"ids":[2]})"));
        return done();
    },{},{},hooks);
    check(round==6&&run.text.rfind("review_pending:",0)==0&&visible.find("Design complete.")==std::string::npos,"inspection progress at the same revision is retained; repeated unchanged final becomes a review stop without leaking completion");
    check(!s.bomCompletionCheck().find("allowed")->b&&s.revision==1,"recoverable stop does not approve or mutate BOM");
    s.beginBomTask();call(s,"bom_inspect",V::makeObj());
    auto review=json(R"({"completion":"task","expected_revision":1,"scope":"concept","pending_notes":"Only display inspected; global BOM remains pending.","fastening_review":"No connections changed by the display-only task."})");
    const auto receipt=call(s,"bom_review",review);
    check(receipt.find("selected_feature_ids")->arr.empty()&&s.bomCompletionCheck(false,true).find("allowed")->b&&!s.bomCompletionCheck().find("allowed")->b,"explicit empty affected task review does not force unrelated metadata or approve global project");
    call(s,"doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"feature.edit","id":1,"p":{"dx":22}}]})"));
    check(!s.bomCompletionCheck(false,true).find("allowed")->b,"empty task review invalidates after a physical edit");
    call(s,"bom_inspect",json(R"({"ids":[1]})"));review.set("expected_revision",V::makeNum(2));auto changed=call(s,"bom_review",review);
    check(changed.find("selected_feature_ids")->dump()=="[1]"&&s.bomCompletionCheck(false,true).find("allowed")->b,"omitting task ids selects actual affected definitions rather than unrelated project inventory");
}
static void limitedTaskCompletion(){
    AgentSession s;
    call(s,"doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"Existing unidentified housing"},{"op":"feature.add","type":"box","name":"Edited cable envelope","part":{"part_number":"TEST-CABLE","description":"Synthetic cable envelope","procurement":"unspecified"}},{"op":"feature.add","type":"box","name":"Other part","part":{"part_number":"TEST-OTHER","description":"Other synthetic part","procurement":"make"}}]})"));
    auto review=json(R"({"expected_revision":1,"ids":[2],"completion":"task","scope":"concept","pending_notes":"Cable material, electrical properties and flex-life are unresolved; the global BOM is outside this task.","fastening_review":"Synthetic envelope only; strain relief is unresolved."})");
    call(s,"bom_inspect",json(R"({"ids":[2]})"));
    check(s.call("bom_review",review).find("isError")->b,"model cannot create its own task boundary");
    s.beginBomTask();
    call(s,"doc_apply",json(R"({"expected_revision":1,"actor":{"kind":"agent","id":"engineer"},"commands":[{"op":"feature.edit","id":2,"p":{"dx":24}}]})"));
    auto inspect=call(s,"bom_inspect",json(R"({"ids":[2]})"));
    check(inspect.find("task_scope")->find("affected_feature_ids")->dump()=="[2]","host discovers actual target change despite incomplete unrelated BOM");
    s.beginBomTask(true);check(s.bomTaskScope().find("affected_feature_ids")->dump()=="[2]","resume retains previous affected definitions instead of inventing a summary-only task");
    review.set("expected_revision",V::makeNum(2));auto ordinary=review;ordinary.set("completion",V::makeStr("project"));
    call(s,"bom_review",ordinary);check(!s.bomCompletionCheck(true,true).find("allowed")->b,"ordinary selected review still cannot finalize a task");
    auto detailed=review;detailed.set("scope",V::makeStr("detailed"));check(s.call("bom_review",detailed).find("isError")->b,"task scope does not weaken detailed metadata requirements");
    const auto authored=documentToJson(s.document);call(s,"bom_review",review);auto task=s.bomCompletionCheck(true,true);
    check(task.find("summary")->str.size()<240&&task.find("summary")->str.find("strain relief")==std::string::npos,"visible BOM qualification stays short while detailed notes stay in review receipt");
    check(task.find("allowed")->b&&!task.find("project_completion_allowed")->b&&!s.bomCompletionCheck().find("allowed")->b,"task completes while global completion remains blocked");
    check(task.find("summary")->str.find("Global project/BOM completion remains pending")!=std::string::npos&&documentToJson(s.document)==authored&&s.revision==2,"host qualification is explicit and review never changes authoring");
    AssistantConfig config;config.model="offline-task";AssistantRetryHooks hooks;hooks.completionCheck=[&]{return s.bomCompletionCheck(true,true);};
    auto run=runAssistant(config,V::makeStr("Finish only the cable edit"),AgentSession::tools(),[&](const std::string& name,const V& args){return s.call(name,args);},{},nullptr,[](auto&,auto&,auto*){return done();},{},{},hooks);
    check(run.text.find("Global project/BOM completion remains pending")!=std::string::npos,"single assistant can deliver a qualified limited result without BOM retry loop");
    AgentTeam team;AgentTeamConfig teamConfig;teamConfig.completionCheck=[&]{return s.bomCompletionCheck(false,true);};AgentTeamRole lead;lead.id="engineer";lead.assistant=config;lead.canWrite=true;lead.discipline=AgentTeamDiscipline::Orchestrator;int rounds=0;
    lead.transport=[&](auto&,auto&,auto*){return ++rounds==1?response("team_finish_goal","limited",json(R"({"state":"completed","summary":"Limited cable edit complete; global BOM remains pending."})")):done();};teamConfig.roles.push_back(lead);team.start(teamConfig,"Finish only the cable edit",AgentSession::tools(),[&](const std::string& name,const V& args){return s.call(name,args);});team.waitIdle();
    check(team.snapshot().goalState==AgentGoalState::Completed&&team.snapshot().goalSummary.find("Global project/BOM completion remains pending")!=std::string::npos,"team task finalization retains host qualification");
    call(s,"doc_apply",json(R"({"expected_revision":2,"commands":[{"op":"feature.edit","id":3,"p":{"dx":17}}]})"));
    check(!s.bomCompletionCheck(true,true).find("allowed")->b,"further edits invalidate task review");
    call(s,"bom_inspect",json(R"({"ids":[2]})"));review.set("expected_revision",V::makeNum(3));
    check(s.call("bom_review",review).find("isError")->b,"review cannot omit another changed definition or narrow coverage with ids");
    review.set("ids",json("[2,3]"));call(s,"bom_inspect",json(R"({"ids":[2,3]})"));call(s,"bom_review",review);
    check(s.bomCompletionCheck(true,true).find("allowed")->b,"all affected definitions allow limited completion");
    s.beginBomTask();check(!s.bomCompletionCheck(true,true).find("allowed")->b,"new host task cannot reuse a prior task receipt at the same revision");
    auto preview=call(s,"doc_preview",json(R"({"expected_revision":3,"commands":[{"op":"feature.edit","id":2,"p":{"dy":19}}]})"));V publish=V::makeObj();publish.set("candidate_id",*preview.find("candidate_id"));publish.set("expected_revision",V::makeNum(3));call(s,"doc_apply",publish);
    check(s.bomTaskScope().find("affected_feature_ids")->dump()=="[2]","prepared publication is included in host task coverage");
    call(s,"doc_apply",json(R"({"expected_revision":4,"commands":[{"op":"feature.remove","id":3}]})"));call(s,"bom_inspect",json(R"({"ids":[2]})"));review.set("ids",json("[2]"));review.set("expected_revision",V::makeNum(5));
    check(s.call("bom_review",review).find("isError")->b&&s.bomTaskScope().find("removed_feature_ids")->dump()=="[3]","removed definitions conservatively require a global review");
    s.resetBomReview();check(!s.bomTaskScope().find("active")->b,"document reset clears task scope");
    const auto tools=AgentSession::tools();bool advertised=false;for(const auto& tool:tools.arr)if(tool.find("name")->str=="bom_review")advertised=tool.find("inputSchema")->find("properties")->find("completion")!=nullptr;check(advertised,"task completion argument is discoverable");
    AgentSession dependency;call(dependency,"doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"cable_width","expr":10},{"op":"feature.add","type":"box","name":"Dependent envelope","p":{"dx":"cable_width"},"part":{"part_number":"TEST-DEPENDENT","description":"Synthetic dependency"}},{"op":"feature.add","type":"box","name":"Unrelated unidentified part"}]})"));dependency.beginBomTask();
    call(dependency,"doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"cable_width","expr":15}]})"));check(dependency.bomTaskScope().find("affected_feature_ids")->dump()=="[1]","parameter changes identify evaluated dependants without inventing unrelated identity work");
}
int main(){try{
    missingInputIsNotCompletion();
    recoveryMakesProgress();
    limitedTaskCompletion();
    AgentSession s;check(s.bomCompletionCheck().find("allowed")->b,"empty drawing has no fabricated BOM obligation");
    {AgentSession conceptSession;V args=json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"Concept body"}]})");args.set("actor",json(R"({"kind":"agent","id":"engineer"})"));call(conceptSession,"doc_apply",args);
     check(conceptSession.bomCompletionCheck().find("allowed")->b&&conceptSession.bomCompletionCheck(true).find("allowed")->b,"agent concept geometry without part metadata or BOM identity completes without a BOM review");}
    call(s,"doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"First","part":{"part_number":"TEST-1","description":"Fixture one","procurement":"make"}},{"op":"feature.add","type":"box","name":"Second","part":{"part_number":"TEST-2","description":"Fixture two","procurement":"buy"}}]})"));
    auto review=json(R"({"expected_revision":1,"scope":"concept","pending_notes":"Material and vendor selection unresolved; not ready for purchasing","fastening_review":"Connections remain to be designed; no fasteners claimed"})");
    check(!s.bomCompletionCheck().find("allowed")->b&&s.call("bom_review",review).find("isError")->b,"review requires current inspection");
    call(s,"bom_inspect",json(R"({"limit":1})"));check(s.call("bom_review",review).find("isError")->b,"first page cannot attest whole BOM");call(s,"bom_inspect",json(R"({"offset":1,"limit":1})"));auto detailed=review;detailed.set("scope",V::makeStr("detailed"));check(s.call("bom_review",detailed).find("isError")->b,"detailed scope rejects incomplete metadata");
    const auto authored=documentToJson(s.document);call(s,"bom_review",review);auto gate=s.bomCompletionCheck();check(gate.find("allowed")->b&&gate.find("summary")->str.find("INCOMPLETE")!=std::string::npos&&documentToJson(s.document)==authored&&s.revision==1,"concept qualification is explicit and nonmutating");
    call(s,"doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"changed","expr":2}]})"));check(!s.bomCompletionCheck().find("allowed")->b&&s.call("bom_review",review).find("isError")->b,"new revision invalidates review and rejects stale submission");
    AssistantConfig config;config.model="offline-bom";AssistantRetryHooks hooks;hooks.completionCheck=[&]{return s.bomCompletionCheck();};int round=0;std::string visible;
    auto run=runAssistant(config,V::makeStr("Finish the concept and disclose pending sourcing"),AgentSession::tools(),[&](const std::string& name,const V& args){return s.call(name,args);},[&](const AssistantEvent& event){if(event.kind==AssistantEvent::Text)visible+=event.text;},nullptr,[&](auto&,auto&,auto*){switch(++round){case 1:return done();case 2:check(visible.empty(),"unreviewed completion text never reaches UI");return response("bom_inspect","inspect",V::makeObj());case 3:review.set("expected_revision",V::makeNum(2));return response("bom_review","review",review);default:return done();}},{},{},hooks);
    check(round==4&&run.text.find("INCOMPLETE")!=std::string::npos&&visible.find("INCOMPLETE")!=std::string::npos,"single assistant finalization appends host qualification");
    call(s,"doc_apply",json(R"({"expected_revision":2,"commands":[{"op":"param.set","name":"changed","expr":3}]})"));AgentTeam team;AgentTeamConfig teamConfig;teamConfig.completionCheck=[&]{return s.bomCompletionCheck();};AgentTeamRole lead;lead.id="engineer";lead.assistant=config;lead.canWrite=true;lead.discipline=AgentTeamDiscipline::Orchestrator;round=0;
    lead.transport=[&](auto&,const V& request,auto*){switch(++round){case 1:return response("team_finish_goal","early",json(R"({"state":"completed","summary":"Finished"})"));case 2:check(request.dump().find("BOM completion blocked")!=std::string::npos&&request.dump().find("bom_completion")!=std::string::npos&&request.dump().find("uninspected_count")!=std::string::npos,"team gets structured completion rejection and actionable inspection evidence");return response("bom_inspect","team_inspect",V::makeObj());case 3:review.set("expected_revision",V::makeNum(3));return response("bom_review","team_review",review);case 4:return response("team_finish_goal","finish",json(R"({"state":"completed","summary":"Concept finished; commercial data pending"})"));default:return done();}};
    teamConfig.roles.push_back(lead);team.start(teamConfig,"Complete concept",AgentSession::tools(),[&](const std::string& name,const V& args){return s.call(name,args);});team.waitIdle();auto snapshot=team.snapshot();check(snapshot.goalState==AgentGoalState::Completed&&snapshot.goalSummary.find("INCOMPLETE")!=std::string::npos,"team completion qualified by host BOM review");
    call(s,"doc_apply",json(R"({"expected_revision":3,"commands":[{"op":"material.set","name":"Fixture material","density":1},{"op":"feature.edit","id":1,"material":"Fixture material","part":{"part_number":"TEST-1","description":"Fixture one","procurement":"buy","representation":"exact","source_url":"https://example.invalid/1","supplier":"Synthetic test supplier","supplier_part_number":"S1","sourcing_status":"selected"}},{"op":"feature.edit","id":2,"material":"Fixture material","part":{"part_number":"TEST-2","description":"Fixture two","procurement":"buy","representation":"exact","source_url":"https://example.invalid/2","supplier":"Synthetic test supplier","supplier_part_number":"S2","sourcing_status":"selected"}}]})"));call(s,"bom_inspect",V::makeObj());review.set("expected_revision",V::makeNum(4));review.set("scope",V::makeStr("detailed"));call(s,"bom_review",review);check(s.bomCompletionCheck().find("allowed")->b&&s.bomCompletionCheck().find("metadata_complete")->b,"detailed review accepts complete declared fixture metadata");
    s.resetBomReview();check(s.bomCompletionCheck(true).find("allowed")->b&&!s.bomCompletionCheck().find("allowed")->b,"opening a project does not manufacture agent authorship; goal completion still needs review");
    std::cout<<"BOM completion: paginated inspection, stale and detailed rejection, concept qualification, single and team gates passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
