#pragma once

#include "ark/json.hpp"

namespace ark::occt {

// Build an exact, named multi-member tube frame from evaluated numeric input.
// Returns the same B-rep/report/mesh/topology contract as modelSolidNative,
// plus frame_report for cut list, joints and member endpoint frames.
arkjson::Value frameModelNative(const arkjson::Value& request);
// Exact end fit of an authored Tube or its rigid placement, without converting
// its authoring type into a ConstructionFrame.
arkjson::Value tubeEndFitNative(const arkjson::Value& request);

} // namespace ark::occt
