#pragma once
#include "ark/agent.hpp"

namespace ark {
arkjson::Value surfaceControlSchema059();
// Read-only exact inspection and explicit revision-bound SurfaceEdit plans.
arkjson::Value surfaceControl059(AgentSession&, const arkjson::Value&);
// These plans retain a candidate in the owning session. A disposable exact-read
// worker or a batched postcondition cannot own the later explicit publication.
inline bool surfaceControlRetainsCandidate068B(const arkjson::Value& args){
    const auto* action=args.find("action");if(!action||action->t!=arkjson::Value::Str)return false;
    const auto& value=action->str;return value=="match_plan"||value=="network_plan"||value=="multi_trim_plan"||value=="extend_trimmed_plan"||value=="sew_plan"||value=="offset_plan";
}
}
