#include "ark/agent.hpp"
#include <atomic>
#include <random>
#include <sstream>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
std::string fingerprint(const Document& doc){
    std::string value=documentToJson(doc);
    for(double x:{doc.tessOpt.tol,doc.tessOpt.maxEdge,doc.tessOpt.minAngleDeg,(double)doc.tessOpt.maxLevels})value+='|'+V::makeNum(x).dump();
    return value;
}
void checkRevision(const V& args,u64 revision){const auto* expected=args.find("expected_revision");if(!expected||expected->t!=V::Num||expected->num!=(double)revision)throw std::runtime_error("Revision conflict; read doc_info with detail design and retry");}
size_t candidateBytes(const Document& doc){size_t bytes=documentToJson(doc).size();for(const auto& item:doc.referenceFrameCache){bytes+=item.second.brep.size()+item.second.topology.size();for(const auto& frame:item.second.frames)bytes+=frame.first.size()+frame.second.dump().size()*4;}for(const auto& b:doc.evalBodies){bytes+=2*b.nativeData.dump().size();if(b.baked)bytes+=2*(b.baked->pos.size()+b.baked->nrm.size()+b.baked->idx.size())*sizeof(float);bytes+=b.faces.size()*512;}return bytes;}
std::string candidateId(){static std::atomic<unsigned long long> next{0};static const auto salt=[](){std::random_device random;return ((unsigned long long)random()<<32)^random();}();std::ostringstream out;out<<std::hex<<salt<<'-'<<++next;return out.str();}
}
arkjson::Value AgentSession::preparePreview(const V& args){
    checkRevision(args,revision);if(args.find("candidate_id"))throw std::runtime_error("Preview requires commands, not another candidate");
    const auto now=std::chrono::steady_clock::now();
    prepared_.erase(std::remove_if(prepared_.begin(),prepared_.end(),[&](const Prepared& p){return p.expires<=now||p.revision!=revision;}),prepared_.end());
    AgentSession candidate;candidate.document=document;candidate.document.evaluatingBodies=nullptr;candidate.revision=revision;
    const auto response=candidate.call("doc_apply",args);V out=V::makeObj();bool valid=!response.find("isError")->b;
    out.set("valid",V::makeBool(valid));out.set("revision",V::makeNum((double)revision));out.set("candidate_result",*response.find("structuredContent"));out.set("apply",args);
    if(!valid)return out;
    V view=V::makeObj();view.set("detail",V::makeStr("design"));out.set("before",*call("doc_info",view).find("structuredContent"));out.set("after",*candidate.call("doc_info",view).find("structuredContent"));
    const size_t bytes=candidateBytes(candidate.document),budget=128u*1024u*1024u;
    if(bytes>budget){out.set("candidate_cached",V::makeBool(false));out.set("cache_reason",V::makeStr("Candidate exceeds 128 MiB cache budget; apply commands recomputes"));return out;}
    size_t used=0;for(const auto& p:prepared_)used+=p.bytes;
    while(!prepared_.empty()&&(prepared_.size()>=4||used+bytes>budget)){used-=prepared_.front().bytes;prepared_.erase(prepared_.begin());}
    Prepared prepared;prepared.id=candidateId();prepared.baseline=fingerprint(document);prepared.revision=revision;prepared.bytes=bytes;prepared.document=std::make_shared<Document>(std::move(candidate.document));prepared.result=*response.find("structuredContent");prepared.expires=std::chrono::steady_clock::now()+std::chrono::minutes(5);
    V apply=V::makeObj();apply.set("expected_revision",V::makeNum((double)revision));apply.set("candidate_id",V::makeStr(prepared.id));out.set("apply",apply);out.set("candidate_id",V::makeStr(prepared.id));out.set("candidate_cached",V::makeBool(true));out.set("expires_in_seconds",V::makeNum(300));
    // The expanded recipe remains inspectable and can be submitted explicitly.
    out.set("recipe",args);prepared_.push_back(std::move(prepared));return out;
}
arkjson::Value AgentSession::commitPrepared(const V& args){
    checkRevision(args,revision);for(const auto& field:args.obj)if(field.first!="expected_revision"&&field.first!="candidate_id")throw std::runtime_error("Prepared apply accepts only candidate_id and expected_revision");
    const auto* id=args.find("candidate_id");if(!id||id->t!=V::Str)throw std::runtime_error("candidate_id must be text");
    auto found=std::find_if(prepared_.begin(),prepared_.end(),[&](const Prepared& p){return p.id==id->str;});
    if(found==prepared_.end())throw std::runtime_error("Prepared candidate missing, evicted or already applied");
    if(found->expires<=std::chrono::steady_clock::now()||found->revision!=revision||found->baseline!=fingerprint(document)){prepared_.erase(found);throw std::runtime_error("Prepared candidate expired or document changed; preview again");}
    auto next=found->document;V out=found->result;remember();document=std::move(*next);++revision;prepared_.clear();
    V evaluation=V::makeObj();for(const char* key:{"recompute_ms","features_evaluated","features_reused","parallel_features","peak_parallel_features"})if(const auto* value=out.find(key))evaluation.set(key,*value);out.set("prepared_evaluation",evaluation);
    out.set("features_evaluated",V::makeNum(0));out.set("features_reused",V::makeNum(document.evalBodies.size()));out.set("parallel_features",V::makeNum(0));out.set("peak_parallel_features",V::makeNum(0));
    out.set("revision",V::makeNum((double)revision));out.set("geometry_recomputed",V::makeBool(false));out.set("applied_prepared",V::makeBool(true));out.set("recompute_ms",V::makeNum(0));return out;
}
}
