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
#include <cstdint>
#include <functional>
class GProp_GProps;

namespace ark::occt {
using V=arkjson::Value;
namespace fs=std::filesystem;
// Opt-in request-local timing only. Trace rows never contain model data, paths,
// B-reps, authored names, geometry coordinates or error text.
class NativeProfileRequest {
    void* state_=nullptr;
public:
    NativeProfileRequest() noexcept;
    ~NativeProfileRequest() noexcept;
    NativeProfileRequest(const NativeProfileRequest&)=delete;
    NativeProfileRequest& operator=(const NativeProfileRequest&)=delete;
    void operation(const std::string& worker,const std::string& model={}) noexcept;
    void succeeded() noexcept;
};
class NativeProfileScope {
    const char* name_=nullptr;
    NativeProfileScope* parent_=nullptr;
    std::uint64_t start_=0,children_=0;
public:
    explicit NativeProfileScope(const char* phase) noexcept;
    ~NativeProfileScope() noexcept;
    NativeProfileScope(const NativeProfileScope&)=delete;
    NativeProfileScope& operator=(const NativeProfileScope&)=delete;
};
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
// Volume and centre of mass only; the inertia tensor is not guaranteed.
void volumeCentroidProperties(const TopoDS_Shape& shape,GProp_GProps& properties);
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
// Display/picking tessellation of a generated or restored body. Parts up to
// 250 mm keep 0.025 mm / 0.12 rad; larger parts scale the chord deviation with
// their diagonal. The exact B-rep remains the only geometry authority.
struct DisplayTessellation {double deflection,angle;};
DisplayTessellation displayTessellation(const TopoDS_Shape& shape);
V displayMeshData(const TopoDS_Shape& shape);
V edgeVisibility(const TopoDS_Shape& shape); // Read-only native topology/regularity classification.
V vec(const gp_XYZ& xyz);
V matrix(const gp_Trsf& transform);
std::string brepData(const TopoDS_Shape& shape); // base64 BRep, no tessellation
TopoDS_Shape readBrep(const std::string& encoded);
// Exactly one solid with positive measured volume, else the same explicit
// rejection as shapeReport(shape,true). Returns that volume; no BRepCheck.
double requireOnePositiveSolid(const TopoDS_Shape& shape);
V shapeResult(const TopoDS_Shape& shape,bool requireSolid);
// shapeResult(shape,true) for a solid whose volume this request has already
// measured with properties(shape); avoids integrating the same volume twice.
V solidShapeResult(const TopoDS_Shape& shape,double measuredVolume);
// One local generation pipeline: validate, serialize, add producer references,
// and derive selection/vertices from the same exact shape. No mutable cache.
V generatedShapeResult(const TopoDS_Shape& shape,bool requireSolid,
    const std::function<V(const std::string&)>& topologyBuilder,bool vertices=false);
// Complete current-B-rep picking inventory without changing semantic aliases.
// Generic names embed the exact B-rep digest and never retarget after a change.
V completeSelectionReferences(V result);
// Untrusted persisted state: validate the original B-rep and saved reference
// bindings, regenerate current measurements/mesh, and preserve its exact bytes.
V restoreNativeResult(const V& resolvedRequest,const V& savedResult);
V shapeReport(const TopoDS_Shape& shape,bool requireSolid);
double symmetricDifference(const TopoDS_Shape& a,const TopoDS_Shape& b);
std::string kernelVersion();
int nativeThreadBudget(); // ARK_NATIVE_THREADS=1 disables parallel tessellation
void configureNativeThreadBudget(int threads); // Idle, serialized worker request boundary only.
}
