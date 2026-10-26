#include "ark/agent_team.hpp"
#include "ark/agent.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
using V=arkjson::Value;
static int checks=0;
static void check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
static V json(const std::string& text){V v;check(arkjson::parse(text,v),"valid fixture JSON");return v;}
static const V& data(const V& v){const auto* p=v.find("structuredContent");return p?*p:v;}
static bool failed(const V& v){const auto* e=v.find("isError");return e&&e->b;}
static V call(const std::string& args){static int serial=0;V out=json(R"({"status":"completed","output":[]})"),item=json(R"({"type":"function_call","name":"team_part"})");item.set("call_id",V::makeStr("part-call-"+std::to_string(++serial)));item.set("arguments",V::makeStr(args));V output=V::makeArr();output.arr.push_back(item);out.set("output",output);return out;}
static V done(){return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Checkpoint complete."}]}]})");}
static V last(const V& request){const auto& input=*request.find("input");for(auto it=input.arr.rbegin();it!=input.arr.rend();++it)if(const auto* out=it->find("output");out&&out->t==V::Str)return json(out->str);throw std::runtime_error("missing tool receipt");}
static ark::AgentTeamConfig config(){ark::AgentTeamConfig c;c.focusedWorkflow=true;c.isolatePartDesigners=true;c.orchestratedStart=true;for(const auto& id:{"engineer","designer"}){ark::AgentTeamRole r;r.id=id;r.label=id;r.canWrite=true;r.discipline=r.id=="engineer"?ark::AgentTeamDiscipline::Orchestrator:ark::AgentTeamDiscipline::PartDesigner;r.assistant.model="offline-part-test";r.assistant.apiKeyEnv="";c.roles.push_back(r);}return c;}
static void fixture(ark::AgentSession& source){source.suppressFailureCapture();check(!failed(source.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"SharedWidth","expr":12},{"op":"feature.add","type":"box","name":"Part","p":{"dx":"SharedWidth","dy":6,"dz":7}},{"op":"feature.add","type":"box","name":"SECRET_OUTSIDE","p":{"dx":"SharedWidth","dy":3,"dz":4}},{"op":"assembly.add","name":"PartGroup","feature":0},{"op":"assembly.add","name":"PartOccurrence","feature":1,"parent":1},{"op":"assembly.add","name":"OutsideOccurrence","feature":2}]})"))),"source baseline accepted");}
static void isolatedWorkflow(bool stale,bool failingCheck,bool withChecks=true,bool wrongDigest=false){
    ark::AgentSession source;fixture(source);auto c=config();c.roles[1].assistant.memoryPrefetch=true;c.roles[1].assistant.onboardingPack="GLOBAL_PRELOADED_CONTEXT";int lead=0,worker=0;bool rejectedDependency=false,contextIsolated=false;std::string reviewed;std::size_t workerRequest=0;
    c.roles[0].transport=[&](const auto&,const V& request,const auto*){
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
        case 4:check(failed(last(request)),"global memory is unreachable");return call(R"({"action":"apply","expected_revision":0,"commands":[{"op":"feature.edit","id":1,"p":{"dy":9}}]})");
        case 5:check(!failed(last(request))&&source.revision==1,"private edit accepted with source untouched");return call(R"({"action":"submit","expected_revision":1,"message":"Part dy is 9; SharedWidth expression retained. Ready for native source acceptance checks."})");
        case 6:check(!failed(last(request)),"private result submitted");return call(R"({"action":"apply","expected_revision":1,"commands":[{"op":"feature.edit","id":1,"p":{"dy":11}}]})");
        case 7:check(failed(last(request)),"submitted child cannot change after handoff");return done();
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
int main(){try{isolatedWorkflow(false,false);isolatedWorkflow(true,false);isolatedWorkflow(false,true);isolatedWorkflow(false,false,false);isolatedWorkflow(false,false,true,true);std::cout<<"052D part agent checks passed: "<<checks<<'\n';return 0;}catch(const std::exception& e){std::cerr<<"052D part agent failure: "<<e.what()<<'\n';return 1;}}
