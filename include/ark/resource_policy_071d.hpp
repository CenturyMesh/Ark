#pragma once
#include "ark/json.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace ark {
struct ResourceHardware071D {
    std::uint64_t totalRamBytes=0,availableRamBytes=0;
    unsigned logicalThreads=0,physicalCores=0;
};
struct ResourceOverrides071D {
    std::optional<unsigned> cpuThreads,featureWorkers,nativeThreads;
    std::optional<std::uint64_t> decodedCacheBytes,payloadBytes;
    bool nativeCacheEnabled=true;
    std::vector<std::string> warnings;
};
struct ResourcePolicy071D {
    ResourceHardware071D hardware;
    std::uint64_t reserveBytes=0,availableHeadroomBytes=0,reclaimableDecodedBytes=0;
    std::uint64_t decodedTargetBytes=0,decodedLimitBytes=0,payloadTargetBytes=0,payloadLimitBytes=0;
    unsigned cpuThreads=1,defaultFeatureWorkers=1,requestedFeatureWorkers=1,featureWorkers=1,nativeThreadsPerWorker=1,ownerNativeThreads=1;
    std::size_t decodedEntryLimit=128;
    std::size_t eligibleTasks=std::numeric_limits<std::size_t>::max();
    std::size_t estimatedParallelWidth=std::numeric_limits<std::size_t>::max();
    bool memoryKnown=false,physicalCoresKnown=false,decodedOverride=false,payloadOverride=false,featureOverride=false;
    bool decodedPressureLimited=false,payloadPressureLimited=false,workersPressureLimited=false;
    std::vector<std::string> reasons,warnings;
};
ResourceHardware071D probeResourceHardware071D();
ResourceOverrides071D resourceOverrides071D();
ResourcePolicy071D selectResourcePolicy071D(const ResourceHardware071D&,const ResourceOverrides071D&,
    std::size_t eligibleTasks=std::numeric_limits<std::size_t>::max(),std::uint64_t retainedDecodedBytes=0,
    std::size_t estimatedParallelWidth=std::numeric_limits<std::size_t>::max());
ResourcePolicy071D currentResourcePolicy071D(std::size_t eligibleTasks=std::numeric_limits<std::size_t>::max(),std::uint64_t retainedDecodedBytes=0,
    std::size_t estimatedParallelWidth=std::numeric_limits<std::size_t>::max());
arkjson::Value resourcePolicyReceipt071D(const ResourcePolicy071D&);
}
