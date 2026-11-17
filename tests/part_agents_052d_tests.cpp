#include "ark/agent_team.hpp"
#include "ark/agent.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <set>
using V=arkjson::Value;
static int checks=0;
static void check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
static V json(const std::string& text){V v;check(arkjson::parse(text,v),"valid fixture JSON");return v;}
static const V& data(const V& v){const auto* p=v.find("structuredContent");return p?*p:v;}
static bool failed(const V& v){const auto* e=v.find("isError");return e&&e->b;}
static V namedCall(const std::string& name,const std::string& args){static int serial=0;V out=json(R"({"status":"completed","output":[]})"),item=json(R"({"type":"function_call"})");item.set("name",V::makeStr(name));item.set("call_id",V::makeStr("part-call-"+std::to_string(++serial)));item.set("arguments",V::makeStr(args));V output=V::makeArr();output.arr.push_back(item);out.set("output",output);return out;}
static V call(const std::string& args){return namedCall("team_part",args);}
static V done(){return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Checkpoint complete."}]}]})");}
static V last(const V& request){const auto& input=*request.find("input");for(auto it=input.arr.rbegin();it!=input.arr.rend();++it)if(const auto* out=it->find("output");out&&out->t==V::Str)return json(out->str);throw std::runtime_error("missing tool receipt");}
static ark::AgentTeamConfig config(){ark::AgentTeamConfig c;c.focusedWorkflow=true;c.isolatePartDesigners=true;c.orchestratedStart=true;for(const auto& id:{"engineer","designer"}){ark::AgentTeamRole r;r.id=id;r.label=id;r.canWrite=true;r.discipline=r.id=="engineer"?ark::AgentTeamDiscipline::Orchestrator:ark::AgentTeamDiscipline::PartDesigner;r.assistant.model="offline-part-test";r.assistant.apiKeyEnv="";c.roles.push_back(r);}return c;}
static void catalogue(const V& request,bool isolated,bool handles=false){
 const auto* tools=request.find("tools");check(tools&&tools->arr.size()==128,"Complete lead catalogue must stay exactly within128 provider functions");std::set<std::string> names;bool proposalRead=false,releaseAlias=false;for(const auto& tool:tools->arr){check(names.insert(tool.find("name")->str).second,"Lead catalogue contains a duplicate tool");if(tool.find("name")->str=="team_context")proposalRead=tool.find("parameters")->find("properties")->find("proposal_id");if(tool.find("name")->str=="team_reserve_write")releaseAlias=tool.find("parameters")->find("properties")->find("release");}
 const std::set<std::string> hostOnly{"usage_record","compute_benchmark","history_compact","history_migrate_derived","memory_maintain","failure_export"};for(const auto& tool:ark::AgentSession::tools().arr)if(!hostOnly.count(tool.find("name")->str))check(names.count(tool.find("name")->str)==1,"Catalogue capacity silently removed a CAD capability");
 for(const char* name:{"team_send","team_report_task","team_finish_goal","team_context","team_reserve_write","team_propose","team_apply_step","geometry_graph"})check(names.count(name)==1,"Required coordination/geometry capability missing");check(proposalRead==(isolated||handles)&&releaseAlias==(isolated&&handles)&&names.count("team_part")==size_t(isolated)&&names.count("team_proposal")==size_t(!(isolated||handles))&&names.count("team_release_write")==size_t(!(isolated&&handles))&&names.count("result_get")==size_t(handles),"Capacity-aware aliases/helper slots are not explicit in the catalogue");
}
static void fixture(ark::AgentSession& source){source.suppressFailureCapture();check(!failed(source.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"SharedWidth","expr":12},{"op":"feature.add","type":"box","name":"Part","p":{"dx":"SharedWidth","dy":6,"dz":7}},{"op":"feature.add","type":"box","name":"SECRET_OUTSIDE","p":{"dx":"SharedWidth","dy":3,"dz":4}},{"op":"assembly.add","name":"PartGroup","feature":0},{"op":"assembly.add","name":"PartOccurrence","feature":1,"parent":1},{"op":"assembly.add","name":"OutsideOccurrence","feature":2}]})"))),"source baseline accepted");}
static void isolatedWorkflow(bool stale,bool failingCheck,bool withChecks=true,bool wrongDigest=false){
    ark::AgentSession source;fixture(source);auto c=config();c.roles[1].assistant.memoryPrefetch=true;c.roles[1].assistant.onboardingPack="GLOBAL_PRELOADED_CONTEXT";int lead=0,worker=0;bool rejectedDependency=false,contextIsolated=false;std::string reviewed;std::size_t workerRequest=0;
    c.roles[0].transport=[&](const auto&,const V& request,const auto*){
        catalogue(request,true);
        switch(++lead){
        case 1:{V assignment=json(std::string(R"({"action":"assign","to":"designer","node":1,"expected_revision":1,"message":"Detail Part: change dy to 9, preserve SharedWidth expression and the shared interface.","checks":[{"query":{"tool":"eval_expr","args":{"expr":"SharedWidth"},"path":"value"},"op":"eq","value":)")+(failingCheck?"999":"12")+"}]}");if(!withChecks)assignment.obj.erase(std::remove_if(assignment.obj.begin(),assignment.obj.end(),[](const auto& p){return p.first=="checks";}),assignment.obj.end());return call(assignment.dump());}
        case 2:check(!failed(last(request)),"assignment succeeds");return done();
        case 3:check(source.revision==1,"private edits never published source");return call(R"({"action":"review","task":"part-1","expected_revision":1})");
        case 4:{const auto receipt=last(request);if(failingCheck){check(failed(receipt)||(data(receipt).find("valid")&&!data(receipt).find("valid")->b),"immutable source acceptance check rejects review");return call(R"({"action":"discard","task":"part-1"})");}
            check(!failed(receipt)&&receipt.find("reviewed_child_sha256"),"lead receives validated child digest");reviewed=receipt.find("reviewed_child_sha256")->str;
            if(stale)check(!failed(source.call("doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"UnrelatedChange","expr":3}]})"))),"outside manual edit accepted");
            return call("{\"action\":\"integrate\",\"task\":\"part-1\",\"expected_revision\":1,\"reviewed_child_sha256\":\""+(wrongDigest?"incorrect-digest":reviewed)+"\"}");}
        case 5:if(stale||wrongDigest){check(failed(last(request)),"stale source or wrong reviewed digest rejects integration");return call(R"({"action":"discard","task":"part-1"})");}if(!failingCheck)check(!failed(last(request)),"reviewed child integrates");return done();
        default:return done();}
    };
    c.roles[1].transport=[&](const auto&,const V& request,const auto*){
        const auto input=request.find("input")->dump();check(request.dump().find("GLOBAL_PRELOADED_CONTEXT")==std::string::npos&&input.find("GLOBAL_SECRET_BRIEF")==std::string::npos&&input.find("SECRET_OUTSIDE")==std::string::npos,"worker never receives global brief, preloaded context or sibling authoring");
        check(request.find("tools")->arr.size()==1&&request.find("tools")->arr[0].find("name")->str=="team_part","private capability is the only advertised tool");
        switch(++worker){
        case 1:workerRequest=request.dump().size();return call(R"({"action":"query","tool":"doc_info","args":{"detail":"design"}})");
        case 2:contextIsolated=!failed(last(request));return call(R"({"action":"apply","expected_revision":0,"commands":[{"op":"param.set","name":"SharedWidth","expr":99}]})");
        case 3:rejectedDependency=failed(last(request));return call(R"({"action":"query","tool":"memory_context","args":{}})");
        case 4:check(failed(last(request)),"global memory is unreachable");return call(R"({"action":"query","tool":"team_context","args":{"proposal_id":"proposal-1"}})");
        case 5:check(failed(last(request)),"private specialist cannot read global team proposals through the context alias");return call(R"({"action":"apply","expected_revision":0,"commands":[{"op":"feature.edit","id":1,"p":{"dy":9}}]})");
        case 6:check(!failed(last(request))&&source.revision==1,"private edit accepted with source untouched");return call(R"({"action":"submit","expected_revision":1,"message":"Part dy is 9; SharedWidth expression retained. Ready for native source acceptance checks."})");
        case 7:check(!failed(last(request)),"private result submitted");return call(R"({"action":"apply","expected_revision":1,"commands":[{"op":"feature.edit","id":1,"p":{"dy":11}}]})");
        case 8:check(failed(last(request)),"submitted child cannot change after handoff");return done();
        default:throw std::runtime_error("unexpected specialist turn");}
    };
    ark::AgentTeam team;team.start(c,"GLOBAL_SECRET_BRIEF: detailed product story belongs to the lead.",ark::AgentSession::tools(),[&](const auto& name,const V& args){check(ark::AgentTeam::currentRoleId()=="engineer","specialist cannot invoke parent executor");return source.call(name,args);});
    check(team.waitIdleFor(std::chrono::seconds(15)),"workflow finishes within test watchdog");const auto snapshot=team.snapshot(true);
    for(const auto& role:snapshot.roles){if(!role.error.empty())std::cerr<<role.id<<": "<<role.error<<'\n';check(role.error.empty(),"offline provider completed without hidden assertion failure");}
    check(rejectedDependency&&contextIsolated,"read-only dependency and private context proven");
    check(snapshot.roles[1].partTask=="part-1"&&snapshot.roles[1].partState==((stale||failingCheck||wrongDigest)?"discarded":"integrated")&&snapshot.roles[1].privateStepsApplied==1,"snapshot distinguishes actual private work from integrated/discarded source outcomes");
    check(source.document.getParam("SharedWidth")==12,"shared expression input preserved");
    const std::string expected=(stale||failingCheck||wrongDigest)?"6":"9";check(source.document.features[0].p.at("dy")==expected,"only reviewed accepted detail affects source");
    check(source.document.features[0].p.at("dx")=="SharedWidth","parametric relationship remains authored");
    std::string error;ark::Document reopened;check(ark::documentFromJson(ark::documentToJson(source.document),reopened,&error),"integrated document persists");reopened.recompute();check(reopened.features[0].p.at("dy")==expected,"reopened authoring rebuild agrees");
    std::cout<<"PART_CONTEXT_EVIDENCE worker_first_request_bytes="<<workerRequest<<" worker_requests="<<snapshot.roles[1].performance.providerRequests<<" stale="<<stale<<" failed_check="<<failingCheck<<"\n";
}
static V proposalReadAlias(bool isolated,bool handles){
 ark::AgentSession source;source.suppressFailureCapture();source.suppressIterationHistory();const auto before=ark::documentToJson(source.document,false,false);auto c=config();c.roles.resize(1);c.isolatePartDesigners=isolated;c.roles[0].assistant.resultHandleBytes=handles?1024*1024:0;const bool readAlias=isolated||handles,releaseAlias=isolated&&handles;int rounds=0;V exact;std::string id,token;
 c.roles[0].transport=[&](const auto&,const V& request,const auto*){catalogue(request,isolated,handles);switch(++rounds){
  case 1:return namedCall("doc_info",R"({"detail":"revision"})");
  case 2:check(!failed(last(request)),"Proposal source revision read failed");return namedCall("team_propose",R"({"to":"engineer","expected_revision":0,"commands":[{"op":"param.set","name":"ProposalValue","expr":7}],"reason":"Synthetic read alias parity","evidence":"source revision0; uncommitted proposal"})");
  case 3:{const auto proposal=last(request);check(!failed(proposal)&&data(proposal).find("stored")->b,"Proposal was not stored");id=data(proposal).find("proposal_id")->str;return namedCall(readAlias?"team_context":"team_proposal",std::string("{\"")+(readAlias?"proposal_id":"id")+"\":\""+id+"\"}");}
  case 4:{const auto read=last(request);check(!failed(read),"Exact proposal context lookup failed");exact=data(read);check(exact.find("id")->str==id&&exact.find("from")->str=="engineer"&&exact.find("to")->str=="engineer"&&exact.find("expected_revision")->num==0&&exact.find("commands")->arr[0].find("name")->str=="ProposalValue","Context alias lost proposal author/recipient/commands/original revision");return namedCall(readAlias?"team_context":"team_proposal",readAlias?R"({"proposal_id":"foreign-proposal"})":R"({"id":"foreign-proposal"})");}
  case 5:check(failed(last(request)),"Unknown/foreign proposal escaped the current team store");return namedCall("doc_info",R"({"detail":"revision"})");
  case 6:check(!failed(last(request)),"Reservation inspection failed");return namedCall("team_reserve_write",R"({"expected_revision":0})");
  case 7:{const auto receipt=last(request);check(!failed(receipt)&&data(receipt).find("reserved")->b,"Owner reservation failed");token=data(receipt).find("reservation_id")->str;return namedCall(releaseAlias?"team_reserve_write":"team_release_write",std::string("{")+(releaseAlias?"\"release\":true,":"")+"\"reservation_id\":\""+token+"\"}");}
  case 8:check(!failed(last(request))&&data(last(request)).find("released")->b,"Owner release alias failed");return namedCall(releaseAlias?"team_reserve_write":"team_release_write",std::string("{")+(releaseAlias?"\"release\":true,":"")+"\"reservation_id\":\""+token+"\"}");
  case 9:check(!failed(last(request))&&data(last(request)).find("already_ended")->b,"Release alias lost idempotent owner receipt");return done();
  default:return done();}};
 ark::AgentTeam team;team.start(c,"Read a stored proposal; do not publish it.",ark::AgentSession::tools(),[&](const auto& name,const V& args){return source.call(name,args);});check(team.waitIdleFor(std::chrono::seconds(15)),"Proposal alias test did not finish");for(const auto& role:team.snapshot(true).roles)check(role.error.empty(),"Proposal alias provider failed");check(source.revision==0&&ark::documentToJson(source.document,false,false)==before,"Proposal lookup/release applied or rebased source");return exact;
}
int main(){try{auto oversized=ark::AgentSession::tools();for(int i=0;i<4;++i){V extra=oversized.arr.front();extra.set("name",V::makeStr("synthetic_extra_"+std::to_string(i)));oversized.arr.push_back(extra);}bool capRejected=false;try{ark::assistantFunctionTools(oversized);}catch(const std::exception&){capRejected=true;}check(capRejected,"Provider128-function contract was relaxed");const auto legacyProposal=proposalReadAlias(false,false);for(const auto mode:{std::pair<bool,bool>{true,false},{false,true},{true,true}})check(proposalReadAlias(mode.first,mode.second).dump()==legacyProposal.dump(),"Capacity-aware proposal alias changed exact legacy read content");isolatedWorkflow(false,false);isolatedWorkflow(true,false);isolatedWorkflow(false,true);isolatedWorkflow(false,false,false);isolatedWorkflow(false,false,true,true);std::cout<<"052D part agent checks passed: "<<checks<<'\n';return 0;}catch(const std::exception& e){std::cerr<<"052D part agent failure: "<<e.what()<<'\n';return 1;}}
