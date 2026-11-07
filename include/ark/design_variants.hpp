#pragma once
#include "ark/agent.hpp"
namespace ark {
// Saved configurations contain authored parameter values, never cached geometry.
void validateDesignVariants(const arkjson::Value&);
bool applyDesignVariantCommand(Document&,const arkjson::Value&);
bool callDesignVariantsTool(AgentSession&,const std::string&,const arkjson::Value&,arkjson::Value&);
void appendDesignVariantsTools(arkjson::Value&);
}
