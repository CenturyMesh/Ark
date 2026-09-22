#pragma once
#include "ark/json.hpp"
namespace ark {
// Ideal rigid-half kinematics only. Does not certify deformation of a finite wall.
arkjson::Value planNotchedBlank(const arkjson::Value& request);
}
