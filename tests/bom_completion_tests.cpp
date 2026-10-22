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
    review.set("expected_revision",V::makeNum(2));auto ordinary=review;ordinary.set("completion",V::makeStr("project"));
    call(s,"bom_review",ordinary);check(!s.bomCompletionCheck(true,true).find("allowed")->b,"ordinary selected review still cannot finalize a task");
    auto detailed=review;detailed.set("scope",V::makeStr("detailed"));check(s.call("bom_review",detailed).find("isError")->b,"task scope does not weaken detailed metadata requirements");
    const auto authored=documentToJson(s.document);call(s,"bom_review",review);auto task=s.bomCompletionCheck(true,true);
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
    lead.transport=[&](auto&,const V& request,auto*){switch(++round){case 1:return response("team_finish_goal","early",json(R"({"state":"completed","summary":"Finished"})"));case 2:check(request.dump().find("BOM completion blocked")!=std::string::npos,"team gets actual completion rejection");return response("bom_inspect","team_inspect",V::makeObj());case 3:review.set("expected_revision",V::makeNum(3));return response("bom_review","team_review",review);case 4:return response("team_finish_goal","finish",json(R"({"state":"completed","summary":"Concept finished; commercial data pending"})"));default:return done();}};
    teamConfig.roles.push_back(lead);team.start(teamConfig,"Complete concept",AgentSession::tools(),[&](const std::string& name,const V& args){return s.call(name,args);});team.waitIdle();auto snapshot=team.snapshot();check(snapshot.goalState==AgentGoalState::Completed&&snapshot.goalSummary.find("INCOMPLETE")!=std::string::npos,"team completion qualified by host BOM review");
    call(s,"doc_apply",json(R"({"expected_revision":3,"commands":[{"op":"material.set","name":"Fixture material","density":1},{"op":"feature.edit","id":1,"material":"Fixture material","part":{"part_number":"TEST-1","description":"Fixture one","procurement":"buy","representation":"exact","source_url":"https://example.invalid/1","supplier":"Synthetic test supplier","supplier_part_number":"S1","sourcing_status":"selected"}},{"op":"feature.edit","id":2,"material":"Fixture material","part":{"part_number":"TEST-2","description":"Fixture two","procurement":"buy","representation":"exact","source_url":"https://example.invalid/2","supplier":"Synthetic test supplier","supplier_part_number":"S2","sourcing_status":"selected"}}]})"));call(s,"bom_inspect",V::makeObj());review.set("expected_revision",V::makeNum(4));review.set("scope",V::makeStr("detailed"));call(s,"bom_review",review);check(s.bomCompletionCheck().find("allowed")->b&&s.bomCompletionCheck().find("metadata_complete")->b,"detailed review accepts complete declared fixture metadata");
    s.resetBomReview();check(s.bomCompletionCheck(true).find("allowed")->b&&!s.bomCompletionCheck().find("allowed")->b,"opening a project does not manufacture agent authorship; goal completion still needs review");
    std::cout<<"BOM completion: paginated inspection, stale and detailed rejection, concept qualification, single and team gates passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
