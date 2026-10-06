// ============================================================================
//  ARK-Point :: ark/model.hpp
//  Parametric engine: expression-evaluated parameters, feature-history DAG,
//  recompute pipeline, and a geometric constraint solver for sketches
//  (damped-least-squares / Levenberg-Marquardt over the constraint Jacobian).
//
//  Design goals: deterministic replay (agents), stable topological naming
//  (feature IDs persist; face IDs are currently regenerated), full history replay.
// ===========================================================================
#pragma once
#include "ark/core.hpp"
#include "ark/brep.hpp"
#include "ark/tube.hpp"
#include "ark/notch.hpp"
#include "ark/assembly.hpp"
#include <unordered_map>

namespace ark {

// ---------------------------------------------------------------------------
// Expression evaluator --------------------------------------------------------
// Grammar: expr := term (('+'|'-') term)* ; term := factor (('*'|'/') factor)*
//          factor := number | ident | ident '(' args ')' | '(' expr ')' | '-' factor | factor '^' factor
// Functions: sin cos tan sqrt abs min max pow floor ceil pi e
struct ExprEnv {
    std::unordered_map<std::string,f64> vars;
};
f64 evalExpr(const std::string& src, const ExprEnv& env, bool* ok=nullptr);

// ---------------------------------------------------------------------------
struct Parameter {
    std::string name;
    std::string expr;     // canonical expression ("10", "width/2", ...)
    f64 value = 0;
    bool depends(const std::string& var) const;
};

// ---------------------------------------------------------------------------
enum class FeatureType : i32 {
    Box, Cylinder, Sphere, Cone, Torus,
    Extrude, Revolve, Transform, Boolean, LinearPattern, ParametricSurface, Tube, NotchedTube, SourceTube, SketchCut,
    Loft, SurfaceFill, BSplineSurface, Thicken, SheetMetal, SourceSheet, NativeSolid,
    Sweep, ExtrudeSurface, RevolveSurface, OffsetSurface, Sew,
    MeshBody, MeshWeld, MeshSmooth, MeshDecimate, MeshOffset, MeshFill,
    Curve, IntersectionCurve, Bridge,
};

struct Feature {
    u64 id = 0;
    std::string name;
    FeatureType type{};
    std::unordered_map<std::string,std::string> p;   // expression params
    std::vector<u64> inputs;                          // parent feature ids
    std::vector<TubeBend> bends;
    std::vector<NotchPoint> notches;
    arkjson::Value sourceData;
    arkjson::Value nativeSpec; // authored native geometry; evaluated worker output is never serialized
    int opInt = 0;                                    // Boolean: 0 union 1 cut 2 intersect
    bool suppressed = false;                          // history and parameters stay; recompute skips the body

    std::string get(const char* k, const char* dflt="") const {
        auto it=p.find(k); return it==p.end()?dflt:it->second;
    }
    f64 num(const ExprEnv& env, const char* k, f64 dflt=0) const {
        auto it=p.find(k); if(it==p.end()) return dflt;
        bool ok=false; f64 v=evalExpr(it->second,env,&ok);
        return ok? v:dflt;
    }
};

// Evaluation status per feature
struct FeatureResult {
    u64 id=0;
    f64 ms=0;
    bool ok=true;
    std::string error;
    size_t tris=0;
};

// ---------------------------------------------------------------------------
// Sketches ----------------------------------------------------------------------
struct SketchEnt {
    enum Kind : i32 { Pnt, Line, Circle, Spline, Arc, Wire, Project, Offset } kind=Pnt;
    // dof layout: Pnt -> [x,y]; Line -> [x0,y0,x1,y1]; Circle -> [cx,cy,r]
    // Arc -> [cx,cy,r,a0deg,a1deg]. Wire flattens start, then each segment.
    // Project and offset store the same flattened polyline as a wire of lines.
    // Their coordinates are driven: the solver does not free them.
    struct WireSeg {
        enum Kind : i32 { Line, Arc, Cubic } kind=Line;
        int sweep=1; // +1 bulges left of the directed chord; the arc is the minor arc
    };
    std::vector<f64> dof;
    std::string name;
    std::vector<std::string> expressions; // empty: solver variable; string: parameter binding
    std::vector<WireSeg> segments;
    bool construction=false, closed=false;
    f64 tension=0;
    u64 sourceFeature=0;          // project: earlier feature id
    std::string reference;        // project: edge or planar face name
    std::string sourceName;       // offset: earlier entity in this sketch
    std::string distance;         // offset: signed millimetres, positive to the left
};
struct SketchCon {
    enum Type : i32 {
        FixedX, FixedY, Fixed,          // target point coords
        DistPP,                         // point-point distance
        Horizontal, Vertical,           // line axis-aligned
        Parallel, Perpendicular,        // line-line
        Radius,                         // circle radius
        Coincident,                     // point-point same location
        AngleLL,                        // line-line angle (rad)
        Tangent                         // line tangent to a circle or arc
    } type=Fixed;
    i32 a=-1,b=-1;      // entity indices (b optional)
    i32 pa=0,pb=0;      // sub-point index within entity when applicable
    f64 val=0;          // target value (Fixed: x target, Fixed also uses val2 for y)
    f64 val2=0;         // Fixed: y target
    std::string expression, expression2;
    std::string id;     // optional stable authoring identity; empty in older sketches
};

// A fillet or chamfer trims one wire corner. The sharp wire stays the solver
// authority; size is reapplied when the sketch evaluates.
struct SketchCorner {
    std::string entity;
    int corner=0;
    bool chamfer=false;
    std::string size;
    double solved=0;
};

struct Sketch {
    std::vector<SketchEnt> ents;
    std::vector<SketchCon> cons;
    std::vector<SketchCorner> corners;
    std::string plane="XY";
    f64 tolerance=.05;

    // helpers to build
    i32 addPoint(f64 x,f64 y){ SketchEnt e; e.kind=SketchEnt::Pnt; e.dof={x,y};
                                ents.push_back(std::move(e)); return (i32)ents.size()-1; }
    i32 addLine(f64 x0,f64 y0,f64 x1,f64 y1){ SketchEnt e; e.kind=SketchEnt::Line;
                                e.dof={x0,y0,x1,y1}; ents.push_back(std::move(e)); return (i32)ents.size()-1; }
    i32 addCircle(f64 cx,f64 cy,f64 r){ SketchEnt e; e.kind=SketchEnt::Circle;
                                e.dof={cx,cy,r}; ents.push_back(std::move(e)); return (i32)ents.size()-1; }
};

// Solve sketch constraints. Returns max |residual| after convergence and the
// number of LM iterations used.
struct SketchDiagnostics { int variables=0, rank=0; };
f64 solveSketch(Sketch& sk, int* itersOut=nullptr, int maxIters=200, SketchDiagnostics* diagnostics=nullptr);

// ---------------------------------------------------------------------------
// Feature type <-> stable string names (serialization / MCP / CLI)
const char* featureTypeName(FeatureType t);
bool featureTypeFromName(const std::string& s, FeatureType& out);
// Exact OCCT surface features, including loft, fill, NURBS, thicken and the
// later sweep/extrude/revolve/offset/sew operations.
bool isNativeSurfaceFeature(FeatureType t);
// Exact curve features: authored 3D wire, solid-plane section, and edge bridge.
bool isCurveFeature(FeatureType t);
// Sampled triangle operations. Their results are mesh authority, never a B-rep.
bool isMeshModelFeature(FeatureType t);

// Sketch constraint type <-> stable string names
const char* constraintTypeName(i32 t);
bool constraintTypeFromName(const std::string& s, i32& out);

// ---------------------------------------------------------------------------
// Named sketch: constraint-solved 2D profile geometry that features consume
// (Extrude/Revolve reference it by name via the "sketch" feature param).
struct NamedSketch {
    std::string name;
    Sketch      sk;
    f64         residual = 0;   // max |residual| after the last solve
    std::string error;
    int freeDofs=0, rank=0;
};

struct DatumPlane {
    std::string name, parent="XY";
    std::unordered_map<std::string,std::string> p;
    arkjson::Value support; // semantic feature reference, never tessellation indices
    std::string kind="plane"; // plane, axis (local Z), or full coordinate frame
};

// Display photo on a datum. The kernel stores the path and the rectangle; it
// does not decode pixels. Studio draws it. A missing file is not a model error.
struct PlaneCanvas {
    std::string name;
    std::string plane="XY";
    std::string path;
    std::string width="100";
    std::string height="100";
    double opacity=0.65;
};

class Document {
public:
    // parameters ------------------------------------------------------------
    std::vector<Parameter> params;
    void setParam(const std::string& name, const std::string& expr);
    f64  getParam(const std::string& name) const;
    bool removeParam(const std::string& name);
    std::string parameterError;
    void refreshParams();   // cascade-evaluate dependent expressions

    ExprEnv buildEnv() const {
        ExprEnv env;
        for(auto&p:params) env.vars[p.name]=p.value;
        return env;
    }

    // sketches ----------------------------------------------------------------
    // Solved once per recompute, before any feature evaluates.
    std::vector<NamedSketch> sketches;
    std::vector<DatumPlane> planes;
    std::vector<PlaneCanvas> canvases;
    NamedSketch* findSketch(const std::string& name);
    void setSketch(const std::string& name, Sketch sk);   // create or replace
    bool removeSketch(const std::string& name);

    // features ----------------------------------------------------------------
    std::vector<Feature> features;
    u64 addFeature(Feature f);                    // returns id
    Feature* find(u64 id);
    bool     remove(u64 id);
    bool     reorderMove(u64 id, int delta);      // shift position in history

    // evaluation --------------------------------------------------------------
    mutable std::vector<Body> evalBodies;                 // last recompute output
    std::vector<Body>* evaluatingBodies=nullptr; // transient earlier history during recompute
    std::vector<FeatureResult> lastResults;
    f64 recomputeMs = 0;
    TessOptions tessOpt{};

    // Recompute full history. Bodies accumulate: each feature's result body
    // replaces/extends the working set per its semantics.
    const std::vector<FeatureResult>& recompute();

    size_t totalTris() const {
        size_t n=0;
        for(auto&b:evalBodies) n+=b.tessellate(tessOpt).triCount();
        return n;
    }

    std::vector<AssemblyNode> assembly;
    std::vector<AssemblyMate> mates;
    u64 nextMateId_=1;
    u64 nextAssemblyId_=1;
    u64 nextFeatureId_=1;
};

} // namespace ark
