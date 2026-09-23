#pragma once
#include "ark/model.hpp"
#include <array>

namespace ark {
// Named construction references, independent of transient tessellation faces.
Mat4 resolvePlane(const Document& doc, const std::string& name);
void evaluateSketch(NamedSketch& sketch, const ExprEnv& env);
// Diagnose a solved (or attempted) sketch without mutating it. Rank and
// redundancy are local numerical properties of the current coordinates.
arkjson::Value diagnoseSketch(const Sketch& sketch, double residualTolerance=1e-6);
std::vector<Vec2> sampleSketchEntity(const SketchEnt& entity, double tolerance);
std::vector<std::array<Vec2,4>> sketchSplineSegments(const SketchEnt& entity);
// Explicitly closed, simple, non-touching loops. No automatic closing/healing.
std::vector<SketchProfile> sketchProfiles(const Sketch& sketch, const Mat4& frame);
arkjson::Value inspectSketch(const Document& doc, const NamedSketch& sketch, bool samples=false);
}
