#pragma once

#include "ark/json.hpp"

namespace ark::occt {

// Exact OCCT surface construction. Requests and results are JSON values at the
// worker boundary; failures throw and leave the owning document unchanged.
arkjson::Value modelSurfaceNative(const arkjson::Value& request);

} // namespace ark::occt
