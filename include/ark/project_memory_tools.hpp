#pragma once
#include "ark/agent.hpp"
#include "ark/project_memory.hpp"
#include <functional>
namespace ark {
bool callProjectMemoryTool(AgentSession&,const std::string&,const arkjson::Value&,arkjson::Value&);
void appendProjectMemoryTools(arkjson::Value&);
std::string defaultProjectMemoryDirectory();
ProjectMemoryConfig localProjectMemoryConfig(const std::string& root);
int projectMemoryCli(int argc,char** argv);
void captureProjectMemoryEpisode(AgentSession&,const std::string&,const arkjson::Value&,arkjson::Value&);
// Provider-independent startup recall. Retrieved text is an attributed data
// message, separate from static onboarding instructions and original user input.
arkjson::Value projectMemoryAssistantInput(const arkjson::Value&,const arkjson::Value&,
    const std::function<arkjson::Value(const std::string&,const arkjson::Value&)>&);
}
