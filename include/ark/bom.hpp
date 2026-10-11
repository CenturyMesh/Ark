#pragma once
#include "ark/model.hpp"
namespace ark {
// Caller-declared commercial/engineering data. Geometry never certifies it.
arkjson::Value validatePartMetadata(const arkjson::Value& value);
arkjson::Value partMetadataSchema();
std::string newBomDocumentId();
bool validBomDocumentId(const std::string& value);
arkjson::Value inspectBom(const Document& document,u64 revision);
arkjson::Value inspectBomPage(const Document& document,u64 revision,const arkjson::Value& args);
arkjson::Value exportBom(const Document& document,u64 revision,const arkjson::Value& args);
arkjson::Value fastenerRecipe(const Document& document,u64 revision,const arkjson::Value& args);
void appendBomTools(arkjson::Value& tools);
}
