#pragma once
#include "ark/agent.hpp"
#include <set>
namespace ark {
// Retained immutable journal snapshots are replayed into private documents.
// Comparisons never restore/publish a document or edit the journal.
arkjson::Value compareDesignRevisions(const Document& source,u64 revision,const std::string& beforeId,const std::string& afterId="current");
arkjson::Value diagnoseDesignFeatures(const Document& source,u64 revision);
bool callDesignReviewTool(AgentSession&,const std::string&,const arkjson::Value&,arkjson::Value&);
void appendDesignReviewTools(arkjson::Value&);
}
