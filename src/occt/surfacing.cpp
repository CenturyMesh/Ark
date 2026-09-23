#include "ark/occt_modeling.hpp"
#include "common.hpp"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierCurve.hxx>
#include <GeomAbs_Shape.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace ark::occt {
namespace {
using V = arkjson::Value;
constexpr double kTolerance = 1.0e-6;

const V& required(const V& object, const char* key) {
    if (object.t != V::Obj) throw std::runtime_error("Expected object");
    const V* value = object.find(key);
    if (!value) throw std::runtime_error(std::string("Missing ") + key);
    return *value;
}

double number(const V& value, const char* label) {
    if (value.t != V::Num || !std::isfinite(value.num) || std::abs(value.num) > 1e8)
        throw std::runtime_error(std::string(label) + " must be a finite number");
    return value.num;
}

int integer(const V& value, const char* label, int low, int high) {
    const double x = number(value, label);
    if (x < low || x > high || std::floor(x) != x)
        throw std::runtime_error(std::string(label) + " out of range");
    return static_cast<int>(x);
}

double tolerance(const V& request) {
    const V* value = request.find("tolerance");
    if (!value) return kTolerance;
    const double result = number(*value, "tolerance");
    if (result != kTolerance) throw std::runtime_error("Only 1e-6 mm tolerance is supported");
    return result;
}

std::array<double, 16> frameOf(const V& profile) {
    constexpr double frameTolerance = 1e-10; // dimensionless; not the linear modeling tolerance
    std::array<double, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const V* frame = profile.find("frame");
    if (!frame) return m;
    if (frame->t != V::Arr || frame->arr.size() != 16)
        throw std::runtime_error("frame must contain 16 column-major numbers");
    for (size_t i = 0; i < 16; ++i) m[i] = number(frame->arr[i], "frame entry");
    if (std::abs(m[3]) > frameTolerance || std::abs(m[7]) > frameTolerance ||
        std::abs(m[11]) > frameTolerance || std::abs(m[15] - 1) > frameTolerance)
        throw std::runtime_error("frame must be affine");
    for (int a = 0; a < 3; ++a) {
        for (int b = a; b < 3; ++b) {
            double dot = 0;
            for (int c = 0; c < 3; ++c) dot += m[4 * a + c] * m[4 * b + c];
            if (std::abs(dot - (a == b ? 1.0 : 0.0)) > frameTolerance)
                throw std::runtime_error("frame axes must be orthonormal");
        }
    }
    const double det = m[0] * (m[5] * m[10] - m[6] * m[9]) -
                       m[4] * (m[1] * m[10] - m[2] * m[9]) +
                       m[8] * (m[1] * m[6] - m[2] * m[5]);
    if (std::abs(det - 1.0) > frameTolerance)
        throw std::runtime_error("frame must preserve orientation");
    return m;
}

gp_Pnt point(const V& value, const std::array<double, 16>& m) {
    if (value.t != V::Arr || (value.arr.size() != 2 && value.arr.size() != 3))
        throw std::runtime_error("point must be [x,y] or [x,y,z]");
    const double x = number(value.arr[0], "point x");
    const double y = number(value.arr[1], "point y");
    const double z = value.arr.size() == 3 ? number(value.arr[2], "point z") : 0;
    return {m[0] * x + m[4] * y + m[8] * z + m[12],
            m[1] * x + m[5] * y + m[9] * z + m[13],
            m[2] * x + m[6] * y + m[10] * z + m[14]};
}

struct EdgeRecord {
    TopoDS_Edge edge;
    gp_Pnt first;
    gp_Pnt last;
};

EdgeRecord edgeOf(const V& entity, const std::array<double, 16>& frame) {
    const V& kind = required(entity, "kind");
    if (kind.t != V::Str) throw std::runtime_error("entity kind must be string");
    if (kind.str == "circle") {
        const gp_Pnt center = point(required(entity, "center"), frame);
        const double radius = number(required(entity, "radius"), "radius");
        if (radius <= kTolerance) throw std::runtime_error("circle radius too small");
        const gp_Dir normal(frame[8], frame[9], frame[10]);
        const gp_Dir xAxis(frame[0], frame[1], frame[2]);
        const gp_Circ circle(gp_Ax2(center, normal, xAxis), radius);
        BRepBuilderAPI_MakeEdge maker(circle);
        if (!maker.IsDone()) throw std::runtime_error("circle edge construction failed");
        const gp_Pnt seam(center.X() + radius * frame[0],
                          center.Y() + radius * frame[1],
                          center.Z() + radius * frame[2]);
        return {maker.Edge(), seam, seam};
    }
    const V& points = required(entity, "points");
    const size_t count = kind.str == "line" ? 2 : kind.str == "bezier" ? 4 : 0;
    if (!count || points.t != V::Arr || points.arr.size() != count)
        throw std::runtime_error("line needs 2 points; bezier needs 4 points");
    std::vector<gp_Pnt> poles;
    for (const auto& item : points.arr) poles.push_back(point(item, frame));
    if (poles.front().Distance(poles.back()) <= kTolerance)
        throw std::runtime_error("zero-length/openly degenerate edge");
    if (kind.str == "line") {
        BRepBuilderAPI_MakeEdge maker(poles[0], poles[1]);
        if (!maker.IsDone()) throw std::runtime_error("line edge construction failed");
        return {maker.Edge(), poles[0], poles[1]};
    }
    NCollection_Array1<gp_Pnt> controls(1, 4);
    for (int i = 1; i <= 4; ++i) controls.SetValue(i, poles[static_cast<size_t>(i - 1)]);
    Handle(Geom_BezierCurve) curve = new Geom_BezierCurve(controls);
    BRepBuilderAPI_MakeEdge maker(curve);
    if (!maker.IsDone()) throw std::runtime_error("Bezier edge construction failed");
    return {maker.Edge(), poles.front(), poles.back()};
}

struct Profile {
    TopoDS_Wire wire;
    std::vector<EdgeRecord> edges;
    bool closed = false;
};

Profile profileOf(const V& value, bool requireClosed) {
    const auto frame = frameOf(value);
    const V& entities = required(value, "entities");
    if (entities.t != V::Arr || entities.arr.empty() || entities.arr.size() > 128)
        throw std::runtime_error("profile needs 1..128 entities");
    Profile result;
    for (const V& entity : entities.arr) {
        EdgeRecord edge = edgeOf(entity, frame);
        if (!result.edges.empty() && result.edges.back().last.Distance(edge.first) > kTolerance)
            throw std::runtime_error("profile entities are not connected in supplied order");
        result.edges.push_back(std::move(edge));
    }
    result.closed = result.edges.front().first.Distance(result.edges.back().last) <= kTolerance;
    if (requireClosed && !result.closed) throw std::runtime_error("profile is not explicitly closed");
    BRepBuilderAPI_MakeWire maker;
    for (const auto& edge : result.edges) maker.Add(edge.edge);
    if (!maker.IsDone()) throw std::runtime_error("profile wire construction failed");
    result.wire = maker.Wire();
    return result;
}

V loft(const V& request) {
    const V& profiles = required(request, "profiles");
    if (profiles.t != V::Arr || profiles.arr.size() < 2 || profiles.arr.size() > 32)
        throw std::runtime_error("loft needs 2..32 profiles");
    const V* solidValue = request.find("solid");
    const V* ruledValue = request.find("ruled");
    if ((solidValue && solidValue->t != V::Bool) || (ruledValue && ruledValue->t != V::Bool))
        throw std::runtime_error("solid and ruled must be booleans");
    const bool solid = solidValue && solidValue->b;
    const bool ruled = ruledValue && ruledValue->b;
    int degree = 8;
    if (const V* maxDegree = request.find("max_degree"))
        degree = integer(*maxDegree, "max_degree", 1, 8);
    BRepOffsetAPI_ThruSections maker(solid, ruled, tolerance(request));
    maker.CheckCompatibility(false); // Explicit section edge order is authoritative.
    maker.SetMaxDegree(degree);
    size_t segments = 0;
    bool closed = false;
    std::vector<std::pair<std::string,TopoDS_Edge>> firstSectionEdges;
    for (const V& value : profiles.arr) {
        Profile profile = profileOf(value, solid);
        if (!segments) { segments = profile.edges.size(); closed = profile.closed; }
        if (profile.edges.size() != segments || profile.closed != closed)
            throw std::runtime_error("loft profiles have incompatible edge topology");
        if(firstSectionEdges.empty()){
            const V& sourceEntities=required(value,"entities");
            std::set<std::string> names;
            for(size_t i=0;i<profile.edges.size();++i){
                const V* supplied=sourceEntities.arr[i].find("name");
                std::string name=supplied&&supplied->t==V::Str&&!supplied->str.empty()?supplied->str:"entity."+std::to_string(i);
                if(!names.insert(name).second)throw std::runtime_error("Duplicate loft section entity name");
                firstSectionEdges.push_back({name,profile.edges[i].edge});
            }
        }
        maker.AddWire(profile.wire);
    }
    maker.Build();
    if (!maker.IsDone()) throw std::runtime_error("OCCT loft failed");
    V result=shapeResult(maker.Shape(), solid),topology=V::makeObj(),rows=V::makeArr();
    topology.set("brep_sha256",V::makeStr(sha256(base64Decode(required(result,"brep_base64").str))));
    auto bind=[&](const std::string& name,const TopoDS_Shape& source){
        V row=V::makeObj(),indices=V::makeArr();int index=-1;
        if(!source.IsNull()){
            int n=0;for(auto& candidate:shapes(maker.Shape(),TopAbs_FACE)){if(candidate.IsSame(source)){index=n;break;}++n;}
        }
        row.set("name",V::makeStr(name));row.set("kind",V::makeStr("face"));row.set("status",V::makeStr(index<0?"deleted":"bound"));
        if(index>=0)indices.arr.push_back(V::makeNum(index));row.set("indices",indices);rows.arr.push_back(row);
    };
    if(solid){bind("cap.start",maker.FirstShape());bind("cap.end",maker.LastShape());}
    for(const auto& [name,edge]:firstSectionEdges)bind("side."+name,maker.GeneratedFace(edge));
    topology.set("references",rows);result.set("topology",topology);return result;
}

V surfaceFill(const V& request) {
    Profile profile = profileOf(required(request, "profile"), true);
    const double tol = tolerance(request);
    BRepOffsetAPI_MakeFilling maker(3, 15, 2, false, tol, tol);
    for (const auto& edge : profile.edges) maker.Add(edge.edge, GeomAbs_C0, true);
    maker.Build();
    if (!maker.IsDone()) throw std::runtime_error("OCCT surface filling failed");
    if (!std::isfinite(maker.G0Error()) || maker.G0Error() > tol)
        throw std::runtime_error("surface filling exceeds boundary tolerance");
    return shapeResult(maker.Shape(), false);
}

NCollection_Array1<double> knots(int poleCount, int degree) {
    const int length = poleCount - degree + 1;
    NCollection_Array1<double> result(1, length);
    for (int i = 1; i <= length; ++i)
        result.SetValue(i, static_cast<double>(i - 1) / (length - 1));
    return result;
}

NCollection_Array1<int> multiplicities(int poleCount, int degree) {
    const int length = poleCount - degree + 1;
    NCollection_Array1<int> result(1, length);
    for (int i = 1; i <= length; ++i)
        result.SetValue(i, i == 1 || i == length ? degree + 1 : 1);
    return result;
}

V surfaceNurbs(const V& request) {
    const V& rows = required(request, "poles");
    if (rows.t != V::Arr || rows.arr.size() < 2 || rows.arr.size() > 32)
        throw std::runtime_error("NURBS needs 2..32 pole rows");
    const int nu = static_cast<int>(rows.arr.size());
    if (rows.arr.front().t != V::Arr || rows.arr.front().arr.size() < 2 ||
        rows.arr.front().arr.size() > 32)
        throw std::runtime_error("NURBS needs 2..32 poles per row");
    const int nv = static_cast<int>(rows.arr.front().arr.size());
    int uDegree = 3, vDegree = 3;
    if (const V* value = request.find("u_degree")) uDegree = integer(*value, "u_degree", 1, 8);
    if (const V* value = request.find("v_degree")) vDegree = integer(*value, "v_degree", 1, 8);
    if (uDegree >= nu || vDegree >= nv) throw std::runtime_error("degree exceeds pole grid");
    const V* weightsValue = request.find("weights");
    if (weightsValue && (weightsValue->t != V::Arr || weightsValue->arr.size() != rows.arr.size()))
        throw std::runtime_error("weights grid shape mismatch");
    NCollection_Array2<gp_Pnt> poles(1, nu, 1, nv);
    NCollection_Array2<double> weights(1, nu, 1, nv);
    const std::array<double, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (int u = 1; u <= nu; ++u) {
        const V& row = rows.arr[static_cast<size_t>(u - 1)];
        if (row.t != V::Arr || static_cast<int>(row.arr.size()) != nv)
            throw std::runtime_error("NURBS pole grid is ragged");
        const V* weightRow = weightsValue ? &weightsValue->arr[static_cast<size_t>(u - 1)] : nullptr;
        if (weightRow && (weightRow->t != V::Arr || static_cast<int>(weightRow->arr.size()) != nv))
            throw std::runtime_error("NURBS weight grid is ragged");
        for (int v = 1; v <= nv; ++v) {
            const V& p = row.arr[static_cast<size_t>(v - 1)];
            if (p.t != V::Arr || p.arr.size() != 3) throw std::runtime_error("NURBS poles must be [x,y,z]");
            poles.SetValue(u, v, point(p, identity));
            const double weight = weightRow ? number(weightRow->arr[static_cast<size_t>(v - 1)], "weight") : 1;
            if (weight <= 0) throw std::runtime_error("NURBS weights must be positive");
            weights.SetValue(u, v, weight);
        }
    }
    Handle(Geom_BSplineSurface) surface = new Geom_BSplineSurface(
        poles, weights, knots(nu, uDegree), knots(nv, vDegree),
        multiplicities(nu, uDegree), multiplicities(nv, vDegree), uDegree, vDegree);
    BRepBuilderAPI_MakeFace maker(surface, tolerance(request));
    if (!maker.IsDone()) throw std::runtime_error("NURBS face construction failed");
    return shapeResult(maker.Face(), false);
}

V thicken(const V& request) {
    const V& input = required(request, "input_brep");
    if (input.t != V::Str || input.str.empty()) throw std::runtime_error("input_brep must be base64");
    const double thickness = number(required(request, "thickness"), "thickness");
    if (std::abs(thickness) <= tolerance(request))
        throw std::runtime_error("thickness magnitude must exceed tolerance");
    const TopoDS_Shape source = readBrep(input.str);
    if (source.ShapeType() != TopAbs_FACE && source.ShapeType() != TopAbs_SHELL)
        throw std::runtime_error("thicken requires one open face or shell");
    BRepOffsetAPI_MakeThickSolid maker;
    maker.MakeThickSolidBySimple(source, thickness);
    if (!maker.IsDone()) throw std::runtime_error("OCCT thickening failed");
    TopoDS_Shape solid = maker.Shape();
    if (shapes(solid, TopAbs_SOLID).size() != 1)
        throw std::runtime_error("OCCT thickening did not return one solid: type=" +
                                 std::to_string(solid.ShapeType()) +
                                 " shells=" + std::to_string(shapes(solid, TopAbs_SHELL).size()));
    // The simple offset can return the opposite topological orientation for a
    // positive offset. Reverse orientation only; never alter offset geometry.
    if (properties(solid) < 0) solid.Reverse();
    return shapeResult(solid, true);
}
} // namespace

V modelSurfaceNative(const V& request) {
    try {
        const V& operation = required(request, "op");
        if (operation.t != V::Str) throw std::runtime_error("op must be a string");
        if (operation.str == "loft") return loft(request);
        if (operation.str == "surface_fill") return surfaceFill(request);
        if (operation.str == "surface_nurbs") return surfaceNurbs(request);
        if (operation.str == "thicken") return thicken(request);
        throw std::runtime_error("unsupported surfacing operation");
    } catch (const Standard_Failure& error) {
        throw std::runtime_error(std::string("OCCT surfacing failure: ") + error.GetMessageString());
    }
}
} // namespace ark::occt
