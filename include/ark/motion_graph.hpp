#pragma once
#include "ark/model.hpp"
#include "ark/document_container.hpp"
#include <atomic>
#include <map>
namespace ark {
class AgentSession;
// 044B Skeleton Motion. The skeleton is the set of existing revolute/prismatic
// assembly joints. A persisted vector graph describes each joint coordinate over
// time with analytic and cubic Bezier segments composed by graph nodes; it is
// never a keyframe list. Baking evaluates the graph through the real joint solver.
struct MotionCurveNode {
    std::string type,ease,combine,expression;
    double duration=0,start=0,end=0; // start: value at local time zero; end: value at duration
    double p[8]{};                   // type-specific coefficients
    int count=1;
    std::vector<int> children;
    std::vector<double> offsets;     // sequence child start times
};
struct MotionChannelPlan {
    std::string name,expression;
    u64 mate=0;int root=-1;double start=0;bool explicitStart=false;
    std::vector<size_t> dependencies; // channels referenced by an expression coupling
};
struct CompiledMotion {
    double duration=0;bool allowJumps=false;
    std::vector<MotionCurveNode> nodes;
    std::vector<MotionChannelPlan> channels;
    std::vector<size_t> order; // coupling dependency order
    ExprEnv env;               // document parameters at compile time
};
// startValues supplies a channel start when the graph omits "from" (bake uses the measured joint coordinate).
bool compileMotionGraph(const arkjson::Value& graph,const ExprEnv& env,const std::map<std::string,double>& startValues,CompiledMotion&,std::string& error);
// side -1/+1 selects the left/right limit at a segment boundary. Throws on a nonfinite coupling.
std::vector<double> evaluateMotion(const CompiledMotion&,double time,std::vector<double>* slopes=nullptr,int side=1);
std::vector<double> motionSeams(const CompiledMotion&,size_t channel);
struct MotionBezierHandle {double t0=0,v0=0,t1=0,v1=0,t2=0,v2=0,t3=0,v3=0;};
// Control polygons of Bezier segments whose graph time mapping is affine.
std::vector<MotionBezierHandle> motionBezierHandles(const CompiledMotion&,size_t channel);
struct MotionLimit {bool limited=false;double min=0,max=0;};
// Continuity (C0/C1) at every seam, sampled ranges and optional limit violations.
arkjson::Value motionDiagnostics(const CompiledMotion&,const std::vector<MotionLimit>& limits={});

struct MotionJointBinding {std::string channel,type,unit;u64 mate=0,driver=0;bool limited=false,constrained=false;double min=0,max=0,position=0;std::string explodeView;}; // 048B: explode_view signal, factor 0..1
// Every bound channel names an existing revolute or prismatic mate with a grounded moving side.
bool bindMotionJoints(const AssemblyManipulationProgram&,const Document&,const arkjson::Value& graph,std::vector<MotionJointBinding>&,std::string& error);
// Solved joint at one frame: world pivot/axis and its moving occurrence set.
struct MotionJointFrame {Vec3 origin,axis;bool rotate=false,constrained=false,valid=false;size_t movingSet=0;};
struct MotionSolvedFrames {
    std::vector<double> times;
    std::vector<AssemblyPlacement> baseline;   // committed pose
    std::vector<std::vector<Mat4>> poses;      // per frame, aligned with baseline
    std::vector<std::vector<double>> coordinates; // per frame, measured joint coordinate per channel
    std::vector<std::vector<MotionJointFrame>> joints; // per frame and channel; only with recordJoints
    std::vector<std::vector<u64>> movingSets;  // distinct sorted moving node sets referenced by joints
    size_t rigidSteps=0,constrainedSteps=0;int maxContinuationSteps=0;double maxChannelError=0;
};
// Drives the bound joints frame by frame with the assembly manipulation solver.
// Work-document placements are restored before returning; it never commits.
bool solveMotionFrames(Document& work,const CompiledMotion&,const std::vector<MotionJointBinding>&,const std::vector<double>& times,MotionSolvedFrames&,std::string& error,bool recordJoints=false);
std::vector<double> motionFrameTimes(double duration,double fps);

// Studio playback form of a persisted bake. Matrices are rigid display poses.
struct BakedMotionFrames {
    std::vector<u64> nodes;std::vector<int> copies;std::vector<std::string> names;
    std::vector<Mat4> poses; // frame-major, instance-minor
    std::vector<float> coordinates; // frame-major, channel-minor
    std::vector<std::string> channels,units,explodeViews;std::vector<double> starts; // explodeViews: per channel, empty unless explode_view
    size_t frameCount=0;double fps=30,duration=0;
};
// Inline 044B-048B payloads, or a 049A receipt whose frames are in this process cache.
bool decodeBakedMotion(const arkjson::Value& motion,BakedMotionFrames&,std::string& error);
// 049A derived bakes. A persisted bake is a receipt: every bake field except the
// float32 payloads, plus frames_sha256/coordinates_sha256 over the raw
// little-endian bytes. Frames are derived data: this process cache first (seeded
// by bakes, re-bakes and the document's optional derived section on open), then
// a deterministic re-bake whose digests must match. Inline payloads stay readable
// until the next bake or an explicit history_migrate_derived.
bool bakeIsReceipt(const arkjson::Value& bake);
bool bakeHasInlineFrames(const arkjson::Value& bake);
bool bakedMotionFramesCached(const arkjson::Value& bake);
bool motionReceiptsRequireDocumentVersion49(const Document&);
// source: memory (baked or re-baked here), derived (document derived section),
// rebake (solved now) or inline (legacy payload). A re-bake that does not
// reproduce the receipt is returned with verified=false and mismatch set; it is
// the current solve, never the recorded evidence.
struct BakedMotionFramesSource {std::string source,mismatch;bool verified=false;std::vector<std::string> staleReasons;arkjson::Value json() const;};
bool resolveBakedMotion(Document&,const arkjson::Value& motion,BakedMotionFrames&,std::string& error,BakedMotionFramesSource* source=nullptr,bool allowRebake=true);
// Replaces inline payloads by a receipt; with cache the frames stay available. False without inline payloads.
bool motionReceiptFromInline(arkjson::Value& bake,bool cache);
struct BakedMotionCacheStats {size_t entries=0,bytes=0,capacityBytes=0,hits=0,misses=0,rebakes=0,rebakeMismatches=0,evictions=0;};
BakedMotionCacheStats bakedMotionCacheStats();
void clearBakedMotionCache();
// Optional derived-section entries for cached frames of current receipts, within the given remaining capacity.
struct MotionDerivedReport {size_t entries=0,bytes=0,omittedEntries=0,omittedBytes=0,notCached=0;};
void appendMotionDerivedPayloads(const Document&,DocumentDerivedPayloads& records,size_t capacityBytes,size_t capacityEntries,MotionDerivedReport* report=nullptr);
bool motionDerivedPayload(const DocumentDerivedPayload&);
// Caches entries whose key and digests match a current receipt; others are discarded and counted.
arkjson::Value stageMotionDerivedPayloads(const Document&,const DocumentDerivedPayloads& records);
// Fixture support only: write the 044B-048B inline bake shape to build legacy documents. ARK never enables it.
void setMotionBakeInlineFixture(bool inlinePayloads);
// Assembly authoring, parameters and solved placements a bake depends on.
std::string motionSourceDigest(const Document&,const std::vector<AssemblyPlacement>& placements);
std::string motionGraphDigest(const arkjson::Value& graph);
const arkjson::Value* findMotion(const Document&,const std::string& name);
bool motionsRequireDocumentVersion47(const Document&);
// Structural persistence checks; graph semantics are checked by authoring, bake and inspection.
void validateMotionStorage(const arkjson::Value& motions);
// doc_apply motion.graph.set/bake/rename/remove. Returns false for other operations.
// Callers evaluate geometry first; bake/graph.set resolve the candidate assembly.
bool applyMotionCommand(Document&,const arkjson::Value& command,u64 revision,std::string& error);
bool callMotionTool(AgentSession&,const std::string& tool,const arkjson::Value& args,arkjson::Value& out);
// 046B read-only clearance over a fresh bake (motion_info{name,clearance}).
// Rejects a stale or mismatched bake, re-solves its frames in double precision,
// measures exact B-rep distance per frame and certifies frame intervals with a
// conservative displacement bound, subdividing through the same joint solver.
// Derived evidence only: nothing is persisted. cancel is polled between bounded
// native requests and subdivision levels.
arkjson::Value inspectMotionClearance(Document&,u64 revision,const std::string& motion,const arkjson::Value& request,const std::atomic_bool* cancel=nullptr);
void appendMotionTools(arkjson::Value& tools);
}
