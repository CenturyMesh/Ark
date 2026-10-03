#include "ark/occt_modeling.hpp"
#include "common.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierCurve.hxx>
#include <GeomAbs_Shape.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <NCollection_List.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

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
    std::array<double, 16> frame{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
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
    result.frame = frame;
    result.closed = result.edges.front().first.Distance(result.edges.back().last) <= kTolerance;
    if (requireClosed && !result.closed) throw std::runtime_error("profile is not explicitly closed");
    BRepBuilderAPI_MakeWire maker;
    for (const auto& edge : result.edges) maker.Add(edge.edge);
    if (!maker.IsDone()) throw std::runtime_error("profile wire construction failed");
    result.wire = maker.Wire();
    return result;
}

bool flag(const V& request, const char* key) {
    const V* value = request.find(key);
    if (!value) return false;
    if (value->t != V::Bool) throw std::runtime_error(std::string(key) + " must be a boolean");
    return value->b;
}

bool pointProfile(const V& value) {
    if (!value.find("point")) return false;
    const V* entities = value.find("entities");
    return !entities || (entities->t == V::Arr && entities->arr.empty());
}

TopoDS_Edge edgeByEnds(const TopoDS_Shape& host, const gp_Pnt& start, const gp_Pnt& end) {
    if (host.IsNull()) return {};
    for (const TopoDS_Shape& candidate : shapes(host, TopAbs_EDGE)) {
        TopoDS_Vertex first, last;
        TopExp::Vertices(TopoDS::Edge(candidate), first, last);
        if (first.IsNull() || last.IsNull()) continue;
        const gp_Pnt a = BRep_Tool::Pnt(first), b = BRep_Tool::Pnt(last);
        if ((a.Distance(start) <= kTolerance && b.Distance(end) <= kTolerance) ||
            (a.Distance(end) <= kTolerance && b.Distance(start) <= kTolerance))
            return TopoDS::Edge(candidate);
    }
    return {};
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
    const bool smooth = flag(request, "smooth");
    const bool compatible = flag(request, "compatible");
    int degree = 8;
    if (const V* maxDegree = request.find("max_degree"))
        degree = integer(*maxDegree, "max_degree", 1, 8);
    int continuity = 1;
    if (const V* authored = request.find("continuity"))
        continuity = integer(*authored, "continuity", 0, 2);
    BRepOffsetAPI_ThruSections maker(solid, ruled, tolerance(request));
    maker.CheckCompatibility(compatible);
    maker.SetMaxDegree(degree);
    if (smooth) {
        maker.SetSmoothing(true);
        maker.SetContinuity(continuity == 0 ? GeomAbs_C0 : continuity == 2 ? GeomAbs_C2 : GeomAbs_C1);
    }
    size_t segments = 0;
    bool closed = false;
    bool sawWire = false;
    std::vector<std::pair<std::string, EdgeRecord>> firstSectionEdges, lastSectionEdges;
    TopoDS_Wire firstWire;
    auto namedEdges = [](const V& value, const Profile& profile) {
        std::vector<std::pair<std::string, EdgeRecord>> named;
        const V& sourceEntities = required(value, "entities");
        std::set<std::string> names;
        for (size_t i = 0; i < profile.edges.size(); ++i) {
            const V* supplied = sourceEntities.arr[i].find("name");
            std::string name = supplied && supplied->t == V::Str && !supplied->str.empty()
                ? supplied->str : "entity." + std::to_string(i);
            if (!names.insert(name).second) throw std::runtime_error("Duplicate loft section entity name");
            named.push_back({name, profile.edges[i]});
        }
        return named;
    };
    for (size_t profileIndex = 0; profileIndex < profiles.arr.size(); ++profileIndex) {
        const V& value = profiles.arr[profileIndex];
        if (pointProfile(value)) {
            if (profileIndex != 0 && profileIndex + 1 != profiles.arr.size())
                throw std::runtime_error("a point section can only start or end a loft");
            const gp_Pnt tip = point(required(value, "point"), frameOf(value));
            BRepBuilderAPI_MakeVertex vertex(tip);
            if (!vertex.IsDone()) throw std::runtime_error("loft point section is invalid");
            maker.AddVertex(vertex.Vertex());
            continue;
        }
        Profile profile = profileOf(value, solid);
        if (!sawWire) {
            segments = profile.edges.size();
            closed = profile.closed;
            sawWire = true;
            firstWire = profile.wire;
            firstSectionEdges = namedEdges(value, profile);
        } else if (!compatible && (profile.edges.size() != segments || profile.closed != closed)) {
            throw std::runtime_error("loft profiles have incompatible edge topology");
        }
        lastSectionEdges = namedEdges(value, profile);
        maker.AddWire(profile.wire);
    }
    if (!sawWire) throw std::runtime_error("loft needs at least one wire section");
    maker.Build();
    if (!maker.IsDone()) throw std::runtime_error("OCCT loft failed");
    V result = shapeResult(maker.Shape(), solid), topology = V::makeObj(), rows = V::makeArr();
    topology.set("brep_sha256", V::makeStr(sha256(base64Decode(required(result, "brep_base64").str))));
    auto bind = [&](const std::string& name, const std::string& kind, const TopoDS_Shape& source) {
        V row = V::makeObj(), indices = V::makeArr();
        int index = -1;
        if (!source.IsNull()) {
            const TopAbs_ShapeEnum which = kind == "edge" ? TopAbs_EDGE : TopAbs_FACE;
            int n = 0;
            for (const TopoDS_Shape& candidate : shapes(maker.Shape(), which)) {
                if (candidate.IsSame(source)) { index = n; break; }
                ++n;
            }
        }
        row.set("name", V::makeStr(name));
        row.set("kind", V::makeStr(kind));
        row.set("status", V::makeStr(index < 0 ? "deleted" : "bound"));
        if (index >= 0) indices.arr.push_back(V::makeNum(index));
        row.set("indices", indices);
        rows.arr.push_back(row);
    };
    if (solid) {
        bind("cap.start", "face", maker.FirstShape());
        bind("cap.end", "face", maker.LastShape());
    }
    for (const auto& [name, edge] : firstSectionEdges)
        bind("side." + name, "face", maker.GeneratedFace(edge.edge));
    const bool tipFirst = pointProfile(profiles.arr.front());
    const bool tipLast = pointProfile(profiles.arr.back());
    if (!tipFirst) {
        for (const auto& [name, edge] : firstSectionEdges)
            bind("edge.start." + name, "edge", edgeByEnds(maker.FirstShape().IsNull() ? maker.Shape() : maker.FirstShape(), edge.first, edge.last));
    }
    if (!tipLast) {
        for (const auto& [name, edge] : lastSectionEdges)
            bind("edge.end." + name, "edge", edgeByEnds(maker.LastShape().IsNull() ? maker.Shape() : maker.LastShape(), edge.first, edge.last));
    }
    if (!firstWire.IsNull()) {
        int ordinal = 0;
        for (BRepTools_WireExplorer walk(firstWire); walk.More(); walk.Next(), ++ordinal) {
            TopoDS_Shape rail;
            int found = 0;
            for (NCollection_List<TopoDS_Shape>::Iterator generated(maker.Generated(walk.CurrentVertex())); generated.More(); generated.Next()) {
                if (generated.Value().ShapeType() != TopAbs_EDGE) continue;
                rail = generated.Value();
                ++found;
            }
            bind("rail." + std::to_string(ordinal), "edge", found == 1 ? rail : TopoDS_Shape());
        }
    }
    topology.set("references", rows);
    result.set("topology", topology);
    return result;
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

bool flag(const V& request, const char* key, bool fallback) {
    const V* value = request.find(key);
    if (!value) return fallback;
    if (value->t != V::Bool) throw std::runtime_error(std::string(key) + " must be a boolean");
    return value->b;
}

V tag(V result, const char* operation, const char* extraKey = nullptr, V extra = {}) {
    const V* report = result.find("report");
    if (!report) throw std::runtime_error("native surface result has no report");
    V updated = *report;
    updated.set("operation", V::makeStr(operation));
    if (extraKey) updated.set(extraKey, std::move(extra));
    result.set("report", std::move(updated));
    return result;
}

TopoDS_Shape basisOf(const Profile& profile, bool solid) {
    if (!solid) return profile.wire;
    if (!profile.closed) throw std::runtime_error("solid surface operation needs a closed profile");
    BRepBuilderAPI_MakeFace face(profile.wire, false);
    if (!face.IsDone()) throw std::runtime_error("closed profile did not make a face");
    return face.Face();
}

V extrudeSurface(const V& request) {
    const V& profileValue = required(request, "profile");
    const auto frame = frameOf(profileValue);
    const Profile profile = profileOf(profileValue, false);
    const double distance = number(required(request, "distance"), "distance");
    if (std::abs(distance) <= kTolerance) throw std::runtime_error("extrude distance must exceed tolerance");
    const bool solid = flag(request, "solid", false);
    const gp_Vec direction(frame[8] * distance, frame[9] * distance, frame[10] * distance);
    BRepPrimAPI_MakePrism prism(basisOf(profile, solid), direction, true, true);
    prism.Build();
    if (!prism.IsDone()) throw std::runtime_error("OCCT surface extrusion failed");
    TopoDS_Shape shape = prism.Shape();
    if (solid && properties(shape) < 0) shape.Reverse();
    return tag(shapeResult(shape, solid), "extrude_surface");
}

V revolveSurface(const V& request) {
    const V& profileValue = required(request, "profile");
    const auto frame = frameOf(profileValue);
    const Profile profile = profileOf(profileValue, false);
    const V& axisName = required(request, "axis");
    if (axisName.t != V::Str || (axisName.str != "X" && axisName.str != "Y" && axisName.str != "Z"))
        throw std::runtime_error("revolve axis must be X, Y or Z");
    const int axis = axisName.str == "X" ? 0 : axisName.str == "Y" ? 1 : 2;
    const double degrees = number(required(request, "angle"), "angle");
    if (!(degrees > 0.0 && degrees <= 360.0))
        throw std::runtime_error("revolve angle must be in (0, 360] degrees");
    const bool solid = flag(request, "solid", false);
    const gp_Ax1 revolution({frame[12], frame[13], frame[14]},
                            {frame[axis * 4], frame[axis * 4 + 1], frame[axis * 4 + 2]});
    const TopoDS_Shape basis = basisOf(profile, solid);
    const bool full = std::abs(degrees - 360.0) <= 1.0e-9;
    TopoDS_Shape shape;
    if (full) {
        BRepPrimAPI_MakeRevol revol(basis, revolution, true);
        revol.Build();
        if (!revol.IsDone()) throw std::runtime_error("OCCT surface revolution failed");
        shape = revol.Shape();
    } else {
        BRepPrimAPI_MakeRevol revol(basis, revolution, degrees * std::acos(-1.0) / 180.0, true);
        revol.Build();
        if (!revol.IsDone()) throw std::runtime_error("OCCT surface revolution failed");
        shape = revol.Shape();
    }
    if (solid && properties(shape) < 0) shape.Reverse();
    return tag(shapeResult(shape, solid), "revolve_surface");
}

bool sectionDefinesPlane(const Profile& section) {
    std::vector<gp_Pnt> samples;
    for (const EdgeRecord& edge : section.edges) {
        if (BRepAdaptor_Curve(edge.edge).GetType() != GeomAbs_Line) return true;
        samples.push_back(edge.first);
        samples.push_back(edge.last);
    }
    if (samples.size() < 2) return false;
    const gp_Vec chord(samples.front(), samples.back());
    if (chord.Magnitude() <= kTolerance) return false;
    for (const gp_Pnt& sample : samples) {
        if (chord.Crossed(gp_Vec(samples.front(), sample)).Magnitude() > kTolerance * chord.Magnitude())
            return true;
    }
    return false;
}

gp_Dir initialTangent(const Profile& path) {
    BRepAdaptor_Curve curve(path.edges.front().edge);
    gp_Pnt origin;
    gp_Vec tangent;
    curve.D1(curve.FirstParameter(), origin, tangent);
    if (path.edges.front().edge.Orientation() == TopAbs_REVERSED) tangent.Reverse();
    if (tangent.Magnitude() <= kTolerance) throw std::runtime_error("sweep path has no initial tangent");
    return gp_Dir(tangent);
}

TopoDS_Wire alignSectionPlane(Profile section, const gp_Dir& tangent) {
    const gp_Dir normal(section.frame[8], section.frame[9], section.frame[10]);
    if (normal.IsParallel(tangent, 1.0e-9)) return section.wire;
    const gp_Dir axis = normal.Crossed(tangent);
    gp_XYZ centroid(0, 0, 0);
    int count = 0;
    for (const EdgeRecord& edge : section.edges) {
        centroid += edge.first.XYZ();
        centroid += edge.last.XYZ();
        count += 2;
    }
    centroid /= count;
    gp_Trsf rotation;
    rotation.SetRotation(gp_Ax1(gp_Pnt(centroid), axis), normal.AngleWithRef(tangent, axis));
    BRepBuilderAPI_Transform transformed(section.wire, rotation, true);
    if (!transformed.IsDone() || transformed.Shape().ShapeType() != TopAbs_WIRE)
        throw std::runtime_error("sweep section correction failed");
    return TopoDS::Wire(transformed.Shape());
}

V sweepSurface(const V& request) {
    const bool solid = flag(request, "solid", false);
    const Profile section = profileOf(required(request, "section"), solid);
    const Profile path = profileOf(required(request, "path"), false);
    BRepOffsetAPI_MakePipeShell pipe(path.wire);
    pipe.SetMode(false); // corrected Frenet; straight paths stay parallel to the section plane
    const bool contact = flag(request, "contact", false);
    const bool correction = flag(request, "correction", true);
    // MakePipeShell accepts a wire or a vertex. A solid is the swept shell, capped by MakeSolid.
    // A straight section has no curve plane, so OCCT refuses WithCorrection; the sketch normal is that plane.
    TopoDS_Wire profile = section.wire;
    bool occtCorrection = false;
    if (correction && sectionDefinesPlane(section)) occtCorrection = true;
    else if (correction) profile = alignSectionPlane(section, initialTangent(path));
    pipe.Add(profile, contact, occtCorrection);
    pipe.Build();
    if (!pipe.IsDone()) throw std::runtime_error("OCCT sweep failed");
    if (solid && !pipe.MakeSolid()) throw std::runtime_error("OCCT sweep did not make a solid");
    TopoDS_Shape shape = pipe.Shape();
    if (solid && properties(shape) < 0) shape.Reverse();
    return tag(shapeResult(shape, solid), "sweep");
}

V offsetSurface(const V& request) {
    const V& input = required(request, "input_brep");
    if (input.t != V::Str || input.str.empty()) throw std::runtime_error("input_brep must be base64");
    const double distance = number(required(request, "distance"), "distance");
    if (std::abs(distance) <= kTolerance) throw std::runtime_error("offset distance must exceed tolerance");
    const TopoDS_Shape source = readBrep(input.str);
    if (!shapes(source, TopAbs_SOLID).empty())
        throw std::runtime_error("offset_surface requires a face or shell, not a solid");
    if (shapes(source, TopAbs_FACE).empty()) throw std::runtime_error("offset_surface input has no faces");
    BRepOffsetAPI_MakeOffsetShape maker;
    maker.PerformBySimple(source, distance);
    if (!maker.IsDone()) throw std::runtime_error("OCCT surface offset failed");
    return tag(shapeResult(maker.Shape(), false), "offset_surface",
               "offset_intersection_checked", V::makeBool(false));
}

V sewSurfaces(const V& request) {
    const V& inputs = required(request, "inputs");
    if (inputs.t != V::Arr || inputs.arr.size() < 2 || inputs.arr.size() > 16)
        throw std::runtime_error("sew needs 2..16 B-rep inputs");
    const bool solid = flag(request, "solid", false);
    BRepBuilderAPI_Sewing sewing(tolerance(request));
    for (const V& encoded : inputs.arr) {
        if (encoded.t != V::Str || encoded.str.empty()) throw std::runtime_error("sew input must be base64");
        const TopoDS_Shape shape = readBrep(encoded.str);
        if (!shapes(shape, TopAbs_SOLID).empty())
            throw std::runtime_error("sew inputs must be faces or shells, not solids");
        if (shapes(shape, TopAbs_FACE).empty()) throw std::runtime_error("sew input has no faces");
        sewing.Add(shape);
    }
    sewing.Perform();
    TopoDS_Shape sewn = sewing.SewedShape();
    if (sewn.IsNull() || shapes(sewn, TopAbs_FACE).empty()) throw std::runtime_error("OCCT sewing produced no faces");
    const int freeEdges = sewing.NbFreeEdges();
    if (solid) {
        if (freeEdges != 0) throw std::runtime_error("sew solid requires no free edges");
        const auto shells = shapes(sewn, TopAbs_SHELL);
        if (shells.size() != 1) throw std::runtime_error("sew solid requires one closed shell");
        BRepBuilderAPI_MakeSolid maker(TopoDS::Shell(shells.front()));
        maker.Build();
        if (!maker.IsDone()) throw std::runtime_error("OCCT sew did not make a solid");
        sewn = maker.Solid();
        if (properties(sewn) < 0) sewn.Reverse();
    }
    return tag(shapeResult(sewn, solid), "sew", "free_edges", V::makeNum(freeEdges));
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
        if (operation.str == "extrude_surface") return extrudeSurface(request);
        if (operation.str == "revolve_surface") return revolveSurface(request);
        if (operation.str == "sweep") return sweepSurface(request);
        if (operation.str == "offset_surface") return offsetSurface(request);
        if (operation.str == "sew") return sewSurfaces(request);
        throw std::runtime_error("unsupported surfacing operation");
    } catch (const Standard_Failure& error) {
        throw std::runtime_error(std::string("OCCT surfacing failure: ") + error.GetMessageString());
    }
}
} // namespace ark::occt
