#pragma once

#include "ark/model.hpp"

namespace ark {

bool isNativeFeature(FeatureType type);
Body evaluateNativeFeature(const Feature& feature, const ExprEnv& env,
                           Document& document, std::vector<Body>& preceding);
// Materialize exact geometry from a prior feature's analytic parameters only
// when a native operation or semantic reference needs it.
void ensureNativeBody(Document& document, u64 feature, std::vector<Body>& bodies);
Body evaluateExactLegacyOperation(const Feature& feature, const ExprEnv& env,
                                  Document& document, std::vector<Body>& preceding);
bool validateImportedComponentDefinition(const Feature& feature,std::string& error);
// Check stored hashes and any still-present library-owned source even when
// geometry comes from an evaluation cache. Does not regenerate display data.
void validateNativeComponentCustody(const Document& document);

} // namespace ark
