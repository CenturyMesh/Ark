#pragma once
#include "ark/model.hpp"

namespace ark {
// Explicit status projection. This reads runtime counters without constructing
// an inventory or evaluating geometry; it is not a geometric validation receipt.
inline arkjson::Value revisionReceipt032D(const Document& document,u64 revision){
    using V=arkjson::Value;V r=V::makeObj();
    r.set("response_scope",V::makeStr("revision_status"));
    r.set("revision",V::makeNum((double)revision));r.set("units",V::makeStr("mm"));
    r.set("feature_count",V::makeNum(document.features.size()));
    r.set("recompute_ms",V::makeNum(document.recomputeMs));
    r.set("features_reused",V::makeNum(document.recomputeReused));
    r.set("features_evaluated",V::makeNum(document.recomputeEvaluated));
    r.set("parameter_error",V::makeStr(document.parameterError));
    r.set("history_tip",document.historyTip?V::makeNum((double)document.historyTip):V{});
    if(!document.derivedGeometryStatus.isNull())r.set("derived_geometry",document.derivedGeometryStatus);
    return r;
}
}
