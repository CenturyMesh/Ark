#pragma once
#include "ark/json.hpp"
#include <TopoDS_Shape.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <gp_Trsf.hxx>
#include <gp_XYZ.hxx>
#include <filesystem>
#include <array>
#include <vector>
#include <string>
class GProp_GProps;

namespace ark::occt {
using V=arkjson::Value;
namespace fs=std::filesystem;
std::string readFile(const fs::path& path);
void writeFile(const fs::path& path,const std::string& bytes);
void replaceFile(const fs::path& staged,const fs::path& destination);
std::string sha256(const std::string& bytes);
std::string base64Encode(const std::string& bytes);
std::string base64Decode(const std::string& text);
TopoDS_Shape readStep(const fs::path& path);
void writeStep(const TopoDS_Shape& shape,const fs::path& path);
double properties(const TopoDS_Shape& shape,char kind='v');
void volumeProperties(const TopoDS_Shape& shape,GProp_GProps& properties,bool moments=true);
const char* volumeIntegrationMethod(const TopoDS_Shape& shape);
std::vector<TopoDS_Shape> shapes(const TopoDS_Shape& shape,TopAbs_ShapeEnum kind);
std::array<double,6> bounds(const TopoDS_Shape& shape);
TopoDS_Shape cut(const TopoDS_Shape& a,const TopoDS_Shape& b);
TopoDS_Shape common(const TopoDS_Shape& a,const TopoDS_Shape& b);
TopoDS_Shape fuse(const TopoDS_Shape& a,const TopoDS_Shape& b);
TopoDS_Shape compound(const std::vector<TopoDS_Shape>& parts);
TopoDS_Shape transformed(const TopoDS_Shape& shape,const gp_Trsf& transform);
// Full ordered picking edges plus optional edge_visible bools for display only.
V meshData(const TopoDS_Shape& shape,double deflection=.025,double angle=.12);
V edgeVisibility(const TopoDS_Shape& shape); // Read-only native topology/regularity classification.
V vec(const gp_XYZ& xyz);
V matrix(const gp_Trsf& transform);
std::string brepData(const TopoDS_Shape& shape); // base64 BRep, no tessellation
TopoDS_Shape readBrep(const std::string& encoded);
V shapeResult(const TopoDS_Shape& shape,bool requireSolid);
V shapeReport(const TopoDS_Shape& shape,bool requireSolid);
double symmetricDifference(const TopoDS_Shape& a,const TopoDS_Shape& b);
std::string kernelVersion();
int nativeThreadBudget(); // ARK_NATIVE_THREADS=1 disables parallel tessellation
}
