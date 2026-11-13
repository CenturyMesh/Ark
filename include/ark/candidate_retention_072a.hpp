#pragma once
#include "ark/resource_policy_071d.hpp"
#include <algorithm>
#include <limits>
#include <new>

namespace ark {
struct CandidateRetentionPolicy072A {
    size_t softTargetBytes=0;
    bool resourcePolicyAvailable=true;
};
inline CandidateRetentionPolicy072A candidateRetentionPolicy072A() noexcept {
    try {const auto policy=currentResourcePolicy071D();return {static_cast<size_t>(std::min<std::uint64_t>(policy.payloadLimitBytes,std::numeric_limits<size_t>::max())),true};}
    catch(...){return {0,false};} // optional cache-policy allocation/configuration cannot turn a valid edit into a rejection
}
inline void describeCandidateRetention072A(arkjson::Value& out,const CandidateRetentionPolicy072A& policy,size_t additional,size_t retained,size_t count){
    using V=arkjson::Value;
    out.set("candidate_retention_target_bytes",V::makeNum(double(policy.softTargetBytes)));
    out.set("candidate_budget_bytes",V::makeNum(double(policy.softTargetBytes))); // compatibility alias, explicitly soft
    out.set("candidate_budget_is_hard_limit",V::makeBool(false));out.set("candidate_hard_limit_bytes",V{});out.set("max_edit_bytes",V{});
    out.set("candidate_additional_bytes",V::makeNum(double(additional)));out.set("candidate_retained_bytes",V::makeNum(double(retained)));out.set("candidate_retained_count",V::makeNum(double(count)));
    out.set("candidate_over_soft_target",V::makeBool(retained>policy.softTargetBytes));
    out.set("candidate_retention_policy",V::makeStr("adaptive_soft_target_keep_newest_validated"));
    out.set("allocation_scope",V::makeStr("Conservatively accounted retained allocations with shared live geometry excluded. The adaptive target evicts older cached proposals; the newest validated candidate is retained even above it. No per-edit hard memory ceiling."));
    if(!policy.resourcePolicyAvailable)out.set("candidate_retention_warning",V::makeStr("Resource policy unavailable; newest validated candidate retained, older cache entries may be evicted."));
}
}
