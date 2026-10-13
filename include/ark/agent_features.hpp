#pragma once
#include "ark/agent.hpp"
namespace ark {
bool callAgentFeature(AgentSession&,const std::string&,const arkjson::Value&,arkjson::Value&);
void appendAgentFeatureTools(arkjson::Value&);
// The advertised shape is shared by apply, preview and team authoring tools.
// Operation execution and measured postconditions remain the authority.
arkjson::Value agentPostconditionsSchema();
// Additive native fields for editable helix, loft alignment and sew intent.
arkjson::Value agentParametricNativeSchema();
arkjson::Value evaluatePostconditions(AgentSession&,const arkjson::Value& checks);
}
