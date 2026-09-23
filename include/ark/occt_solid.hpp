#pragma once
#include "ark/json.hpp"

namespace ark::occt {
// Exact solid operations. Inputs are encoded OCCT B-reps, never display meshes.
arkjson::Value modelSolidNative(const arkjson::Value& request);
// Resolve a semantic topology handle against the exact B-rep that created it.
arkjson::Value referenceFrameNative(const arkjson::Value& request);
}
