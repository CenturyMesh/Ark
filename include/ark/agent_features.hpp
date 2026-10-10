#pragma once
#include "ark/agent.hpp"
namespace ark {
bool callAgentFeature(AgentSession&,const std::string&,const arkjson::Value&,arkjson::Value&);
void appendAgentFeatureTools(arkjson::Value&);
arkjson::Value evaluatePostconditions(AgentSession&,const arkjson::Value& checks);
}
