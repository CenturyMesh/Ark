#include "ark/agent.hpp"
#include "ark/evaluation_scheduler_070a.hpp"
#include "ark/mesh_model.hpp"
#include "ark/serialize.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ark;using V=arkjson::Value;
int checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
struct Environment {
    const char* name;std::string previous;
    Environment(const char* key,const char* value):name(key),previous(std::getenv(key)?std::getenv(key):""){set(value);}
    void set(const char* value){
#ifdef _WIN32
        _putenv_s(name,value);
#else
        if(*value)setenv(name,value,1);else unsetenv(name);
#endif
    }
    ~Environment(){set(previous.c_str());}
};
V json(const char* source){V value;if(!arkjson::parse(source,value))throw std::runtime_error("Invalid synthetic scheduler fixture");return value;}
V accepted(const V& response){if(response.find("isError")->b)throw std::runtime_error(response.dump());return *response.find("structuredContent");}
V apply(AgentSession& session,V commands){V args=V::makeObj();args.set("commands",std::move(commands));args.set("expected_revision",V::makeNum(session.revision));return accepted(session.call("doc_apply",args));}
void healthy(Document& document){document.recompute();for(const auto& result:document.lastResults)check(result.ok,result.error.c_str());}
Mesh quad(){Mesh mesh;for(const Vec3 point:std::initializer_list<Vec3>{{0,0,0},{2,0,0},{2,2,0},{0,2,0}})mesh.addVertex(point);mesh.idx={0,1,2,0,2,3};mesh.computeNormals();return mesh;}
void parity(const Document& serial,const Document& parallel){
    check(serial.lastResults.size()==parallel.lastResults.size(),"Serial/parallel result count differs");
    for(size_t i=0;i<serial.lastResults.size();++i){const auto& a=serial.lastResults[i];const auto& b=parallel.lastResults[i];check(a.id==b.id&&a.ok==b.ok&&a.error==b.error,"Scheduler changed result order or failure semantics");}
    check(serial.evalBodies.size()==parallel.evalBodies.size(),"Serial/parallel evaluated body count differs");
    for(size_t i=0;i<serial.evalBodies.size();++i){const auto& a=serial.evalBodies[i];const auto& b=parallel.evalBodies[i];check(a.id==b.id,"Scheduler changed body publication order");
        check(bool(a.baked)==bool(b.baked),"Scheduler changed mesh presence");if(a.baked)check(a.baked->pos==b.baked->pos&&a.baked->idx==b.baked->idx&&a.baked->nrm==b.baked->nrm,"Serial/parallel mesh buffers differ");
        if(const auto* report=a.nativeData.find("report")){const auto* other=b.nativeData.find("report");check(other&&report->dump()==other->dump(),"Serial/parallel exact or mesh report differs");}
        if(const auto* topology=a.nativeData.find("topology"))check(b.nativeData.find("topology")&&topology->dump()==b.nativeData.find("topology")->dump(),"Serial/parallel exact topology differs");
    }
}
Document chain(size_t count){Document doc;doc.setParam("leaf_delta","0.001");Feature source;source.type=FeatureType::MeshBody;source.name="Public mesh source";source.sourceData=meshToPayload(quad());doc.addFeature(source);for(size_t i=1;i<count;++i){Feature offset;offset.type=FeatureType::MeshOffset;offset.name="Offset "+std::to_string(i);offset.inputs={i};offset.p["distance"]=i+1==count?"leaf_delta":"0.001";doc.addFeature(offset);}return doc;}
void cachedFrontier(){
    Environment workers("ARK_FEATURE_THREADS","4");Document doc=chain(768);healthy(doc);
    check(doc.parallelFeatures==0,"A single dirty dependency chain should run directly without worker handoffs");
    const auto before=doc.evaluationCache;healthy(doc);const auto cached=evaluationSchedulerDiagnostics070A();
    check(doc.recomputeEvaluated==0&&doc.recomputeReused==768,"Cached chain regenerated geometry");
    check(cached.dirtyCandidates==0&&cached.readinessChecks==0&&cached.inputLookups==0&&cached.harvestChecks==0,"Cached history must not enter the scheduler frontier");
    for(const auto& item:before)check(doc.evaluationCache.at(item.first).body==item.second.body,"Cached chain lost immutable body custody");
    doc.setParam("leaf_delta","0.007");healthy(doc);const auto edited=evaluationSchedulerDiagnostics070A();
    check(doc.recomputeEvaluated==1&&doc.recomputeReused==767,"Leaf change regenerated unrelated ancestors");
    check(edited.resourcePolicy.eligibleTasks==1&&doc.featureWorkers==1&&edited.dirtyCandidates==0&&edited.readinessChecks==0&&edited.inputLookups==0,"A single dirty leaf should select owner execution without scheduler overhead");
    check(edited.peakRunningJobs<=doc.featureWorkers&&doc.peakParallelFeatures<=doc.featureWorkers,"Leaf task exceeded configured worker count");
    check(doc.parallelContextFeaturesCopied==0,"Pure mesh worker copied unrelated ancestor recipes");
    check(std::abs(doc.evalBodies.back().baked->pos[2]-(766*.001+.007))<1e-5,"Leaf expression change did not reach actual mesh coordinates");
    Document reference=doc.authoringCopy();Environment serialWorkers("ARK_FEATURE_THREADS","1");healthy(reference);parity(reference,doc);
}

Document branching(){
    Document doc;doc.setParam("offset","0.1");Feature source;source.type=FeatureType::MeshBody;source.sourceData=meshToPayload(quad());source.name="Root";doc.addFeature(source);
    for(int i=0;i<6;++i){Feature offset;offset.type=FeatureType::MeshOffset;offset.name="Branch "+std::to_string(i);offset.inputs={1};offset.p["distance"]="offset*"+std::to_string(i+1);doc.addFeature(offset);}
    Feature owner;owner.type=FeatureType::Transform;owner.inputs={2};owner.p["tx"]="3";owner.name="Owner-only legacy transform";doc.addFeature(owner);
    Feature crossing;crossing.type=FeatureType::MeshOffset;crossing.name="Audited continuation after owner row";crossing.inputs={3};crossing.p["distance"]="0.7";doc.addFeature(crossing);
    Feature dependent;dependent.type=FeatureType::MeshOffset;dependent.name="Owner result consumer";dependent.inputs={8};dependent.p["distance"]="0.5";doc.addFeature(dependent);
    return doc;
}
void meshDependencyAndFailures(){
    auto serial=branching(),parallel=serial.authoringCopy();Environment workers("ARK_FEATURE_THREADS","1");healthy(serial);workers.set("4");healthy(parallel);parity(serial,parallel);
    check(parallel.parallelFeatures>0&&parallel.peakParallelFeatures<=4,"Ready branches must retain bounded worker evaluation");
    check(parallel.parallelContextFeaturesCopied==0,"Mesh branches cloned Document feature context");
    auto stats=evaluationSchedulerDiagnostics070A();check(stats.readinessChecks<=parallel.features.size()+2&&stats.dependencyWakeups<=parallel.features.size()*2,"Dependency release unexpectedly rescans the whole DAG");
    for(Document* doc:{&serial,&parallel})doc->setParam("offset","0.25");workers.set("1");healthy(serial);workers.set("4");healthy(parallel);parity(serial,parallel);
    // Invalid exact input order remains an owner rejection, never work that can
    // be speculatively published by the dirty frontier.
    auto failedSerial=branching(),failedParallel=failedSerial.authoringCopy();failedSerial.features[1].inputs={10};failedParallel.features[1].inputs={10};
    workers.set("1");failedSerial.recompute();workers.set("4");failedParallel.recompute();parity(failedSerial,failedParallel);
    check(!failedParallel.lastResults[1].ok,"Forward dependency was accepted");
    auto suppressedSerial=branching(),suppressedParallel=suppressedSerial.authoringCopy();suppressedSerial.features.front().suppressed=true;suppressedParallel.features.front().suppressed=true;
    workers.set("1");suppressedSerial.recompute();workers.set("4");suppressedParallel.recompute();parity(suppressedSerial,suppressedParallel);
    check(suppressedParallel.evalBodies.empty(),"Suppressed source leaked speculative descendants");
    auto rolled=branching();rolled.historyTip=2;healthy(rolled);check(rolled.evalBodies.size()==2&&rolled.parallelFeatures==0,"History rollback scheduled inactive features");
}

#ifdef ARK_SCHEDULER_NATIVE_TESTS
V nativeFixture(){
    V commands=json(R"([{"op":"param.set","name":"height","expr":"20"},{"op":"param.set","name":"BranchWidth","expr":"10"}])");
    for(int i=0;i<6;++i){auto source=json(R"({"op":"feature.add","type":"native_solid","native":{"op":"box"},"p":{"width":10,"depth":12,"height":"height"}})");source.set("name",V::makeStr("Root "+std::to_string(i)));if(!i){auto p=*source.find("p");p.set("width",V::makeStr("BranchWidth"));source.set("p",p);}commands.arr.push_back(source);
        auto transform=json(R"({"op":"feature.add","type":"native_solid","native":{"op":"transform"},"p":{"tx":2}})");transform.set("name",V::makeStr("Moved "+std::to_string(i)));V inputs=V::makeArr();inputs.arr.push_back(V::makeNum(i*3+1));transform.set("inputs",inputs);commands.arr.push_back(transform);
        auto hole=json(R"({"op":"feature.add","type":"native_solid","native":{"op":"hole","cutter":{"kind":"circle","radius":1,"depth":24,"frame":[1,0,0,0,0,1,0,0,0,0,1,0,7,6,-1,1]}}})");hole.set("name",V::makeStr("Drilled "+std::to_string(i)));inputs.arr[0]=V::makeNum(i*3+2);hole.set("inputs",inputs);commands.arr.push_back(hole);
    }
    return commands;
}
void nativeParallelAndAtomicity(){
    AgentSession serial,parallel;for(AgentSession* session:{&serial,&parallel}){session->suppressIterationHistory();session->suppressFailureCapture();}
    Environment workers("ARK_FEATURE_THREADS","1");const auto commands=nativeFixture();apply(serial,commands);workers.set("4");apply(parallel,commands);parity(serial.document,parallel.document);
    check(parallel.document.peakParallelFeatures>1&&parallel.document.peakParallelFeatures<=4,"Independent exact branches must use bounded actual CPU concurrency");
    check(evaluationSchedulerDiagnostics070A().peakRunningJobs<=4,"Future count exceeds configured workers");
    for(int pass=0;pass<3;++pass){auto edit=json(R"([{"op":"param.set","name":"height","expr":"21"}])");edit.arr[0].set("expr",V::makeStr(std::to_string(21+pass)));workers.set("1");apply(serial,edit);workers.set("4");apply(parallel,edit);parity(serial.document,parallel.document);}
    const auto retained=parallel.document.evaluationCache;
    auto edit=json(R"([{"op":"param.set","name":"BranchWidth","expr":"11"}])");workers.set("1");apply(serial,edit);workers.set("4");apply(parallel,edit);parity(serial.document,parallel.document);
    check(parallel.document.recomputeEvaluated==3&&parallel.document.recomputeReused==15,"Single native branch edit lost incremental cache behavior");
    check(parallel.document.parallelFeatures==0,"A single dirty exact branch should avoid serial worker handoffs");
    check(evaluationSchedulerDiagnostics070A().resourcePolicy.eligibleTasks==3&&parallel.document.featureWorkers==1&&evaluationSchedulerDiagnostics070A().dirtyCandidates==0,"Small native branch should select owner execution and exclude cached branches");
    for(u64 id=4;id<=18;++id)check(retained.at(id).body==parallel.document.evaluationCache.at(id).body,"Unchanged exact branch lost cache custody");
    for(AgentSession* session:{&serial,&parallel}){
        workers.set(session==&serial?"1":"4");const auto before=documentToJson(session->document);const auto revision=session->revision;
        auto args=json(R"({"commands":[{"op":"param.set","name":"BranchWidth","expr":"-2"}]})");args.set("expected_revision",V::makeNum(revision));const auto failure=session->call("doc_apply",args);
        check(failure.find("isError")->b,"Invalid native source accepted");check(documentToJson(session->document)==before&&session->revision==revision,"Native failure published partial speculative work");
        args.set("expected_revision",V::makeNum(0));check(session->call("doc_apply",args).find("isError")->b&&documentToJson(session->document)==before,"Stale revision changed a parallel document");
    }
    Environment cpu("ARK_CPU_THREADS","2"),native("ARK_NATIVE_THREADS","8");workers.set("16");Document capped=parallel.document.authoringCopy();healthy(capped);
    check(capped.featureWorkers==2&&capped.nativeThreadsPerWorker==1&&capped.peakParallelFeatures<=2,"Requested workers/native threads exceeded configured CPU allowance");
    parity(parallel.document,capped);
}
#endif
}
int main(){try{Environment cpu("ARK_CPU_THREADS","4"),native("ARK_NATIVE_THREADS","1");cachedFrontier();meshDependencyAndFailures();
#ifdef ARK_SCHEDULER_NATIVE_TESTS
nativeParallelAndAtomicity();
#endif
std::cout<<"070A evaluation scheduler: "<<checks<<" frontier-work, serial/parallel geometry, failure, cache-custody and CPU-limit checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
