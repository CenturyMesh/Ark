#pragma once
#include "ark/mesh.hpp"
#include "ark/json.hpp"
namespace ark {
struct SourceBend {Vec3 start,axis,outward,widthAxis;double length=0,width=0,wall=0,neutralRadius=0,angle=0;};
struct SourceTubePreview {Mesh straight;std::vector<Mesh> rigid;std::vector<SourceBend> bends;arkjson::Value report,holes;};
SourceTubePreview buildSourceTubePreview(const arkjson::Value& package);
std::vector<Mat4> sourceFoldTransforms(const SourceTubePreview&,double progress);
Mesh sourceHingeMesh(const SourceBend&,double phase);
}
