#pragma once
#include "ark/core.hpp"
#include "ark/json.hpp"
namespace ark {
enum class ComputeMode {Auto,CPU,GPU};
// Packed display/export positions only. Rigid placements remain f64 on CPU.
// GPU mode reports failure; Auto transparently uses the same CPU operation when unavailable.
bool transformPacked(const std::vector<f32>& source,std::vector<f32>& output,const Mat4&,ComputeMode mode=ComputeMode::Auto);
arkjson::Value computeInfo();
arkjson::Value benchmarkCompute(size_t vertices=1048576,int repeats=5);
}
