#pragma once
#include "ark/agent.hpp"
namespace ark {
arkjson::Value inspectPhysics(AgentSession&,const arkjson::Value&);
arkjson::Value inspectMaterials(const Document&);
}
