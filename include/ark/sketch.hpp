#pragma once
#include "ark/model.hpp"
#include <array>

namespace ark {
// Named construction references, independent of transient tessellation faces.
Mat4 resolvePlane(const Document& doc, const std::string& name);
void evaluateSketch(NamedSketch& sketch, const ExprEnv& env);
// Fills project entities from bodies that already exist. waiting means the
// source feature is later in the history. error is a hard failure.
struct DrivenSketchStatus { std::string error; bool waiting=false; };
DrivenSketchStatus bakeSketchProjects(Document& document, NamedSketch& sketch);
// Diagnose a solved (or attempted) sketch without mutating it. Rank and
// redundancy are local numerical properties of the current coordinates.
arkjson::Value diagnoseSketch(const Sketch& sketch, double residualTolerance=1e-6);
std::vector<Vec2> sampleSketchEntity(const SketchEnt& entity, double tolerance);
std::vector<std::array<Vec2,4>> sketchSplineSegments(const SketchEnt& entity);
const char* sketchEntityKindName(SketchEnt::Kind kind);
// dof index of a wire vertex x coordinate. Vertex 0 is the start.
int sketchVertexDof(const SketchEnt& entity, int vertex);
int sketchVertexCount(const SketchEnt& entity);
// Minor arc through two points. a1Deg-a0Deg is the signed sweep in degrees.
bool sketchMinorArc(Vec2 start, Vec2 end, double radius, int sweep, Vec2& center, double& a0Deg, double& a1Deg);
arkjson::Value measureSketchEntity(const Document& doc, const NamedSketch& sketch, const std::string& entity);
// Explicitly closed, simple, non-touching loops. No automatic closing/healing.
std::vector<SketchProfile> sketchProfiles(const Sketch& sketch, const Mat4& frame);
arkjson::Value inspectSketch(const Document& doc, const NamedSketch& sketch, bool samples=false);
}
