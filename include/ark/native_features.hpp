#pragma once

#include "ark/model.hpp"

namespace ark {

bool isNativeFeature(FeatureType type);
// Private inspection callers use the same authoritative loft profile resolver.
arkjson::Value surfaceRequestForInspection068B(Document&,const Feature&);
Body evaluateNativeFeature(const Feature& feature, const ExprEnv& env,
                           Document& document, std::vector<Body>& preceding);
// Materialize exact geometry from a prior feature's analytic parameters only
// when a native operation or semantic reference needs it.
void ensureNativeBody(Document& document, u64 feature, std::vector<Body>& bodies);
arkjson::Value sketchEntityNativeProfile(Document& document,const std::string& sketch,const std::string& entity);
// Read-only native planar region census for human extrusion selection.
arkjson::Value sketchNativeRegions073B(Document& document,const std::string& sketch);
Body evaluateExactLegacyOperation(const Feature& feature, const ExprEnv& env,
                                  Document& document, std::vector<Body>& preceding);
bool validateImportedComponentDefinition(const Feature& feature,std::string& error);
// 046B: an authored native.regions selection on an extrude, extrude_surface,
// revolve_surface or sweep. Multi-loop sketches without it keep their format.
bool profileRegions046BNeedsVersion48(const Document& document);
// Check stored hashes and any still-present library-owned source even when
// geometry comes from an evaluation cache. Does not regenerate display data.
void validateNativeComponentCustody(const Document& document);
// Read-only exact B-rep pair inspection in two explicit rigid world frames.
// Uses the fixed native 1e-6 mm contact tolerance; never modifies either body.
arkjson::Value inspectNativeCollision(const Body& a,const Mat4& aWorld,
                                     const Body& b,const Mat4& bWorld,double tolerance=1e-6);

} // namespace ark
