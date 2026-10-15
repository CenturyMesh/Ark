#pragma once
#include "ark/model.hpp"
#include <set>

namespace ark {
// The history tip includes the selected feature. Zero exposes the complete
// authored history. Inactive recipes retain their IDs and suppression state.
bool featureWithinHistory(const Document& document, u64 feature);
std::set<u64> activeFeatureIds(const Document& document);
std::set<u64> consumedFeatureInputs(const Document& document);
bool applyFeatureHistoryCommand(Document& document, const arkjson::Value& command);
// Structural validation includes named sketch/datum/curve dependencies, even
// when their retained definitions are outside the current evaluation scope.
void validateFeatureHistoryOrder(const Document& document);
arkjson::Value inspectFeatureHistory(const Document& document);
}
