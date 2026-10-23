#pragma once
#include "ark/json.hpp"
namespace ark {
// Read-only command palette. Recipes produce ordinary host tool arguments;
// execution, revisions, permissions and geometry validation stay with that tool.
arkjson::Value agentCommandCatalog(const arkjson::Value& args);
}
