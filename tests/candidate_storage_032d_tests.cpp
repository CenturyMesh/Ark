#include "ark/agent.hpp"
#include "ark/serialize.hpp"
#include "ark/native_backend.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ark;using V=arkjson::Value;size_t checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
V json(const char* text){V result;check(arkjson::parse(text,result),"fixture JSON");return result;}
const V& field(const V& value,const char* name){auto* found=value.find(name);if(!found)throw std::runtime_error(std::string("Missing ")+name);return *found;}
V accepted(V response){if(field(response,"isError").b)throw std::runtime_error(response.dump());return field(response,"structuredContent");}
V batch(AgentSession& session,const char* commands){V args=V::makeObj();args.set("expected_revision",V::makeNum(session.revision));args.set("commands",json(commands));return args;}
V preview(AgentSession& session,const char* commands){auto args=batch(session,commands);args.set("detail",V::makeStr("changes"));auto result=accepted(session.call("doc_preview",args));check(field(result,"valid").b&&field(result,"candidate_cached").b,"valid preview retained");return result;}
void setup(AgentSession& session){session.suppressFailureCapture();accepted(session.call("doc_apply",batch(session,R"([{"op":"feature.add","type":"box","name":"Base","p":{"dx":2,"dy":3,"dz":4}},{"op":"feature.add","type":"box","name":"Leaf","p":{"dx":5,"dy":6,"dz":7}}])")));}
void commit(AgentSession& session,const V& candidate){size_t evaluated=0;auto previous=setEvaluationObserver([&](u64,const std::string&,size_t,size_t,bool complete,bool){if(!complete)++evaluated;});V response;try{response=session.call("doc_apply",field(candidate,"apply"));}catch(...){setEvaluationObserver(std::move(previous));throw;}setEvaluationObserver(std::move(previous));const auto result=accepted(response);check(!field(result,"geometry_recomputed").b&&field(result,"features_evaluated").num==0&&evaluated==0,"cached apply must publish without evaluation");}
void sharedLargeBase(){
    AgentSession session;setup(session);
    // Allocation stress uses an opaque derived fixture field, never a private
    // document or a claim that arbitrary bytes constitute native B-rep evidence.
    // Geometry remains the public exact legacy box; the blob tests custody.
    Body large=*session.document.evaluationCache.at(1).body;
    large.nativeData=V::makeObj();large.nativeData.set("synthetic_retention_payload",V::makeStr(std::string(129u*1024u*1024u,'x')));
    session.document.evaluationCache.at(1).body=std::make_shared<const Body>(std::move(large));
    session.document.evalBodies[0]=*session.document.evaluationCache.at(1).body;
    const auto owner=session.document.evaluationCache.at(1).body;
    const auto source=documentToJson(session.document);const auto revision=session.revision;
    const auto candidate=preview(session,R"([{"op":"feature.edit","id":2,"p":{"dx":5.01}}])");
    check(documentToJson(session.document)==source&&session.revision==revision,"preview preserves authoring/revision");
    check(!field(candidate,"candidate_result").find("features"),"changes candidate result omits inventory");
    commit(session,candidate);
    check(session.document.evaluationCache.at(1).body==owner,"unchanged immutable body identity survives cached commit");
    check(session.document.evaluationCache.at(2).body->pd1.x==5.01,"leaf candidate geometry preserved");
    check(field(session.document.evalBodies[0].nativeData,"synthetic_retention_payload").str.size()==129u*1024u*1024u,"materialization preserves full derived payload");
    const auto retained=preview(session,R"([{"op":"feature.edit","id":2,"p":{"dy":6.01}}])");
    accepted(session.call("doc_apply",batch(session,R"([{"op":"feature.edit","id":1,"p":{"dx":9}}])")));
    // The old base is no longer live-owned. It alone exceeds the receipt budget,
    // even though this test still holds an external shared_ptr to it.
    bool evicted=false;try{session.preparedRecipe(field(retained,"candidate_id").str);}catch(const std::exception&){evicted=true;}check(evicted,"former live owners are recharged and over-budget stale candidates evicted");
}
void transferAndRollback(){
    AgentSession session;setup(session);
    const auto candidate=preview(session,R"([{"op":"feature.edit","id":2,"p":{"dx":8}}])");
    const auto original=documentToJson(session.document);const auto oldOwner=session.document.evaluationCache.at(1).body;
    auto transfer=session.takePreparedCandidate(field(candidate,"candidate_id").str);
    const auto& materialized=transfer->document();check(materialized.evalBodies.size()==2,"transfer materializes ordered complete bodies");check(materialized.evalBodies[1].pd1.x==8,"transfer materializes checked edit");
    check(materialized.evaluationCache.at(1).body==oldOwner,"transfer preserves shared immutable base ownership");
    bool stale=false;try{session.adoptPreparedCandidate(std::move(*transfer),session.revision+1);}catch(const std::exception&){stale=true;}check(stale&&documentToJson(session.document)==original,"stale adoption fails atomically without consuming receipt");
    session.adoptPreparedCandidate(std::move(*transfer),session.revision);
    auto invalid=batch(session,R"([{"op":"feature.edit","id":2,"p":{"dx":-1}}])");check(field(session.call("doc_apply",invalid),"isError").b,"invalid live edit rejected");
    check(documentToJson(session.document)==original,"invalid edit leaves authoring intact");commit(session,candidate);
    check(session.document.evalBodies[1].pd1.x==8,"retained candidate survives failed edit rollback");
}
#if defined(ARK_CONTRACTS_NATIVE_TESTS) && defined(_WIN32)
void checkedLegacyEnrichment(){
    AgentSession session;setup(session);
    const auto baselineOwner=session.document.evaluationCache.at(2).body;
    check(!baselineOwner->nativeData.find("brep_base64"),"fixture starts with legacy cached body without native evidence");
    auto args=batch(session,R"([{"op":"feature.edit","id":2,"p":{"dx":8}}])");args.set("detail",V::makeStr("changes"));
    args.set("checks",json(R"([{"query":{"tool":"native_inspect","args":{"id":2,"detail":"properties"},"path":"report.brep_valid"},"op":"true"}])"));
    const auto candidate=accepted(session.call("doc_preview",args));
    check(field(candidate,"valid").b&&field(candidate,"candidate_cached").b,"checked legacy native enrichment retains candidate");
    check(!baselineOwner->nativeData.find("brep_base64"),"enrichment cannot mutate immutable baseline owner");
    const double before=field(nativeKernelInfo(),"total_worker_requests").num;commit(session,candidate);
    const auto& cached=*session.document.evaluationCache.at(2).body;
    check(cached.nativeData.find("brep_base64")&&session.document.evalBodies[1].nativeData.find("brep_base64"),"cache and published body preserve checked native B-rep");
    const auto inspected=accepted(session.call("native_inspect",json(R"({"id":2,"detail":"properties"})")));
    check(field(field(inspected,"report"),"brep_valid").b,"checked published native body remains valid");
    check(std::abs(field(field(inspected,"report"),"volume_mm3").num-8*6*7)<1e-8,"checked native body retains edited exact dimensions");
    check(field(nativeKernelInfo(),"total_worker_requests").num==before+1,"cached commit and repeated native inspection make no producer requests");
}
#endif
}
int main(){try{sharedLargeBase();transferAndRollback();
#if defined(ARK_CONTRACTS_NATIVE_TESTS) && defined(_WIN32)
checkedLegacyEnrichment();
#endif
std::cout<<checks<<" candidate storage checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
