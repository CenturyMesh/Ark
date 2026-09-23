#pragma once
#include "ark/json.hpp"
#include "ark/mesh.hpp"
namespace ark {
arkjson::Value nativeWorkerRequest(const arkjson::Value& request);
arkjson::Value nativeModelRequest(const arkjson::Value& request);
arkjson::Value nativeKernelInfo();
arkjson::Value exportNativeBrep(const std::string& brep,const std::string& path);
Mesh nativeResultMesh(const arkjson::Value& result);
}
