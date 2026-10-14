#pragma once
#include "ark/model.hpp"

namespace ark {
// Read-only C.03 screening. Inputs retain caller authority, evaluated B-reps
// retain geometric authority, and compatible rules never qualify manufacture.
arkjson::Value inspectDfm(const Document& document,const Feature& feature);
}
