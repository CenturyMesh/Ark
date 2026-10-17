#pragma once
#include "ark/model.hpp"

namespace ark {
// Recipes stay in DatumPlane::support, retaining source names and expressions.
// Throws for unknown fields, malformed anchors and incompatible display kinds.
void validateDatumDefinition(const DatumPlane& datum);
// Preserve the identity of a retained authored Origin across atomic edits.
void validateDatumTransition(const Document& before,const Document& after);
// Shared semantic endpoint resolver for associative spatial-curve authoring.
Vec3 datumAnchorPoint(const Document& document, const arkjson::Value& anchor);
// Stages a full replacement/removal against an explicitly observed revision.
// Recomputes and rejects every invalid dependency; caller publishes the returned
// document atomically, journals it and advances its session revision once.
Document stageDatumSet(const Document& document, const DatumPlane& datum,
                       u64 expectedRevision, u64 currentRevision);
Document stageDatumRemove(const Document& document, const std::string& name,
                          u64 expectedRevision, u64 currentRevision);
arkjson::Value inspectDatum(const Document& document, const std::string& name);
// Recursively bind feature-name anchors to stable producer IDs. Never rewrites
// datum/sketch/entity names or catches an ambiguous/missing producer.
void bindDatumSupportProducers(const Document& document, arkjson::Value& support);
}
