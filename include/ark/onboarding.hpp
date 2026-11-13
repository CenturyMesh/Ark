#pragma once
#include "ark/agent.hpp"
namespace ark {
// Shared by provider instructions and static CAD guides.
const char* agentDesignBriefPolicy() noexcept;
arkjson::Value agentOnboard(const AgentSession&,const arkjson::Value&);
// Static documentation only: never bake a document snapshot or revision.
std::string agentOnboardingPack(const std::vector<std::string>& topics);
std::string agentInteractiveOnboardingPack();
// Basic editable CAD grammar available before a writer's first model request.
std::string agentAuthoringOnboardingPack();
}
