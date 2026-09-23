#pragma once
#include "ark/model.hpp"
#include <array>

namespace ark {
// Named construction references, independent of transient tessellation faces.
Mat4 resolvePlane(const Document& doc, const std::string& name);
void evaluateSketch(NamedSketch& sketch, const ExprEnv& env);
std::vector<Vec2> sampleSketchEntity(const SketchEnt& entity, double tolerance);
std::vector<std::array<Vec2,4>> sketchSplineSegments(const SketchEnt& entity);
// Explicitly closed, simple, non-touching loops. No automatic closing/healing.
std::vector<SketchProfile> sketchProfiles(const Sketch& sketch, const Mat4& frame);
arkjson::Value inspectSketch(const Document& doc, const NamedSketch& sketch, bool samples=false);
}
