#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
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
