#include "ark/agent.hpp"
#include "ark/async_authoring.hpp"
#include "ark/iteration_history.hpp"
#include "ark/usage.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>

namespace {
using namespace ark;using V=arkjson::Value;
constexpr size_t MiB=1024u*1024u;
constexpr const char* payloadKey="synthetic_private_acceleration_allocation";
size_t checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
V json(const char* source){V value;if(!arkjson::parse(source,value))throw std::runtime_error("Invalid synthetic candidate fixture");return value;}
const V& field(const V& value,const char* name){const auto* found=value.find(name);if(!found)throw std::runtime_error(std::string("Missing ")+name);return *found;}
V accepted(const V& response){if(field(response,"isError").b)throw std::runtime_error(response.dump());return field(response,"structuredContent");}
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
V args(AgentSession& session,const char* commands){V request=V::makeObj();request.set("expected_revision",V::makeNum(session.revision));request.set("commands",json(commands));request.set("detail",V::makeStr("changes"));return request;}
void setup(AgentSession& session){session.suppressFailureCapture();session.suppressIterationHistory();accepted(session.call("doc_apply",args(session,R"([{"op":"feature.add","type":"box","name":"Unchanged source","p":{"dx":2,"dy":3,"dz":4}},{"op":"feature.add","type":"box","name":"Editable leaf","p":{"dx":5,"dy":6,"dz":7}}])")));}
void payload(Document& document,u64 id,size_t bytes){
    // This is a real allocated private acceleration-cache stress field, not
    // invented B-rep evidence. Public box/native geometry remains authoritative.
    // Prepared snapshots copy and charge these maps independently of shared bodies.
    auto& cache=document.referenceFrameCache[id];cache.owner=document.evaluationCache.at(id).body->nativeData.weakOwner();cache.frames[payloadKey]=V::makeStr(std::string(bytes,'q'));
}
const std::string& payload(const Document& document,u64 id){return document.referenceFrameCache.at(id).frames.at(payloadKey).str;}
void softReceipt(const V& preview){
    check(field(preview,"valid").b&&field(preview,"candidate_cached").b,"Large validated preview must retain an applicable candidate");
    check(field(preview,"candidate_additional_bytes").num>128.*MiB,"Fixture must actually charge more than the former128MiB limit");
    check(!field(preview,"candidate_budget_is_hard_limit").b&&field(preview,"candidate_hard_limit_bytes").isNull()&&field(preview,"max_edit_bytes").isNull(),"Receipt must explicitly expose no hard per-edit memory ceiling");
    check(field(preview,"candidate_over_soft_target").b&&field(preview,"candidate_retained_count").num==1,"Sole validated candidate must survive above the soft target");
}
V commitWithoutEvaluation(AgentSession& session,const V& request){
    size_t evaluations=0;const auto previous=setEvaluationObserver([&](u64,const std::string&,size_t,size_t,bool done,bool){if(!done)++evaluations;});
    V response;try{response=session.call("doc_apply",request);}catch(...){setEvaluationObserver(previous);throw;}setEvaluationObserver(previous);
    const auto result=accepted(response);check(field(result,"applied_prepared").b&&!field(result,"geometry_recomputed").b&&field(result,"features_evaluated").num==0&&evaluations==0,"Retained apply must publish validated geometry without a second evaluation");return result;
}
bool missing(AgentSession& session,const V& preview){try{session.preparedRecipe(field(preview,"candidate_id").str);return false;}catch(const std::exception&){return true;}}
void oversizedPreviewAndReadBoundary(){
    Environment target("ARK_FEATURE_PAYLOAD_MAX_MB","1");AgentSession session;setup(session);payload(session.document,1,129*MiB);
    const auto checksum=iterationSha256(payload(session.document,1));const auto baseline=documentToJson(session.document);const auto revision=session.revision;const auto owner=session.document.evaluationCache.at(1).body;
    auto first=accepted(session.call("doc_preview",args(session,R"([{"op":"feature.edit","id":2,"p":{"dx":8}}])")));softReceipt(first);
    check(documentToJson(session.document)==baseline&&session.revision==revision,"Large preview mutated live authoring");
    accepted(session.call("doc_info",json(R"({"detail":"revision"})")));check(!missing(session,first),"Serialized read boundary evicted newest oversized candidate");
    auto second=accepted(session.call("doc_preview",args(session,R"([{"op":"feature.edit","id":2,"p":{"dx":9}}])")));softReceipt(second);check(missing(session,first),"Soft retention pressure should evict an older cached proposal");
    check(field(session.call("doc_apply",field(first,"apply")),"isError").b&&documentToJson(session.document)==baseline,"Evicted identity must reject atomically");
    {Environment invalidPolicy("ARK_CPU_THREADS","invalid");accepted(session.call("doc_info",json(R"({"detail":"revision"})")));check(!missing(session,second),"Optional retention-policy failure must not block reads or discard the newest candidate");}
    target.set("0");accepted(session.call("doc_info",json(R"({"detail":"revision"})")));check(!missing(session,second),"Zero cache target cannot erase the only validated edit");
    commitWithoutEvaluation(session,field(second,"apply"));check(session.document.evalBodies[1].pd1.x==9&&session.document.evaluationCache.at(1).body==owner,"Large commit changed unrelated immutable geometry or lost the edit");
    check(payload(session.document,1).size()==129*MiB&&iterationSha256(payload(session.document,1))==checksum,"Publication discarded or altered allocated candidate evidence");
}
void retained128Stress(){
    Environment target("ARK_FEATURE_PAYLOAD_MAX_MB","128");AgentSession session;setup(session);payload(session.document,1,65*MiB);
    auto first=accepted(session.call("doc_preview",args(session,R"([{"op":"feature.edit","id":2,"p":{"dx":8}}])")));
    auto second=accepted(session.call("doc_preview",args(session,R"([{"op":"feature.edit","id":2,"p":{"dx":9}}])")));
    check(field(first,"candidate_additional_bytes").num>65.*MiB&&field(first,"candidate_additional_bytes").num<128.*MiB,"First stress candidate is below the old individual limit");
    check(field(second,"candidate_retained_count").num==1&&missing(session,first)&&!missing(session,second),"Aggregate retention pressure must discard older receipts, not block the newest");
    payload(session.document,1,129*MiB);auto large=accepted(session.call("doc_preview",args(session,R"([{"op":"feature.edit","id":2,"p":{"dx":10}}])")));softReceipt(large);
    check(field(large,"candidate_retention_target_bytes").num<=128.*MiB&&missing(session,second),"A candidate above configured128MiB target must still be retained after older eviction");
    commitWithoutEvaluation(session,field(large,"apply"));check(session.document.evalBodies[1].pd1.x==10&&payload(session.document,1).size()==129*MiB,"Above-target publication lost validated data");
}
AsyncAuthoringWorker::Result wait(AsyncAuthoringWorker& worker){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);do{if(auto result=worker.take())return std::move(*result);std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<deadline);throw std::runtime_error("Large async preparation timed out");}
void adoptionAndAsync(){
    Environment target("ARK_FEATURE_PAYLOAD_MAX_MB","1");AgentSession session;setup(session);payload(session.document,1,129*MiB);const auto checksum=iterationSha256(payload(session.document,1));
    const auto prepared=session.preparePreview(args(session,R"([{"op":"feature.edit","id":2,"p":{"dx":8}}])"));softReceipt(prepared);auto transfer=session.takePreparedCandidate(field(prepared,"candidate_id").str);
    const auto baseline=documentToJson(session.document);bool stale=false;try{session.adoptPreparedCandidate(std::move(*transfer),session.revision+1);}catch(const std::exception&){stale=true;}check(stale&&documentToJson(session.document)==baseline,"Stale adoption must reject without consuming or publishing geometry");
    session.adoptPreparedCandidate(std::move(*transfer),session.revision);accepted(session.call("doc_info",V::makeObj()));check(!missing(session,prepared),"Adoption/tool boundary rejected large validated receipt");commitWithoutEvaluation(session,field(prepared,"apply"));
    AsyncAuthoringWorker worker;auto change=args(session,R"([{"op":"feature.edit","id":2,"p":{"dx":11}}])");worker.start(session.document,session.revision,41,change);auto ready=wait(worker);
    check(ready.transfer&&!ready.rejection.find("isError"),"Validated asynchronous candidate above128MiB must be transferable");
    check(payload(ready.transfer->document(),1).size()==129*MiB,"Async transfer lost allocated private evidence");
    size_t evaluations=0;const auto observer=setEvaluationObserver([&](u64,const std::string&,size_t,size_t,bool done,bool){if(!done)++evaluations;});V publication;try{publication=AsyncAuthoringWorker::publish(std::move(ready),session,41);}catch(...){setEvaluationObserver(observer);throw;}setEvaluationObserver(observer);const auto applied=accepted(publication);
    check(field(applied,"applied_prepared").b&&!field(applied,"geometry_recomputed").b&&evaluations==0&&session.document.evalBodies[1].pd1.x==11,"Async large publication recomputed or changed validated geometry");
    check(iterationSha256(payload(session.document,1))==checksum,"Async large publication discarded retained evidence");
    change=args(session,R"([{"op":"feature.edit","id":2,"p":{"dx":12}}])");worker.start(session.document,session.revision,41,change);ready=wait(worker);const auto before=documentToJson(session.document);const auto revision=session.revision;
    check(field(AsyncAuthoringWorker::publish(std::move(ready),session,42),"isError").b&&documentToJson(session.document)==before&&session.revision==revision,"Large candidate must not bypass replacement epoch rejection");
    AsyncAuthoringWorker allocationFailure([](const std::atomic_bool&){throw std::bad_alloc();});allocationFailure.start(session.document,session.revision,43,change);auto failed=wait(allocationFailure);
    check(!failed.transfer&&!failed.rejection.isNull(),"Allocation exception must remain an explicit preparation failure");
    check(field(AsyncAuthoringWorker::publish(std::move(failed),session,43),"isError").b&&documentToJson(session.document)==before&&session.revision==revision,"Allocation-failure completion changed the live document");
}
void boundedCount(){Environment target("ARK_FEATURE_PAYLOAD_MAX_MB","1024");AgentSession session;setup(session);V first;for(int n=0;n<5;++n){V request=args(session,R"([{"op":"feature.edit","id":2,"p":{"dx":8}}])");auto commands=field(request,"commands"),parameters=field(commands.arr[0],"p");parameters.set("dx",V::makeNum(8+n));commands.arr[0].set("p",parameters);request.set("commands",commands);const auto value=accepted(session.call("doc_preview",request));if(!n)first=value;check(field(value,"candidate_retained_count").num<=4,"Cache count policy exceeded four previews");}check(missing(session,first),"Existing oldest-first count eviction must remain");}
void metadataPublication(){
    AgentSession session;setup(session);session.suppressIterationHistory(false);
    const auto owner=session.document.evaluationCache.at(2).body;
    auto preview=accepted(session.call("doc_preview",args(session,R"([{"op":"material.set","name":"Paint","appearance":{"base_color":[1,0,0],"roughness":0.3}},{"op":"feature.edit","id":2,"material":"Paint"}])")));
    check(field(preview,"candidate_cached").b&&!field(field(preview,"candidate_result"),"geometry_recomputed").b,"Metadata fixture must use retained metadata-only path");
    UsageReceipt usage;usage.id="candidate-metadata-newer-usage";usage.source="studio_assistant";usage.provenance="provider_response";usage.model="offline-fixture";usage.inputTokens=12;usage.outputTokens=3;usage.totalTokens=15;usage.reportedResponses=1;std::string error;
    check(addUsageReceipt(session.document,usage,&error)==UsageRecordResult::Added,"Add intervening live usage");
    auto apply=field(preview,"apply");apply.set("actor",json(R"({"kind":"human","id":"metadata-move-fixture"})"));apply.set("motivation",V::makeStr("Review and apply retained paint"));
    commitWithoutEvaluation(session,apply);
    check(session.document.evaluationCache.at(2).body==owner&&session.document.features[1].material=="Paint"&&session.document.materials.size()==1,"Metadata moves must preserve evaluated geometry while publishing authored values");
    check(usageTotals(session.document).totalTokens==15,"Metadata publication overwrote newer live usage");
    const auto& node=field(session.document.iterationHistory,"nodes").arr.back();check(field(field(node,"actor"),"id").str=="metadata-move-fixture"&&field(node,"motivation").str=="Review and apply retained paint","Metadata publication lost caller history provenance");
    accepted(session.call("doc_undo",V::makeObj()));check(session.document.features[1].material.empty()&&session.document.materials.empty(),"Metadata publication must remain one undoable edit");
}
#ifdef ARK_CANDIDATE_RETENTION_NATIVE_TESTS
void largeDfm(){
    Environment target("ARK_FEATURE_PAYLOAD_MAX_MB","1");AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();
    accepted(session.call("doc_apply",args(session,R"([{"op":"feature.add","type":"native_solid","name":"Part","native":{"op":"box"},"p":{"width":20,"depth":10,"height":5},"manufacturing":{"route":"additive","process":{"method":"fdm","layer_height_mm":0.2,"nozzle_diameter_mm":0.4,"build_direction":[0,0,1],"build_volume_mm":[19,10,5]}}},{"op":"feature.add","type":"native_solid","name":"Unchanged reference","native":{"op":"box"},"p":{"width":2,"depth":3,"height":4}}])")));
    payload(session.document,2,129*MiB);const auto checksum=iterationSha256(payload(session.document,2));const auto baseline=documentToJson(session.document);const auto revision=session.revision;
    auto proposal=json(R"({"id":"Part","rule_id":"DFM.FDM.02","rule_version":1,"commands":[{"op":"feature.edit","id":"Part","p":{"width":18}}]})");proposal.set("expected_revision",V::makeNum(session.revision));const auto preview=accepted(session.call("manufacturing_propose",proposal));softReceipt(preview);
    check(field(field(preview,"dfm_comparison"),"target_screen_resolved").b&&field(field(preview,"dfm_comparison"),"same_process_inputs").b,"Large DFM proposal lost measured comparison or process custody");
    check(documentToJson(session.document)==baseline&&session.revision==revision,"Large DFM proposal mutated live geometry");
    auto apply=field(preview,"apply");apply.obj.erase(std::remove_if(apply.obj.begin(),apply.obj.end(),[](const auto& item){return item.first=="accept_dfm";}),apply.obj.end());
    check(field(session.call("doc_apply",apply),"isError").b&&documentToJson(session.document)==baseline,"Large DFM candidate bypassed explicit acceptance");
    accepted(session.call("doc_info",json(R"({"detail":"revision"})")));check(!missing(session,preview),"DFM failure/read boundary evicted newest oversized candidate");
    const auto applied=commitWithoutEvaluation(session,field(preview,"apply"));check(field(applied,"dfm_accepted").b&&session.document.find(1)->get("width")=="18","Explicit large DFM acceptance failed");
    check(iterationSha256(payload(session.document,2))==checksum,"DFM commit discarded oversized retained evidence");
    const auto& report=*session.document.evalBodies[0].nativeData.find("report");check(std::abs(field(report,"volume_mm3").num-18*10*5)<1e-8,"Large DFM candidate changed exact analytic volume");
}
#endif
}
int main(){try{oversizedPreviewAndReadBoundary();retained128Stress();adoptionAndAsync();boundedCount();metadataPublication();
#ifdef ARK_CANDIDATE_RETENTION_NATIVE_TESTS
largeDfm();
#endif
std::cout<<"072A candidate retention: "<<checks<<" real129MiB allocation, soft-pressure, apply, transfer, async, failure and DFM checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
