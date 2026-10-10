#pragma once
#include "ark/agent.hpp"
namespace ark {
bool callComponentLibraryTool(AgentSession& session,const std::string& tool,
    const arkjson::Value& args,arkjson::Value& out);
void appendComponentLibraryTools(arkjson::Value& tools);
}
