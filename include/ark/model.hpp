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
    Extrude, Revolve, Transform, Boolean, LinearPattern, ParametricSurface, Tube, NotchedTube, SourceTube,
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
    int opInt = 0;                                    // Boolean: 0 union 1 cut 2 intersect

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
    enum Kind : i32 { Pnt, Line, Circle } kind=Pnt;
    // dof layout: Pnt -> [x,y]; Line -> [x0,y0,x1,y1]; Circle -> [cx,cy,r]
    std::vector<f64> dof;
};
struct SketchCon {
    enum Type : i32 {
        FixedX, FixedY, Fixed,          // target point coords
        DistPP,                         // point-point distance
        Horizontal, Vertical,           // line axis-aligned
        Parallel, Perpendicular,        // line-line
        Radius,                         // circle radius
        Coincident,                     // point-point same location
        AngleLL                         // line-line angle (rad)
    } type=Fixed;
    i32 a=-1,b=-1;      // entity indices (b optional)
    i32 pa=0,pb=0;      // sub-point index within entity when applicable
    f64 val=0;          // target value (Fixed: x target, Fixed also uses val2 for y)
    f64 val2=0;         // Fixed: y target
};

struct Sketch {
    std::vector<SketchEnt> ents;
    std::vector<SketchCon> cons;

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
f64 solveSketch(Sketch& sk, int* itersOut=nullptr, int maxIters=200);

// ---------------------------------------------------------------------------
// Feature type <-> stable string names (serialization / MCP / CLI)
const char* featureTypeName(FeatureType t);
bool featureTypeFromName(const std::string& s, FeatureType& out);

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
    std::vector<Body> evalBodies;                 // last recompute output
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
    u64 nextAssemblyId_=1;
    u64 nextFeatureId_=1;
};

} // namespace ark
