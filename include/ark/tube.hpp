#pragma once
#include "ark/mesh.hpp"
#include <unordered_map>
namespace ark {
struct TubeBend {
    std::string feed="0", radius="60", angle="90", roll="0";
};
struct TubeFrame {Vec3 point,tangent,normal,binormal;f64 station=0;};
struct TubeResult {
    Mesh mesh;
    std::vector<TubeFrame> frames;
    std::vector<f64> bendStarts,bendEnds;
    f64 centerlineLength=0,sectionArea=0;
    std::string error;
};
struct Feature;struct ExprEnv;
TubeResult buildTube(const Feature&,const ExprEnv&);
}
