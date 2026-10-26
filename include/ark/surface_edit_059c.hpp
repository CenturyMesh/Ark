#pragma once
#include "ark/agent.hpp"

namespace ark {
arkjson::Value surfaceControlSchema059();
// Read-only exact inspection and explicit revision-bound SurfaceEdit plans.
arkjson::Value surfaceControl059(AgentSession&, const arkjson::Value&);
}
