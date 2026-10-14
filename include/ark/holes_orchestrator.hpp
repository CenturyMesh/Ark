#pragma once
#include "ark/json.hpp"

namespace ark {
// Static, read-only engineering catalog. No network or native worker round trip.
arkjson::Value holesCatalog(const arkjson::Value& args);
arkjson::Value holesCatalogSchema();
arkjson::Value holesRecipeSchema();
void appendHolesTools(arkjson::Value& tools);
// Resolve an already expression-evaluated native request (recipe + numeric p
// fields flattened at request level). The returned plan is not a qualification.
arkjson::Value resolveHolesRecipe(const arkjson::Value& request);
}
