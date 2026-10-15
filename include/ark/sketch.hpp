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
// Empty plane includes source XYZ unchanged. Result has exact B-rep plus world display polylines.
arkjson::Value projectGeometry(Document& document, u64 feature, const std::string& reference,
    const std::string& plane="", u64 sourceInstance=0, int sourceCopy=0,
    const std::string& mode="normal", double displayTolerance=.025);
arkjson::Value projectSketchGeometry(Document& document,const std::string& sourceSketch,
    const std::string& sourceEntity,const std::string& plane="",const std::string& mode="normal",double displayTolerance=.025);
// Diagnose a solved (or attempted) sketch without mutating it. Rank and
// redundancy are local numerical properties of the current coordinates.
arkjson::Value diagnoseSketch(const Sketch& sketch, double residualTolerance=1e-6);
// Read-only, bounded single-deletion probes. Alternatives require explicit
// caller review and a source/revision-bound ordinary document transaction.
arkjson::Value proposeSketchConstraintRepairs(const Sketch& sketch, double residualTolerance=1e-6);
struct SketchLinearization { std::vector<double> residuals; std::vector<size_t> freeSlots; std::vector<std::vector<double>> jacobianColumns; };
SketchLinearization linearizeSketch(const Sketch& sketch);
struct SketchCurve {
    enum Kind { Line, Arc } kind=Line;
    Vec2 a{}, b{}, center{};
    double radius=0, a0=0, a1=0;
    int corner=-1;
};
// Sharp corners stay in the wire. This is the trimmed presentation shared by
// the display sample and the exact profile.
std::vector<SketchCurve> sketchPresentedCurves(const Sketch& sketch, const SketchEnt& entity);
std::vector<Vec2> sampleSketchEntity(const SketchEnt& entity, double tolerance, const Sketch* owner=nullptr);
// Separate paths preserve disconnected text engraving strokes for display.
std::vector<std::vector<Vec2>> sampleSketchEntityPaths(const SketchEnt& entity, double tolerance, const Sketch* owner=nullptr);
std::vector<std::array<Vec2,4>> sketchSplineSegments(const SketchEnt& entity);
const char* sketchEntityKindName(SketchEnt::Kind kind);
// dof index of a wire vertex x coordinate. Vertex 0 is the start.
int sketchVertexDof(const SketchEnt& entity, int vertex);
int sketchVertexCount(const SketchEnt& entity);
// Minor arc through two points. a1Deg-a0Deg is the signed sweep in degrees.
bool sketchMinorArc(Vec2 start, Vec2 end, double radius, int sweep, Vec2& center, double& a0Deg, double& a1Deg);
// Exact analytic arc recipes shared by planar sketches and spatial native
// curves. Sweep follows the start tangent, including arcs greater than pi.
struct AuthoredArc {
    Vec3 center{}, normal{}, xAxis{};
    double radius=0, sweep=0;
    Vec3 point(double fraction) const;
    Vec3 tangent(double fraction) const;
};
AuthoredArc arcThroughThreePoints(Vec3 start, Vec3 through, Vec3 end);
AuthoredArc arcFromStartTangent(Vec3 start, Vec3 end, Vec3 tangent);
// Checks the bounded typed DTO; expressions are evaluated by evaluateSketch.
bool validateSketchArcRecipe(const arkjson::Value& recipe, std::string& error);
bool validateSketchEntityRecipe(const arkjson::Value& recipe, std::string& error);
void evaluateSketchEntityRecipe(SketchEnt& entity, const ExprEnv& env, double displayTolerance);
arkjson::Value sketchRecipeDefaults(const std::string& type);
std::vector<std::string> sketchRecipeTypes();
arkjson::Value measureSketchEntity(const Document& doc, const NamedSketch& sketch, const std::string& entity);
// Explicitly closed, simple, non-touching loops. No automatic closing/healing.
std::vector<SketchProfile> sketchProfiles(const Sketch& sketch, const Mat4& frame);
// Display regions only: ignore open/branched components, triangulate closed
// simple loops with nested holes. Never used as exact modeling authority.
Mesh sketchRegionMesh(const Sketch& sketch, const Mat4& frame);
// Retains equation source in the feature; returns bounded interpolation samples.
arkjson::Value sampleEquationCurve(const arkjson::Value& spec,const ExprEnv& env);
arkjson::Value inspectSketch(const Document& doc, const NamedSketch& sketch, bool samples=false);
}
