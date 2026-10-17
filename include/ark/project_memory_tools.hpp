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
// Provider-independent startup recall. Retrieved text is an actual host tool
// call/result pair, separate from onboarding instructions and user input.
arkjson::Value projectMemoryAssistantInput(const arkjson::Value&,const arkjson::Value&,
    const std::function<arkjson::Value(const std::string&,const arkjson::Value&)>&);
// Recognizes only the exact host startup shape and content-derived identity.
bool isProjectMemoryAssistantPair(const arkjson::Value& call,const arkjson::Value& output);
// Request-only recovery of the 039D overlong host IDs. Retained history is not
// modified, and IDs originating outside an exact host pair are never rewritten.
arkjson::Value projectMemoryAssistantWireInput(const arkjson::Value&);
}
