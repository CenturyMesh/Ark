#pragma once
#include "ark/agent.hpp"
namespace ark {
bool callAuthoring024Tool(AgentSession&,const std::string&,const arkjson::Value&,arkjson::Value&);
void appendAuthoring024Tools(arkjson::Value&);
}
