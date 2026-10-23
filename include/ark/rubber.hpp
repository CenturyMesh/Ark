#pragma once
#include "ark/agent.hpp"

namespace ark {
// Authored exact profiles, procedural decoration and derived dynamics are separate contracts.
arkjson::Value rubberCapabilities();
arkjson::Value rubberPlanSchema();
arkjson::Value rubberStudySchema();
arkjson::Value planRubber(AgentSession&, const arkjson::Value&);
arkjson::Value inspectRubber(AgentSession&, const arkjson::Value&);
}
