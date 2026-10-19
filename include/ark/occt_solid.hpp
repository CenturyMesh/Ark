#pragma once
#include "ark/json.hpp"

namespace ark::occt {
// Exact solid operations. Inputs are encoded OCCT B-reps, never display meshes.
arkjson::Value modelSolidNative(const arkjson::Value& request);
// A whole 2..16-input union/cut/common in one request; same result contract
// as the final pairwise native_solid step.
arkjson::Value booleanSequenceNative(const arkjson::Value& sequence);
// Select one original STEP solid without reconstructing authoring history.
arkjson::Value importComponentNative(const arkjson::Value& request);
arkjson::Value componentCustodyNative(const arkjson::Value& package);
// Resolve a semantic topology handle against the exact B-rep that created it.
arkjson::Value referenceFrameNative(const arkjson::Value& request);
// Resolve a digest-bound semantic face without requiring an analytic frame.
int semanticFaceIndexNative(const arkjson::Value& request);
// Exact separation and overlap evidence; negative legacy values are estimates.
arkjson::Value clearanceNative(const arkjson::Value& request);
arkjson::Value geometryQueryNative(const arkjson::Value& request);
arkjson::Value geometryReferenceShapeNative(const arkjson::Value& request);
// Signed distance from a point to an exact solid. Negative means the point is inside.
arkjson::Value pointDistanceNative(const arkjson::Value& request);
// Volume centroid of one exact solid, in the solid's local millimetre frame.
arkjson::Value solidCentroidNative(const arkjson::Value& request);
arkjson::Value measurementPickNative(const arkjson::Value& request);
}
