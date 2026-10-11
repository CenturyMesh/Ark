#pragma once
#include "ark/json.hpp"
#include <cstdint>
#include <memory>
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
// Host measured outcome; ratings remain separately attributed agent statements.
arkjson::Value recordAgentActivity(Document&,const std::string& tool,const arkjson::Value& args,const arkjson::Value& actor,arkjson::Value result,std::uint64_t beforeRevision,std::uint64_t afterRevision,double elapsedMs);
arkjson::Value inspectAgentActivity(const Document&,const arkjson::Value& args);
arkjson::Value rateAgentActivity(Document&,const arkjson::Value& args,const arkjson::Value& actor);
void appendAgentActivityTools(arkjson::Value& tools);
}
