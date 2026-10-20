#pragma once
#include "ark/model.hpp"

namespace ark {
// 044B curve patterns: chained/whole-wire paths, pitch spacing, frames and the
// compact reference scheme. Legacy single-reference specs are not 044B specs.
bool curvePattern044BSpec(const Feature& feature);
bool curvePattern044BNeedsVersion47(const Document& document);
// Adds the optional third support input and an exact cable centerline path to
// the resolved native request. The support body is a reference, not consumed.
void curvePatternHostInputs044B(const Feature& feature,Document& document,std::vector<Body>& preceding,const Body& path,arkjson::Value& model);
// pattern_plan mode solid: copies the 044B fields into the native payload.
void curvePatternAgentFields044B(const arkjson::Value& args,arkjson::Value& native);
}
