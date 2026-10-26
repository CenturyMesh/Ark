#include "ark/agent.hpp"
#include "ark/serialize.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace ark;
using V=arkjson::Value;
static V parse(const char* s){V v;if(!arkjson::parse(s,v))throw std::runtime_error("fixture JSON");return v;}
static void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static V apply(AgentSession& s,V commands){V a=V::makeObj();a.set("commands",commands);a.set("expected_revision",V::makeNum(s.revision));a.set("detail",V::makeStr("changes"));auto r=s.call("doc_apply",a);if(r.find("isError")->b)throw std::runtime_error(r.dump());return *r.find("structuredContent");}
int main(int argc,char** argv){try{
    if(argc==3&&std::string(argv[1])=="--profile-document"){
        AgentSession session;session.suppressFailureCapture();V args=V::makeObj();args.set("path",V::makeStr(argv[2]));auto loaded=session.call("doc_load",args);check(!loaded.find("isError")->b,"Profile source failed to load");
        V report=V::makeObj();auto stage=[&](const char* name,auto operation){auto start=std::chrono::steady_clock::now();operation();report.set(name,V::makeNum(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()));};
        AssemblyManipulationProgram program;std::string error;stage("build_program_ms",[&]{check(buildAssemblyManipulation(session.document,program,error),error.c_str());});
        const auto joint=std::find_if(session.document.mates.begin(),session.document.mates.end(),[](const AssemblyMate& m){return m.type=="revolute";});check(joint!=session.document.mates.end(),"Profile requires a revolute mate");const u64 driver=joint->b,mate=joint->id;
        AssemblyManipulationPreview preview;stage("preview_ms",[&]{check(previewAssemblyManipulation(program,driver,mate,"rotate",.25,preview,error),error.c_str());});
        stage("cached_recompute_ms",[&]{session.document.recompute();});report.set("evaluated_features",V::makeNum(session.document.recomputeEvaluated));
        std::vector<AssemblyPlacement> placements;stage("resolve_ms",[&]{check(resolveAssembly(session.document,placements,error),error.c_str());});
        V command=V::makeObj(),commands=V::makeArr();command.set("op",V::makeStr("assembly.drive"));command.set("driver",V::makeNum(driver));command.set("mate",V::makeNum(mate));command.set("mode",V::makeStr("rotate"));command.set("delta",V::makeNum(.25));commands.arr.push_back(command);V result;
        stage("commit_with_history_ms",[&]{result=apply(session,commands);});report.set("geometry_recomputed",*result.find("geometry_recomputed"));
        session.suppressIterationHistory();stage("commit_without_history_ms",[&]{apply(session,commands);});
        std::cout<<report.dump()<<'\n';return 0;
    }
    const bool benchmark=argc>1&&std::string(argv[1])=="--benchmark";
    AgentSession s;s.suppressFailureCapture();s.suppressIterationHistory();
    apply(s,parse(R"([
      {"op":"plane.set","name":"Stock plane","p":{"oz":0}},
      {"op":"sketch.set","name":"Stock profile","sketch":{"plane":"Stock plane","entities":[{"kind":"circle","dof":[0,0,40]},{"kind":"circle","dof":[0,0,8]}]}},
      {"op":"feature.add","type":"native_solid","name":"Sketch driven ring","native":{"op":"extrude"},"p":{"sketch":"Stock profile","depth":8}},
      {"op":"feature.add","type":"native_solid","name":"Fixed axle","native":{"op":"cylinder"},"p":{"radius":7,"height":30}},
      {"op":"assembly.add","name":"Frame","feature":2,"fixed":true},
      {"op":"assembly.add","name":"Rotor","feature":1},
      {"op":"mate.add","a":"Frame","b":"Rotor","type":"revolute","reference_a":"Z","reference_b":"Z"}
    ])"));
    auto original=s.document.evaluationCache.at(1).body;
    std::vector<double> times;V samples=V::makeArr();size_t evaluated=0;
    for(int i=0;i<12;++i){auto begin=std::chrono::steady_clock::now();apply(s,parse(R"([{"op":"assembly.drive","driver":"Rotor","mode":"rotate","delta":7}])"));double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();if(i>=2){times.push_back(ms);samples.arr.push_back(V::makeNum(ms));evaluated+=s.document.recomputeEvaluated;}}
    const bool reused=s.document.evaluationCache.at(1).body==original;
    std::sort(times.begin(),times.end());V report=V::makeObj();report.set("fixture",V::makeStr("named-sketch-ring-revolute-v1"));report.set("samples_ms",samples);report.set("median_ms",V::makeNum(times[times.size()/2]));report.set("p95_ms",V::makeNum(times.back()));report.set("evaluated_features",V::makeNum(evaluated));report.set("retained_ring_geometry",V::makeBool(reused));std::cout<<report.dump()<<'\n';
    if(!benchmark)check(reused&&evaluated==0,"Rigid mate motion regenerated sketch-based geometry");
    const auto before=documentToJson(s.document);const auto revision=s.revision;
    auto bad=parse(R"({"expected_revision":0,"commands":[{"op":"assembly.drive","driver":"Rotor","delta":7}]})");check(s.call("doc_apply",bad).find("isError")->b&&revision==s.revision&&before==documentToJson(s.document),"Stale movement changed the document");
    V recipe=parse(R"({"commands":[{"op":"assembly.drive","driver":"Rotor","delta":7}],"detail":"changes"})");recipe.set("expected_revision",V::makeNum(s.revision));
    auto prepared=s.call("doc_preview",recipe);check(!prepared.find("isError")->b&&prepared.find("structuredContent")->find("valid")->b,"Rigid native preview failed");
    check(documentToJson(s.document)==before&&s.revision==revision,"Rigid preview changed the live document");
    V commit=V::makeObj();commit.set("expected_revision",V::makeNum(s.revision));commit.set("candidate_id",*prepared.find("structuredContent")->find("candidate_id"));commit.set("detail",V::makeStr("changes"));
    auto committed=s.call("doc_apply",commit);check(!committed.find("isError")->b,"Rigid prepared publication failed");const auto& receipt=*committed.find("structuredContent");
    check(receipt.find("placement_changed")&&receipt.find("placement_changed")->b&&!receipt.find("geometry_changed")->b&&s.document.evaluationCache.at(1).body==original,"Prepared movement lost geometry custody or placement receipt");
    auto old=s.document.evaluationCache.at(1).body;
    apply(s,parse(R"([{"op":"plane.set","name":"Stock plane","p":{"oz":3}}])"));check(s.document.evaluationCache.at(1).body!=old,"Datum edit reused stale solid");
    AgentSession dependent;dependent.suppressFailureCapture();dependent.suppressIterationHistory();
    apply(dependent,parse(R"([
      {"op":"feature.add","type":"native_solid","name":"Stock","native":{"op":"box"},"p":{"width":10,"depth":10,"height":10}},
      {"op":"assembly.add","name":"A","feature":1,"fixed":true},
      {"op":"assembly.add","name":"B","feature":1,"p":{"tx":5}},
      {"op":"feature.add","type":"native_solid","name":"Placed union","native":{"op":"union","instances":[1,2]}}
    ])"));
    auto unionBefore=dependent.document.evaluationCache.at(2).body;
    apply(dependent,parse(R"([{"op":"assembly.edit","id":2,"p":{"tx":8}}])"));
    check(dependent.document.evaluationCache.at(2).body!=unionBefore,"Placed-instance union retained stale geometry");
    check(std::abs(dependent.document.evalBodies.back().bbox().mx.x-18)<1e-6,"Placed-instance union has stale bounds");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
