#pragma once
#include "ark/json.hpp"
namespace ark::occt {
arkjson::Value modelSheetNative(const arkjson::Value& request);
arkjson::Value importSheetNative(const arkjson::Value& request);
arkjson::Value exportSheetNative(const arkjson::Value& request);
}
