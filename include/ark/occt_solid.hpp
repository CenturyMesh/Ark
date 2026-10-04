#pragma once
#include "ark/json.hpp"

namespace ark::occt {
// Exact solid operations. Inputs are encoded OCCT B-reps, never display meshes.
arkjson::Value modelSolidNative(const arkjson::Value& request);
// Resolve a semantic topology handle against the exact B-rep that created it.
arkjson::Value referenceFrameNative(const arkjson::Value& request);
// Signed gap between two placed exact solids. Positive is clearance, negative is penetration.
arkjson::Value clearanceNative(const arkjson::Value& request);
// Signed distance from a point to an exact solid. Negative means the point is inside.
arkjson::Value pointDistanceNative(const arkjson::Value& request);
}
