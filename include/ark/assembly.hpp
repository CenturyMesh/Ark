#pragma once
#include "ark/core.hpp"
#include "ark/json.hpp"
#include <unordered_map>
namespace ark {
struct AssemblyPattern {
    bool active=false;
    std::string type; // linear or circular
    std::string count,dx="0",dy="0",dz="0",step="90",axis="Z";
};
struct AssemblyNode {
    u64 id=0,parent=0,feature=0; // feature=0 is a subassembly/group
    std::string name;
    std::unordered_map<std::string,std::string> p; // local rigid transform expressions
    bool visible=true;
    bool fixed=false; // Grounded instance; its authored placement is retained by the mate solver.
    AssemblyPattern pattern;
};
struct AssemblyMate {
    u64 id=0,a=0,b=0; // Instance IDs, never feature definition IDs.
    std::string type; // geometric mates or chain, gear, shaft, rack_pinion, cam
    std::string reference_a="origin",reference_b="origin";
    std::string value="0"; // Signed distance in mm or angle in degrees.
    double fraction_a=0.5,fraction_b=0.5; // Along semantic edge references.
    std::unordered_map<std::string,std::string> motion; // evaluated expressions, mechanical mates only
};
struct AssemblyMateResidual {
    u64 id=0;
    std::string type;
    double positionMm=0,angleDeg=0,valueError=0;
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
};
struct AssemblyPlacement {u64 node=0,feature=0;std::string name;Mat4 world;bool visible=true;int copy=0;};
class Document;
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
}
