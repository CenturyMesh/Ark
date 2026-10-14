#pragma once
#include "ark/model.hpp"

namespace ark {
// Read-only exact exchange inspection. STEP/IGES source bytes never become meshes.
arkjson::Value inspectCadFile(const std::string& path);
arkjson::Value inspectImportedCad(const Feature& feature,const Body* candidate=nullptr);
// Returns one revision-independent feature.add command and measured evidence.
// Publication is the caller's atomic revision-checked doc_apply responsibility.
arkjson::Value buildCadImport(const std::string& path,const std::string& name="");
std::string cadImportArkitfyPrompt(const arkjson::Value& receipt,u64 featureId);
// Cheap authoring contract; the native worker validates both embedded digests
// and reproduces the exact exchange transfer before publishing geometry.
bool validateCadPackageAuthoring(const Feature& feature,std::string& error);
}
