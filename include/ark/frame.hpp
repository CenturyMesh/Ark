#pragma once

#include "ark/model.hpp"

namespace ark {

// Evaluate one authored construction-frame program. The OCCT worker owns all
// solid geometry; the document stores only the named, expression-bearing
// profiles, nodes, members, and joints in Feature::nativeSpec.
Body evaluateFrameFeature(const Feature& feature, const ExprEnv& env,
                          Document& document, std::vector<Body>& preceding);
arkjson::Value applyFrameCommand(Document& document,const arkjson::Value& command);

} // namespace ark
