#pragma once
#include "ark/core.hpp"
#include <unordered_map>
namespace ark {
struct AssemblyNode {
    u64 id=0,parent=0,feature=0; // feature=0 is a subassembly/group
    std::string name;
    std::unordered_map<std::string,std::string> p; // local rigid transform expressions
    bool visible=true;
    bool fixed=false; // Grounded instance; its authored placement is retained by the mate solver.
};
struct AssemblyMate {
    u64 id=0,a=0,b=0; // Instance IDs, never feature definition IDs.
    std::string type; // concentric, coincident, contact, distance, angle
    std::string reference_a="origin",reference_b="origin";
    std::string value="0"; // Signed distance in mm or angle in degrees.
    double fraction_a=0.5,fraction_b=0.5; // Along semantic edge references.
};
struct AssemblyMateResidual {
    u64 id=0;
    std::string type;
    double positionMm=0,angleDeg=0,valueError=0;
};
struct AssemblyDiagnostics {
    std::vector<AssemblyMateResidual> mates;
    int rank=0,freeDofs=0;
    double maxPositionMm=0,maxAngleDeg=0;
    std::vector<u64> unconstrainedNodes;
};
struct AssemblyPlacement {u64 node=0,feature=0;std::string name;Mat4 world;bool visible=true;};
class Document;
bool resolveAssembly(const Document&,std::vector<AssemblyPlacement>&,std::string& error,
                     AssemblyDiagnostics* diagnostics=nullptr);
}
