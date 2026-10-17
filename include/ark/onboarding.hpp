#pragma once
#include "ark/agent.hpp"
namespace ark {
arkjson::Value agentOnboard(const AgentSession&,const arkjson::Value&);
// Static documentation only: never bake a document snapshot or revision.
std::string agentOnboardingPack(const std::vector<std::string>& topics);
// Basic editable CAD grammar available before a writer's first model request.
std::string agentAuthoringOnboardingPack();
}
