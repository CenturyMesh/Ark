#pragma once
#include "ark/agent.hpp"
namespace ark {
bool callAuthoring023Tool(AgentSession&, const std::string&, const arkjson::Value&, arkjson::Value&);
void appendAuthoring023Tools(arkjson::Value&);
}
