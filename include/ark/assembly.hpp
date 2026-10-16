#pragma once
#include "ark/core.hpp"
#include "ark/json.hpp"
#include <unordered_map>
namespace ark {
struct AssemblyPattern {
    bool active=false;
    std::string type; // linear/circular, pattern_driven, sketch_driven, curve_driven, chain
    std::string count,dx="0",dy="0",dz="0",step="90",axis="Z";
    arkjson::Value source; // Persistent recipe; evaluated points/frames are never stored.
    std::vector<int> skips; // Stable copy slots; seed 0 cannot be skipped.
    Mat4 carryFrame; // Optional whole-pattern parent-local rigid pose, before node.p.
};
struct AssemblyNode {
    u64 id=0,parent=0,feature=0; // feature=0 is a subassembly/group
    std::string name;
    std::unordered_map<std::string,std::string> p; // local rigid transform expressions
    bool visible=true;
    bool fixed=false; // Grounded instance; its authored placement is retained by the mate solver.
    AssemblyPattern pattern;
};
struct AssemblyJointLimits {
    bool enabled=false;
    std::string min="0",max="0"; // Expressions: degrees for revolute, mm for prismatic.
};
struct AssemblyMate {
    u64 id=0,a=0,b=0; // Instance IDs, never feature definition IDs.
    std::string type; // geometric (including revolute/prismatic/rigid) or mechanical couplings
    std::string reference_a="origin",reference_b="origin";
    std::string value="0"; // Signed distance in mm or angle in degrees.
    double fraction_a=0.5,fraction_b=0.5; // Along semantic edge references.
    std::unordered_map<std::string,std::string> motion; // evaluated expressions, mechanical mates only
    AssemblyJointLimits limits;
};
struct AssemblyMateResidual {
    u64 id=0;
    std::string type;
    double positionMm=0,angleDeg=0,valueError=0;
    bool hasCoordinate=false,limited=false;
    double coordinate=0,min=0,max=0,limitError=0;
};
struct AssemblyMateFrame {
    u64 id=0;
    Vec3 originA,axisA,originB,axisB;
};
struct AssemblyDiagnostics {
    std::vector<AssemblyMateResidual> mates;
    std::vector<AssemblyMateFrame> frames;
    int rank=0,freeDofs=0;
    double maxPositionMm=0,maxAngleDeg=0;
    std::vector<u64> unconstrainedNodes;
    arkjson::Value patternReports; // Exact source/placement evidence and explicitly unchecked domains.
    arkjson::Value rigidClusters; // Analytically condensed same-carrier full-frame attachments.
    int solverMovingBodies=0,eliminatedRigidBodies=0;
};
struct AssemblyPlacement {u64 node=0,feature=0;std::string name;Mat4 world;bool visible=true;int copy=0;};
class Document;
bool specializedAssemblyPattern(const AssemblyPattern&);
bool assemblyPatternHasCarry(const AssemblyPattern&);
Mat4 assemblyPatternCarryFrame(const AssemblyPattern&); // Validates finite proper rigid/affine bounds.
arkjson::Value assemblyPatternToJson(const AssemblyPattern&);
void readAssemblyPattern(AssemblyPattern&,const arkjson::Value&);
// Bind unique source names once at authoring; stored identities are IDs.
void bindAssemblyPatternSource(const Document&,AssemblyPattern&);
// Metadata-only slot discovery: no native geometry sampling or assembly solve.
std::vector<int> assemblyPatternCopySlots(const Document&,const AssemblyNode&);
// Definition identity for an already validated stable slot (no native sampling).
u64 assemblyPatternCopyFeature(const Document&,const AssemblyNode&,int copy);
// Parsing/history validates authoring without requiring unevaluated B-reps.
// Runtime publication still resolves the exact evaluated placements.
bool validateAssemblyPatternAuthoring(const Document&,std::string& error);
// Expand after the mate solve. Failure publishes no partial placements.
bool expandAssemblyPatterns(const Document&,
    const std::unordered_map<u64,Mat4>& parentFrames,
    const std::unordered_map<u64,Mat4>& rigid,
    const std::unordered_map<u64,Mat4>& solvedWorld,
    const std::unordered_map<u64,bool>& visibility,
    std::vector<AssemblyPlacement>&,std::string& error,arkjson::Value* reports=nullptr);
bool resolveAssembly(const Document&,std::vector<AssemblyPlacement>&,std::string& error,
                     AssemblyDiagnostics* diagnostics=nullptr,bool reportUnsatisfied=false);
// Solves a temporary mate and removes it before returning. The document is unchanged.
bool probeAssemblyMate(Document&,const AssemblyMate& candidate,AssemblyDiagnostics& diagnostics,
                       std::string& error,bool& satisfied);
bool isMechanicalMate(const std::string& type);
struct AssemblyMotionRelation {
    AssemblyMate mate;
    Mat4 localA,localB;
    double ratio=1,lift=0,phaseRad=0,target=0,teethA=0,teethB=0;
    bool translational=false;
    bool limited=false;
    double minimum=0,maximum=0;
};
// Build once per document revision, then evaluate repeatedly without copying or
// recomputing the CAD document. The program owns its evaluated static baseline.
struct AssemblyMotionProgram {
    std::vector<AssemblyPlacement> baseline;
    std::vector<AssemblyMotionRelation> relations,geometricConstraints;
    std::unordered_map<u64,Mat4> localAxes;
};
bool buildAssemblyMotion(const Document&,AssemblyMotionProgram&,std::string& error);
arkjson::Value inspectAssemblyMotion(const AssemblyMotionProgram&,u64 driver,double speedRpm,double timeSeconds,double angleDegrees=0);
// Bridge joints retain passive coordinates through an exact rigid motion.
// Closed loops and transmissions use constrained continuation from the resolved
// pose, retaining every constraint and the selected joint coordinate.
struct AssemblyManipulationJoint {
    u64 mate=0;
    u64 movingEndpoint=0; // Endpoint used for nearest-joint selection.
    bool constrained=false;
    std::string type,mode; // rotate: degrees; translate: millimetres
    Vec3 origin,axis; // world baseline pivot and oriented unit axis
    std::vector<u64> movingNodes;
    bool limited=false;
    double position=0,min=0,max=0,coordinateSign=1; // B relative to A; delta maps through coordinateSign.
};
struct AssemblyManipulationProgram {
    std::vector<AssemblyPlacement> baseline;
    AssemblyDiagnostics diagnostics; // rank/DOFs measured at resolved baseline
    std::vector<AssemblyManipulationJoint> joints;
    std::vector<AssemblyMotionRelation> constraints;
    std::unordered_map<u64,Mat4> parentFrames,authoredWorld;
    std::unordered_map<u64,Mat4> patternLocalFrames; // Original node.p remains editable and unchanged.
    std::unordered_map<u64,arkjson::Value> patternRecipes;
    std::unordered_map<u64,u64> rigidCluster;
    std::vector<u64> fixedNodes,patternNodes;
};
struct AssemblyManipulationPreview {
    std::vector<AssemblyPlacement> placements;
    AssemblyDiagnostics diagnostics;
    arkjson::Value commands; // assembly.edit pose recipe; caller must revision-check
    AssemblyManipulationJoint joint;
    double delta=0;
    int continuationSteps=0;
    arkjson::Value couplings; // Measured incremental relation residuals.
};
bool buildAssemblyManipulation(const Document&,AssemblyManipulationProgram&,std::string& error);
bool selectAssemblyManipulationJoint(const AssemblyManipulationProgram&,u64 driver,u64 mate,
                                    const std::string& mode,AssemblyManipulationJoint&,std::string& error);
bool previewAssemblyManipulation(const AssemblyManipulationProgram&,u64 driver,u64 mate,
                                const std::string& mode,double delta,
                                AssemblyManipulationPreview&,std::string& error);
}
