#pragma once
#include "ark/agent.hpp"

namespace ark {
// Portable authored definitions, independent of the STEP snapshot library.
arkjson::Value componentSourceClosure(const Document&, const arkjson::Value&);
arkjson::Value makeParametricComponent(const Document&, const arkjson::Value&);
arkjson::Value loadParametricComponent(const std::string& path);
void saveParametricComponent(const arkjson::Value&, const std::string& path);
std::string parametricComponentHash(const arkjson::Value&);
void validateParametricComponents(const arkjson::Value&);
bool applyParametricComponentCommand(Document&, const arkjson::Value&);
void sealParametricComponentBindings(Document&);
bool callParametricComponentTool(AgentSession&, const std::string&, const arkjson::Value&, arkjson::Value&);
void appendParametricComponentTools(arkjson::Value&);
// Same authored relation index as context_graph, without its agent response cap.
arkjson::Value parametricGraphSnapshot(const Document&, u64 revision);
}
