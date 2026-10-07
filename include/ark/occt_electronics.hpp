#pragma once
#include "ark/json.hpp"

namespace ark::occt {
// Builds exact OCCT solids from a checked numeric electronicsRequest.
arkjson::Value modelElectronicsNative(const arkjson::Value& request);
}
