#pragma once
#include "ark/json.hpp"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace ark {
class Document;
struct AgentActivityLedger {
    std::string id;
    std::uint64_t serial=0,unrecordedActions=0;
    std::vector<arkjson::Value> entries;
};
arkjson::Value agentActivityJson(const std::shared_ptr<AgentActivityLedger>&);
std::shared_ptr<AgentActivityLedger> readAgentActivity(const arkjson::Value&);
std::uint64_t agentActivitySerial(const Document&);
// True for a rejected call, a query_batch with a failed row or a rejected preview candidate.
bool agentActivityFailed(const std::string& tool,const arkjson::Value& result);
// Host measured outcome; ratings remain separately attributed agent statements.
arkjson::Value recordAgentActivity(Document&,const std::string& tool,const arkjson::Value& args,const arkjson::Value& actor,arkjson::Value result,std::uint64_t beforeRevision,std::uint64_t afterRevision,double elapsedMs);
arkjson::Value inspectAgentActivity(const Document&,const arkjson::Value& args);
// A tool_feedback batch is atomic: one invalid item records nothing. The error
// names every offending activity_id; detail adds rejected_ratings, the IDs that
// are already rated and the actor's current pending IDs/count for resync.
struct AgentActivityFeedbackError : std::runtime_error {
    arkjson::Value detail;
    AgentActivityFeedbackError(const std::string& message,arkjson::Value value):std::runtime_error(message),detail(std::move(value)){}
};
// Receipts carry pending_feedback_ids (newest first, at most 64) and the
// untruncated pending_feedback_count for the calling actor.
arkjson::Value rateAgentActivity(Document&,const arkjson::Value& args,const arkjson::Value& actor);
void appendAgentActivityTools(arkjson::Value& tools);
}
