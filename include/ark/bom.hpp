#pragma once
#include "ark/model.hpp"
namespace ark {
// Caller-declared commercial/engineering data. Geometry never certifies it.
arkjson::Value validatePartMetadata(const arkjson::Value& value);
arkjson::Value patchPartMetadata(const arkjson::Value& current,const arkjson::Value& patch);
arkjson::Value partMetadataSchema();
std::string newBomDocumentId();
bool validBomDocumentId(const std::string& value);
arkjson::Value inspectBom(const Document& document,u64 revision);
arkjson::Value selectBomDefinitions(const Document&,const arkjson::Value& full,const arkjson::Value& ids);
arkjson::Value inspectBomPage(const Document& document,u64 revision,const arkjson::Value& args);
arkjson::Value exportBom(const Document& document,u64 revision,const arkjson::Value& args);
class AgentSession;
arkjson::Value exportBomParts(AgentSession& session,const arkjson::Value& args);
arkjson::Value fastenerRecipe(const Document& document,u64 revision,const arkjson::Value& args);
void appendBomTools(arkjson::Value& tools);
}
