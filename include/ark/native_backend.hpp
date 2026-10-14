#pragma once
#include "ark/json.hpp"
#include "ark/mesh.hpp"
namespace ark {
arkjson::Value nativeWorkerRequest(const arkjson::Value& request);
arkjson::Value nativeModelRequest(const arkjson::Value& request);
arkjson::Value nativeKernelInfo();
// Thread-local request allowance; returns previous value (0 inherits host setting).
int setNativeThreadBudget(int threads);
arkjson::Value exportNativeBrep(const std::string& brep,const std::string& path);
Mesh nativeResultMesh(const arkjson::Value& result);
}
