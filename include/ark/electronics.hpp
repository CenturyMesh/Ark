#pragma once

#include "ark/model.hpp"

namespace ark {

// Converts an authored PCB or cable-route native program to bounded numeric
// data, validates design rules, and returns a compact inspection report.
// Throws std::runtime_error on a geometry or clearance violation. A PCB net
// can remain unrouted; its disconnected islands are reported, not healed.
arkjson::Value electronicsRequest(const Feature& feature,const ExprEnv& env);

// Preview fabrication files from a checked PCB program. Outputs individual
// copper Gerbers, board outline Gerber and plated/non-plated Excellon drills.
// This is a data export, not a fabrication release or CAM qualification.
arkjson::Value exportPcbFabrication(const Feature& feature,const ExprEnv& env,
                                   const std::string& pathPrefix);

} // namespace ark
