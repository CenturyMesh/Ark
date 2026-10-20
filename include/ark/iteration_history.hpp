#pragma once
#include "ark/json.hpp"
#include <string>
#include <cstddef>
namespace ark {
class Document;
inline constexpr size_t maxDocumentBytes=4u*1024u*1024u;
inline constexpr size_t maxIterationNodes=512;
// Caller metadata is declared provenance, never an authenticated identity.
arkjson::Value iterationActor(const arkjson::Value& args);
arkjson::Value iterationAuthoring(const Document& document);
std::string iterationSha256(const std::string& bytes);
void iterationAppend(Document& after,const Document& before,const std::string& tool,const arkjson::Value& args,const std::string& restoredFrom={});
arkjson::Value iterationInfo(const Document& document,size_t limit=64,const std::string& beforeId={});
void iterationRestore(const Document& source,const std::string& id,Document& target);
std::string iterationUndoTarget(const Document& document,bool redo);
void iterationPersist(arkjson::Value& root,const Document& document);
bool iterationExpand(arkjson::Value& root,arkjson::Value& history,std::string& error);
bool iterationValidate(const Document& document,const arkjson::Value& history,std::string& error);
}
