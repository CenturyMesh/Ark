#pragma once
#include "ark/model.hpp"

namespace ark {
// Process settings are caller-authored intent. They never certify a process or
// manufacture dimensions from display tessellation.
bool isManufacturingRoute(const std::string& route);
bool applyManufacturingIntent(Feature& feature,const arkjson::Value& intent,std::string& error);
bool validateManufacturingProcess(const Feature& feature,std::string& error);
arkjson::Value inspectManufacturing(const Document& document,const arkjson::Value& args,u64 revision=0);
}
