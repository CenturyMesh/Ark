#pragma once
#include "ark/json.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
namespace ark {
// Local-only diagnostic custody. Baselines are private project data and are
// emitted only by an explicit failure_export, never by ordinary discovery.
struct FailureRecord {
    std::string id,baseline;
    // Diagnostic comparison only; immutable baseline and requests retain custody.
    std::string diagnosticBaselineKey,diagnosticRequestKey;
    std::uint64_t revision=0;
    std::string tool;
    arkjson::Value request,replayRequest,error;
    arkjson::Value environment;
    std::size_t bytes=0;
};
inline arkjson::Value failureSummary(const FailureRecord& record,bool details=false){
    using V=arkjson::Value;V out=V::makeObj();out.set("id",V::makeStr(record.id));out.set("tool",V::makeStr(record.tool));out.set("baseline_revision",V::makeNum((double)record.revision));out.set("baseline_bytes",V::makeNum(record.baseline.size()));out.set("record_bytes",V::makeNum(record.bytes));out.set("error",record.error);
    const auto* commands=record.replayRequest.find("commands");out.set("command_count",V::makeNum(commands&&commands->t==V::Arr?commands->arr.size():0));out.set("baseline_included",V::makeBool(false));out.set("replay_available",V::makeBool(commands&&commands->t==V::Arr));
    if(details){out.set("request",record.request);out.set("replay_request",record.replayRequest);out.set("recorded_environment",record.environment);}return out;
}
}
