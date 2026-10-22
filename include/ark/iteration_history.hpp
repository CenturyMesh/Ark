#pragma once
#include "ark/json.hpp"
#include <string>
#include <cstddef>
#include <functional>
#include <map>
#include <cstdint>
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
// 049A: count of inline 044B-048B baked-frame payload references in a journal and their unique bytes.
void iterationInlineBakePayloads(const arkjson::Value& history,size_t& references,size_t& bytes);
// 049A explicit journal rewrite (schema 2): maps every retained authoring state through
// transform and rebuilds deltas, the blob dictionary, digests and node ids with the same
// topology and node metadata. ids maps every old node and baseline id to its new id.
arkjson::Value iterationRewrite(const arkjson::Value& history,const std::function<void(arkjson::Value& state)>& transform,std::map<std::string,std::string>& ids);
// Explicit archive-backed compaction. Retains the current-head ancestor suffix;
// undo keeps only its contiguous retained targets, redo is cleared. No auto pruning.
arkjson::Value iterationCompact(const Document& source,size_t keepRecent,const arkjson::Value& receipt);
arkjson::Value historyCompactTool(Document& document,std::uint64_t& revision,const arkjson::Value& args);
}
