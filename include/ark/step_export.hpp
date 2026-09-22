#pragma once
#include "ark/json.hpp"
namespace ark {
arkjson::Value importSourceTubeStep(const std::string& path);
arkjson::Value exportSourceTubeStep(const arkjson::Value& package,const std::string& path);
arkjson::Value exportNotchStep(const arkjson::Value& request,const std::string& path);
}
