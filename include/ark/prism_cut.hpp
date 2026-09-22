#pragma once
#include "ark/brep.hpp"
namespace ark {
// Validated sampled polygon topology; arithmetic is f64 and final mesh is f32.
// Supports full-depth disjoint interior openings in a rigidly placed extrusion.
// Returns false (without changing out) for non-prisms, nonparallel cutters or
// cuts which do not cover the entire thickness. Throws for invalid polygons,
// exterior/touching/nested openings, or failed mesh/volume validation.
bool tryPrismaticCut(const Body& base,const std::vector<SketchProfile>& cutters,double depth,Body& out);
}
