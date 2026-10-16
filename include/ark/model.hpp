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
    ConstructionFrame, PCB, CableRoute, Suspension, SurfaceEdit,
    MeshEdit, MeshNet,
};

// Appearance describes the display surface. Density is grams per cubic
// centimetre; zero means the material has no physical density assigned.
struct Appearance {
    Vec3 baseColor{0.72,0.75,0.80};
    double metallic=0, roughness=0.55, opacity=1;
    Vec3 emissionColor{1,1,1};
    double emissionIntensity=0, bloom=0.6, glow=12, emissionPeriod=1;
    std::string emissionEffect="steady";
    std::string texturePath;
    std::string textureAsset; // named embedded PNG/JPEG; portable replacement for texturePath
    double textureScaleU=1, textureScaleV=1;
};
struct ImageAsset {
    std::string name,mimeType,base64,sha256;
    unsigned width=0,height=0;
};
struct FaceAppearance {
    std::string face,sourceTopology,material;
    arkjson::Value appearance; // partial display override; density remains body-owned
};
// A rectangular planar projector, in target-feature coordinates or datum-local
// coordinates when feature=0. The origin is the rectangle centre; +V points up.
struct ProjectedStamp {
    std::string name,asset,plane="XY",sourceTopology;
    u64 feature=0;
    std::vector<std::string> faces;
    Vec3 origin{},uAxis{1,0,0},vAxis{0,1,0};
    std::string width="100",height="100";
    double opacity=1,depth=1;
    bool frontOnly=true;
};
struct Material {
    std::string name;
    double densityGcm3=0;
    Appearance appearance;
    arkjson::Value physical; // Explicit SI elastic properties, optional.
};

// Usage is a document audit trail of provider-reported token counts. It never
// estimates tokens for an external agent when that client did not report them.
struct UsageReceipt {
    std::string id,source,provenance,model,recordedAtUtc;
    u64 inputTokens=0,outputTokens=0,totalTokens=0;
    u64 cachedInputTokens=0,reasoningTokens=0;
    u64 reportedResponses=0,missingResponses=0;
};
struct UsageLedger {
    std::vector<UsageReceipt> entries;
    u64 serial=0; // changes for usage metadata, independent of geometry revision
    bool externalUsageUnknown=true;
    std::string updatedAtUtc;
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
    arkjson::Value part;                              // explicit BOM metadata, independent of geometry
    std::string material;                             // named document material, empty uses default
    std::string manufacturingRoute;                   // explicit intent; empty is undeclared
    arkjson::Value manufacturingProcess;              // bounded caller-declared route-specific process inputs
    arkjson::Value appearance;                        // validated partial appearance override
    std::vector<FaceAppearance> faceAppearances;       // exact digest-bound region display overrides

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
    enum Kind : i32 { Pnt, Line, Circle, Spline, Arc, Wire, Project, Offset, Recipe } kind=Pnt;
    // dof layout: Pnt -> [x,y]; Line -> [x0,y0,x1,y1]; Circle -> [cx,cy,r]
    // Arc -> [cx,cy,r,a0deg,a1deg]. Wire flattens start, then each segment.
    // Project owns an associative exact derived B-rep; its display samples are
    // separate from the two driven endpoint handles. Offset is a sampled wire.
    // Their coordinates are driven: the solver does not free them.
    struct WireSeg {
        enum Kind : i32 { Line, Arc, Cubic } kind=Line;
        int sweep=1; // +1 bulges left of the directed chord; the arc is the minor arc
    };
    std::vector<f64> dof;
    std::string name;
    std::vector<std::string> expressions; // empty: solver variable; string: parameter binding
    std::vector<WireSeg> segments;
    // Authored arc construction recipe. The evaluated center/radius/angles in
    // dof are derived and never replace these source coordinates/expressions.
    arkjson::Value arcRecipe;
    // Typed authored entity recipes retain expressions. The exact native
    // primitive DTOs and display samples are derived, never persisted.
    arkjson::Value entityRecipe, recipeGeometry, recipeReport;
    std::vector<Vec2> recipeSamples;
    bool construction=false, closed=false;
    f64 tension=0;
    u64 sourceFeature=0;          // project: earlier feature id
    std::string reference;        // project: edge or planar face name
    std::string projectionMode="legacy"; // normal: orthographic boundary; legacy: cross-face section
    std::string sourceSketch,sourceEntity;
    u64 sourceInstance=0;
    int sourceCopy=0;
    // Derived exact projection. Never persisted or accepted from DTOs.
    std::string projectionBrep;
    std::vector<Vec2> projectionSamples;
    f64 projectionLength=0;
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
    std::string asset; // optional embedded asset; path remains for legacy documents
};

struct AgentActivityLedger;
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
    std::vector<ImageAsset> imageAssets;
    std::vector<ProjectedStamp> stamps;
    NamedSketch* findSketch(const std::string& name);
    void setSketch(const std::string& name, Sketch sk);   // create or replace
    bool removeSketch(const std::string& name);

    // features ----------------------------------------------------------------
    std::vector<Feature> features;
    std::vector<Material> materials;
    UsageLedger usage;
    std::shared_ptr<AgentActivityLedger> agentActivity; // audit metadata, independent of geometry revision
    std::string bomDocumentId; // assigned on explicit BOM authoring; stable across edits/save
    arkjson::Value iterationHistory; // immutable authoring journal; no evaluated geometry or usage snapshots
    arkjson::Value designContainerCheckout; // scoped workspace ownership and merge baseline; no derived geometry
    arkjson::Value skeleton; // native declarative source, input overrides and stable ownership manifest
    std::vector<arkjson::Value> drawings; // authored associative technical drawings; derived curves are never persisted
    u64 historyTip=0; // inclusive feature prefix; zero resumes the full authored history
    arkjson::Value assemblyConfigurations; // explicit parameter/occurrence visibility recipes
    arkjson::Value assemblyLoadSelection; // null loads all; array retains selected node subtrees
    arkjson::Value assemblyExternalReferences; // imported source hashes and producer revisions
    u64 addFeature(Feature f);                    // returns id
    Feature* find(u64 id);
    const Feature* find(u64 id) const;
    bool     remove(u64 id);
    bool     reorderMove(u64 id, int delta);      // shift position in history

    // Copy persistent authoring without duplicating evaluated meshes/B-reps.
    Document authoringCopy() const;

    // evaluation --------------------------------------------------------------
    mutable std::vector<Body> evalBodies;                 // last recompute output
    std::vector<Body>* evaluatingBodies=nullptr; // transient earlier history during recompute
    std::vector<FeatureResult> lastResults;
    f64 recomputeMs = 0;
    struct EvaluationCacheEntry { std::string key; std::shared_ptr<const Body> body; u64 generation=0; size_t tris=0; };
    std::unordered_map<u64,EvaluationCacheEntry> evaluationCache;
    struct ReferenceFrameCacheEntry {std::string brep,topology;std::unordered_map<std::string,arkjson::Value> frames;};
    mutable std::unordered_map<u64,ReferenceFrameCacheEntry> referenceFrameCache;
    std::unordered_map<u64,std::pair<std::string,arkjson::Value>> centroidCache; // exact B-rep digest -> producer result; never serialized
    u64 evaluationGeneration=0;
    size_t recomputeReused=0, recomputeEvaluated=0;
    size_t parallelFeatures=0, peakParallelFeatures=0, dependencyEdges=0;
    size_t parallelContextFeaturesCopied=0; // audited per-job feature closure, never serialized
    unsigned featureWorkers=1, nativeThreadsPerWorker=1;
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

// Observational, thread-local progress at real feature evaluation/publication boundaries.
using EvaluationObserver=std::function<void(u64,const std::string&,size_t,size_t,bool,bool)>;
EvaluationObserver setEvaluationObserver(EvaluationObserver observer);

// Shared validation and resolution for persistence, agents, and renderers.
bool applyAppearancePatch(Appearance& appearance,const arkjson::Value& patch,std::string& error);
arkjson::Value appearanceToJson(const Appearance& appearance);
bool validateMaterial(const Material& material,std::string& error);
Appearance resolvedAppearance(const Document& document,const Feature& feature);

// Structured geometry ancestry, independent of part labels or appearance.
struct ManufacturingLineage {
    std::string geometryRoute="unknown";
    u64 origin=0; // sheet/frame authoring feature retained through rigid placement
    bool editableSheet=false;
};
std::string featureOperation(const Feature& feature);
// Face extrusion inputs are geometric references for a separate body; their
// source/limit bodies remain physical outputs until an actual modeling recipe consumes them.
bool featureConsumesInputBodies(const Feature& feature);
ManufacturingLineage featureManufacturingLineage(const Document& document,const Feature& feature);
bool validateManufacturingRoutes(const Document& document,std::string& error);

} // namespace ark
