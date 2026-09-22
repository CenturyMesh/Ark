#pragma once
#include "ark/json.hpp"
#include "ark/mesh.hpp"
namespace ark {
struct NotchPoint {std::string station="200",angle="90",face="bottom";};
struct Feature;struct ExprEnv;
// Ideal rigid-half kinematics only. Does not certify deformation of a finite wall.
arkjson::Value planNotchedBlank(const arkjson::Value& request);
struct NotchPreview {
    arkjson::Value plan;
    Mesh straight;
    std::vector<Mesh> segments;
    std::vector<double> stations,angles;
    std::vector<Vec3> hingePoints,hingeAxes;
};
arkjson::Value notchRequestForFeature(const Feature&,const ExprEnv&);
NotchPreview buildNotchPreview(const arkjson::Value& request);
std::vector<Mat4> notchFoldTransforms(const NotchPreview&,double progress);
}
