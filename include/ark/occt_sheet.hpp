#pragma once
#include "ark/json.hpp"
namespace ark::occt {
// Exact authored panel/cylindrical-bend geometry, including bounded common
// miter contours, straight swept profiles and normal-thickness edge recipes.
// Every developed result checks whole-solid integrals and independent refold;
// manufacturing qualification is not inferred from these local proofs.
arkjson::Value modelSheetNative(const arkjson::Value& request);
arkjson::Value importSheetNative(const arkjson::Value& request);
arkjson::Value exportSheetNative(const arkjson::Value& request);
// Analytic mid-thickness flat contours and declared bend lines for authored panels.
arkjson::Value exportSheetFlatDxfNative(const arkjson::Value& request);
}
