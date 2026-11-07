#include "ark/resource_policy_071d.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ark {
namespace {
constexpr std::uint64_t MiB=1024ull*1024ull,GiB=1024ull*MiB;
unsigned strictThreads(const char* name,unsigned maximum){const char* value=std::getenv(name);char* end=nullptr;errno=0;const auto parsed=std::strtoll(value,&end,10);if(errno||end==value||*end||parsed<1||parsed>maximum)throw std::runtime_error(std::string(name)+" has an invalid thread budget");return static_cast<unsigned>(parsed);}
std::optional<std::uint64_t> megabytes(const char* name,bool reject,std::vector<std::string>& warnings){
    const char* value=std::getenv(name);if(!value)return {};
    char* end=nullptr;errno=0;const auto parsed=std::strtoll(value,&end,10);
    if(errno||end==value||*end||parsed<0||parsed>65536){if(reject)throw std::runtime_error(std::string(name)+" must be 0..65536 MiB");warnings.push_back(std::string(name)+": invalid override ignored; using hardware policy");return {};}
    return static_cast<std::uint64_t>(parsed)*MiB;
}
std::uint64_t roundedMiB(std::uint64_t bytes){return bytes/MiB*MiB;}
}
ResourceHardware071D probeResourceHardware071D(){
    static const auto topology=[](){ResourceHardware071D result;result.logicalThreads=std::max(1u,std::thread::hardware_concurrency());
#ifdef _WIN32
        const auto logical=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);if(logical)result.logicalThreads=logical;
        DWORD bytes=0;(void)GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&bytes);
        if(bytes&&bytes<=1024u*1024u){std::vector<unsigned char> storage(bytes);if(GetLogicalProcessorInformationEx(RelationProcessorCore,reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(storage.data()),&bytes)){
            constexpr size_t headerBytes=sizeof(LOGICAL_PROCESSOR_RELATIONSHIP)+sizeof(DWORD);
            for(size_t offset=0;offset+headerBytes<=bytes;){const auto* row=reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(storage.data()+offset);if(row->Size<headerBytes||row->Size>bytes-offset)break;if(row->Relationship==RelationProcessorCore)++result.physicalCores;offset+=row->Size;}
        }}
#endif
        return result;
    }();
    auto result=topology;
#ifdef _WIN32
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);if(GlobalMemoryStatusEx(&memory)){result.totalRamBytes=memory.ullTotalPhys;result.availableRamBytes=memory.ullAvailPhys;}
#elif defined(_SC_PHYS_PAGES) && defined(_SC_AVPHYS_PAGES)
    const long page=sysconf(_SC_PAGESIZE),total=sysconf(_SC_PHYS_PAGES),available=sysconf(_SC_AVPHYS_PAGES);if(page>0&&total>0&&available>=0){result.totalRamBytes=std::uint64_t(page)*std::uint64_t(total);result.availableRamBytes=std::uint64_t(page)*std::uint64_t(available);}
#endif
    return result;
}
ResourceOverrides071D resourceOverrides071D(){
    ResourceOverrides071D result;
    if(std::getenv("ARK_CPU_THREADS"))result.cpuThreads=strictThreads("ARK_CPU_THREADS",64);
    if(std::getenv("ARK_FEATURE_THREADS"))result.featureWorkers=strictThreads("ARK_FEATURE_THREADS",64);
    if(std::getenv("ARK_NATIVE_THREADS"))result.nativeThreads=strictThreads("ARK_NATIVE_THREADS",64);
    result.decodedCacheBytes=megabytes("ARK_NATIVE_MEMORY_CACHE_MAX_MB",false,result.warnings);
    result.payloadBytes=megabytes("ARK_FEATURE_PAYLOAD_MAX_MB",true,result.warnings);
    if(const char* enabled=std::getenv("ARK_NATIVE_RESULT_CACHE");enabled&&std::string(enabled)=="0")result.nativeCacheEnabled=false;
    return result;
}
ResourcePolicy071D selectResourcePolicy071D(const ResourceHardware071D& hardware,const ResourceOverrides071D& overrides,std::size_t eligible,std::uint64_t retained,std::size_t width){
    auto threads=[](const std::optional<unsigned>& value,const char* name){if(value&&(*value<1||*value>64))throw std::runtime_error(std::string(name)+" has an invalid thread budget");};threads(overrides.cpuThreads,"ARK_CPU_THREADS");threads(overrides.featureWorkers,"ARK_FEATURE_THREADS");threads(overrides.nativeThreads,"ARK_NATIVE_THREADS");
    if((overrides.decodedCacheBytes&&*overrides.decodedCacheBytes>65536ull*MiB)||(overrides.payloadBytes&&*overrides.payloadBytes>65536ull*MiB))throw std::runtime_error("Resource memory override exceeds 65536 MiB");
    ResourcePolicy071D result;result.hardware=hardware;result.eligibleTasks=eligible;result.estimatedParallelWidth=width;result.warnings=overrides.warnings;
    const unsigned logical=std::max(1u,hardware.logicalThreads);result.physicalCoresKnown=hardware.physicalCores>0&&hardware.physicalCores<=logical;
    result.memoryKnown=hardware.totalRamBytes>0&&hardware.availableRamBytes<=hardware.totalRamBytes;
    result.cpuThreads=std::min(logical,overrides.cpuThreads.value_or(std::min(logical,32u)));
    result.defaultFeatureWorkers=result.physicalCoresKnown?std::min({16u,hardware.physicalCores,std::max(1u,logical/2)}):std::min(4u,logical);
    if(!result.memoryKnown)result.defaultFeatureWorkers=std::min(result.defaultFeatureWorkers,4u);
    result.requestedFeatureWorkers=overrides.featureWorkers.value_or(result.defaultFeatureWorkers);result.featureOverride=overrides.featureWorkers.has_value();
    result.featureWorkers=std::max(1u,std::min(result.cpuThreads,result.requestedFeatureWorkers));
    result.reasons.push_back(result.featureOverride?"explicit_worker_ceiling":"automatic_physical_and_logical_core_ceiling");
    if(!result.physicalCoresKnown)result.reasons.push_back("physical_topology_unavailable_conservative_workers");
    if(result.requestedFeatureWorkers>result.cpuThreads)result.reasons.push_back("workers_clamped_to_cpu_allowance");
    result.decodedOverride=overrides.decodedCacheBytes.has_value();result.payloadOverride=overrides.payloadBytes.has_value();
    result.decodedTargetBytes=overrides.decodedCacheBytes.value_or(result.memoryKnown?roundedMiB(std::min<std::uint64_t>(hardware.totalRamBytes/16,16ull*GiB)):256ull*MiB);
    result.payloadTargetBytes=overrides.payloadBytes.value_or(result.memoryKnown?roundedMiB(std::min<std::uint64_t>(hardware.totalRamBytes/64,4ull*GiB)):128ull*MiB);
    result.decodedLimitBytes=result.decodedTargetBytes;result.payloadLimitBytes=result.payloadTargetBytes;
    result.reasons.push_back(result.decodedOverride?"explicit_decoded_ram_ceiling":"decoded_ram_target_one_sixteenth_total");
    result.reasons.push_back(result.payloadOverride?"explicit_payload_ceiling":"payload_target_one_sixty_fourth_total");
    if(result.memoryKnown){
        const auto total=hardware.totalRamBytes,available=hardware.availableRamBytes;
        result.reserveBytes=std::min(total/2,std::min<std::uint64_t>(4ull*GiB,std::max<std::uint64_t>(256ull*MiB,total/8)));
        result.availableHeadroomBytes=available>result.reserveBytes?available-result.reserveBytes:0;
        result.reclaimableDecodedBytes=std::min(retained,total);
        const auto includingCache=available+std::min(result.reclaimableDecodedBytes,total-available);
        const auto decodedHeadroom=includingCache>result.reserveBytes?includingCache-result.reserveBytes:0;
        result.decodedLimitBytes=std::min(result.decodedTargetBytes,roundedMiB(decodedHeadroom/2));
        result.payloadLimitBytes=std::min(result.payloadTargetBytes,roundedMiB(result.availableHeadroomBytes/4));
        const auto pressureWorkers=std::max<std::uint64_t>(1,result.availableHeadroomBytes/(256ull*MiB));
        if(pressureWorkers<result.featureWorkers){result.featureWorkers=static_cast<unsigned>(pressureWorkers);result.workersPressureLimited=true;result.reasons.push_back("available_memory_limits_worker_ceiling");}
        result.decodedPressureLimited=result.decodedLimitBytes<result.decodedTargetBytes;
        result.payloadPressureLimited=result.payloadLimitBytes<result.payloadTargetBytes;
        if(result.decodedPressureLimited)result.reasons.push_back("available_memory_limits_decoded_ram");
        if(result.payloadPressureLimited)result.reasons.push_back("available_memory_limits_payload_admission");
    }else result.reasons.push_back("memory_telemetry_unavailable_fixed_fallbacks");
    if(!overrides.nativeCacheEnabled){result.decodedLimitBytes=0;result.reasons.push_back("native_result_cache_disabled");}
    else if(!result.decodedTargetBytes)result.reasons.push_back("decoded_ram_disabled_by_override");
    if(!result.payloadLimitBytes){result.featureWorkers=1;result.reasons.push_back("no_parallel_payload_allowance_owner_fallback");}
    if(eligible!=std::numeric_limits<std::size_t>::max()){
        if(eligible<4){result.featureWorkers=1;result.reasons.push_back(eligible?"fewer_than_four_dirty_audited_tasks":"no_dirty_audited_tasks");}
        else if(eligible<result.featureWorkers){result.featureWorkers=static_cast<unsigned>(eligible);result.reasons.push_back("workers_clamped_to_dirty_audited_tasks");}
    }
    if(width!=std::numeric_limits<std::size_t>::max()&&width<result.featureWorkers){result.featureWorkers=static_cast<unsigned>(std::max<size_t>(1,width));result.reasons.push_back("workers_clamped_to_dependency_layer_width");}
    result.nativeThreadsPerWorker=std::min(overrides.nativeThreads.value_or(8u),std::max(1u,result.cpuThreads/result.featureWorkers));
    result.ownerNativeThreads=std::min(overrides.nativeThreads.value_or(8u),result.cpuThreads);
    result.decodedEntryLimit=static_cast<size_t>(std::clamp<std::uint64_t>(result.decodedLimitBytes/(2ull*MiB),128,4096));
    return result;
}
ResourcePolicy071D currentResourcePolicy071D(std::size_t eligible,std::uint64_t retained,std::size_t width){return selectResourcePolicy071D(probeResourceHardware071D(),resourceOverrides071D(),eligible,retained,width);}
arkjson::Value resourcePolicyReceipt071D(const ResourcePolicy071D& p){
    using V=arkjson::Value;V out=V::makeObj(),hardware=V::makeObj(),reasons=V::makeArr(),warnings=V::makeArr();
    out.set("schema",V::makeStr("ark.resource-policy.071d.v1"));
    hardware.set("logical_threads",V::makeNum(p.hardware.logicalThreads));hardware.set("physical_cores",V::makeNum(p.hardware.physicalCores));hardware.set("physical_cores_known",V::makeBool(p.physicalCoresKnown));hardware.set("total_ram_bytes",V::makeNum(double(p.hardware.totalRamBytes)));hardware.set("available_ram_bytes",V::makeNum(double(p.hardware.availableRamBytes)));hardware.set("memory_known",V::makeBool(p.memoryKnown));out.set("hardware",hardware);
    auto bytes=[&](const char* key,std::uint64_t value){out.set(key,V::makeNum(double(value)));};
    bytes("reserve_bytes",p.reserveBytes);bytes("available_headroom_bytes",p.availableHeadroomBytes);bytes("reclaimable_decoded_bytes",p.reclaimableDecodedBytes);
    bytes("decoded_target_bytes",p.decodedTargetBytes);bytes("decoded_limit_bytes",p.decodedLimitBytes);bytes("payload_target_bytes",p.payloadTargetBytes);bytes("payload_limit_bytes",p.payloadLimitBytes);
    out.set("cpu_threads",V::makeNum(p.cpuThreads));out.set("automatic_feature_workers",V::makeNum(p.defaultFeatureWorkers));out.set("requested_feature_workers",V::makeNum(p.requestedFeatureWorkers));out.set("feature_workers",V::makeNum(p.featureWorkers));out.set("native_threads_per_worker",V::makeNum(p.nativeThreadsPerWorker));out.set("owner_native_threads",V::makeNum(p.ownerNativeThreads));out.set("decoded_entry_limit",V::makeNum(double(p.decodedEntryLimit)));
    out.set("dirty_audited_tasks",p.eligibleTasks==std::numeric_limits<std::size_t>::max()?V{}:V::makeNum(double(p.eligibleTasks)));
    out.set("estimated_parallel_width",p.estimatedParallelWidth==std::numeric_limits<std::size_t>::max()?V{}:V::makeNum(double(p.estimatedParallelWidth)));
    out.set("decoded_override",V::makeBool(p.decodedOverride));out.set("payload_override",V::makeBool(p.payloadOverride));out.set("feature_override",V::makeBool(p.featureOverride));
    out.set("decoded_pressure_limited",V::makeBool(p.decodedPressureLimited));out.set("payload_pressure_limited",V::makeBool(p.payloadPressureLimited));out.set("workers_pressure_limited",V::makeBool(p.workersPressureLimited));
    for(const auto& reason:p.reasons)reasons.arr.push_back(V::makeStr(reason));for(const auto& warning:p.warnings)warnings.arr.push_back(V::makeStr(warning));out.set("reasons",reasons);out.set("warnings",warnings);
    out.set("allocation",V::makeStr("Demand-only ceilings; no RAM reservation. Pressure is sampled at use. Per-recompute worker admission, not a global CPU or process-RAM guarantee."));return out;
}
}
