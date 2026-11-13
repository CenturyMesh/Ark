#pragma once
#include <cstddef>
#include "ark/resource_policy_071d.hpp"

namespace ark {
// Derived diagnostics for the most recent recompute on the calling thread.
// They have no document/revision authority and are never persisted.
struct EvaluationSchedulerDiagnostics070A {
    std::size_t dirtyCandidates=0;
    std::size_t readinessChecks=0;
    std::size_t dependencyWakeups=0;
    std::size_t inputLookups=0;
    std::size_t harvestChecks=0;
    std::size_t peakRunningJobs=0;
    ResourcePolicy071D resourcePolicy;
    std::uint64_t minimumPayloadLimitBytes=0;
    unsigned peakOwnerNativeThreads=0;
};
EvaluationSchedulerDiagnostics070A evaluationSchedulerDiagnostics070A();
}
