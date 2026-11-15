#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
// Approximate driver telemetry controls only idle display buffers. Never
// preallocates VRAM or evicts live meshes; exact geometry is independent.
inline std::size_t displayIdleBudget070(std::uint64_t dedicatedKB,std::uint64_t freeKB,std::size_t requestedMB=256){
    constexpr std::uint64_t MiB=1024*1024;
    if(requestedMB>1024)requestedMB=1024;
    if(!dedicatedKB)return (std::size_t)std::min<std::uint64_t>(32,requestedMB)*MiB;
    const auto capacityCap=dedicatedKB*1024/32;
    const auto available=freeKB*1024;
    const auto pressureCap=available>256*MiB?(available-256*MiB)/4:0;
    return (std::size_t)std::min<std::uint64_t>({requestedMB*MiB,capacityCap,pressureCap});
}
// Approximate free VRAM already excludes retained buffers. Reclaim idle storage
// before a new allocation needs headroom, without evicting any live owner.
inline std::size_t displayUploadIdleBudget078(std::uint64_t dedicatedKB,std::uint64_t freeKB,std::size_t incomingBytes,std::size_t idleBytes,std::size_t idleLimit){
    constexpr std::uint64_t reserve=256ull*1024*1024;
    if(!dedicatedKB)return incomingBytes>=idleLimit?0:idleLimit-incomingBytes;
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    const auto available=freeKB>maximum/1024?maximum:freeKB*1024;
    const auto recoverable=available>maximum-idleBytes?maximum:available+idleBytes;
    const auto safe=recoverable>reserve?recoverable-reserve:0;
    return (std::size_t)std::min<std::uint64_t>(idleLimit,safe>incomingBytes?safe-incomingBytes:0);
}
