#pragma once

#include "ark/json.hpp"
#include <string>

namespace ark::occt {

// These functions run inside the bounded native STEP worker. They throw on an
// unsupported source or failed independent B-rep validation.
arkjson::Value importSourceTubeNative(const std::string& sourcePath,
                                      const std::string& cacheDirectory);
arkjson::Value exportSourceTubeNative(const arkjson::Value& package,
                                      const std::string& target);
arkjson::Value exportNotchNative(const arkjson::Value& plan,
                                 const std::string& target);

} // namespace ark::occt
