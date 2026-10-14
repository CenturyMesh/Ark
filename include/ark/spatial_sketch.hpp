#pragma once
#include "ark/model.hpp"

namespace ark {
struct SpatialSketchSolution {
    arkjson::Value native;
    arkjson::Value diagnostics;
};
// Solves authored point handles without replacing source coordinates/expressions.
// Numeric coordinates are free; expressions and datum anchors remain driven.
// The result is a worker request, never the persisted authoring source.
SpatialSketchSolution solveSpatialCurve(const arkjson::Value& authored,
                                        const Document& document);
}
