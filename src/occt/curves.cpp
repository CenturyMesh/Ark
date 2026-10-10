#include "ark/occt_modeling.hpp"
#include "ark/sketch.hpp"
#include "common.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <stdexcept>

namespace ark::occt {
namespace {
using V = arkjson::Value;

const V& need(const V& object, const char* key) {
    const V* found = object.find(key);
    if (!found) throw std::runtime_error(std::string("Missing curve field: ") + key);
    return *found;
}

double num(const V& value, const char* what) {
    if (value.t != V::Num || !std::isfinite(value.num) || std::abs(value.num) > 1e8)
        throw std::runtime_error(std::string(what) + " must be a finite number");
    return value.num;
}

gp_Pnt xyz(const V& value, const char* what) {
    if (value.t != V::Arr || value.arr.size() != 3) throw std::runtime_error(std::string(what) + " must be [x,y,z]");
    return {num(value.arr[0], what), num(value.arr[1], what), num(value.arr[2], what)};
}

gp_Dir direction(const V& value, const char* what) {
    gp_Vec vector(xyz(value, what).XYZ());
    if (vector.Magnitude() < 1e-12) throw std::runtime_error(std::string(what) + " is zero");
    return gp_Dir(vector);
}

V ribbonMesh(const TopoDS_Shape& wire) {
    V mesh = meshData(wire);
    const V* edges = mesh.find("edges");
    V pos = V::makeArr(), idx = V::makeArr();
    auto push = [&](const gp_Pnt& point) {
        for (double coordinate : {point.X(), point.Y(), point.Z()}) pos.arr.push_back(V::makeNum(coordinate));
    };
    if (edges && edges->t == V::Arr) {
        for (const V& edge : edges->arr) {
            if (edge.t != V::Arr || edge.arr.size() < 6 || edge.arr.size() % 3) continue;
            std::vector<gp_Pnt> points;
            for (size_t i = 0; i + 2 < edge.arr.size(); i += 3)
                points.emplace_back(num(edge.arr[i], "ribbon"), num(edge.arr[i + 1], "ribbon"), num(edge.arr[i + 2], "ribbon"));
            const size_t base = pos.arr.size() / 3;
            for (size_t i = 0; i + 1 < points.size(); ++i) {
                gp_Vec forward(points[i], points[i + 1]);
                if (forward.Magnitude() < 1e-9) continue;
                gp_Vec up(0, 0, 1);
                if (std::abs(gp_Dir(forward).Dot(gp_Dir(up))) > 0.9) up = gp_Vec(0, 1, 0);
                gp_Vec side = forward.Crossed(up);
                side.Normalize();
                side *= 0.15;
                push(points[i].Translated(side));
                push(points[i].Translated(side.Reversed()));
                if (i + 1 == points.size() - 1) {
                    push(points[i + 1].Translated(side));
                    push(points[i + 1].Translated(side.Reversed()));
                }
            }
            const size_t count = pos.arr.size() / 3 - base;
            for (size_t i = 0; i + 3 < count; i += 2) {
                for (int index : {0, 1, 2, 1, 3, 2}) idx.arr.push_back(V::makeNum((double)(base + i + index)));
            }
        }
    }
    if (idx.arr.empty()) throw std::runtime_error("Curve display ribbon is empty");
    V out = V::makeObj();
    out.set("pos", std::move(pos));
    out.set("idx", std::move(idx));
    out.set("edges", edges ? *edges : V::makeArr());
    return out;
}

V curveResult(const TopoDS_Shape& wire) {
    V result = shapeResult(wire, false);
    V report = *result.find("report");
    report.set("length_mm", V::makeNum(properties(wire, 'l')));
    report.set("curve", V::makeBool(true));
    report.set("display_ribbon_mm", V::makeNum(0.3));
    report.set("display_mesh_authoritative", V::makeBool(false));
    result.set("report", report);
    result.set("mesh", ribbonMesh(wire));
    V topology=V::makeObj(),references=V::makeArr();topology.set("brep_sha256",V::makeStr(sha256(base64Decode(result.find("brep_base64")->str))));
    const auto edges=shapes(wire,TopAbs_EDGE);
    for(size_t i=0;i<edges.size();++i){V row=V::makeObj(),indices=V::makeArr();row.set("name",V::makeStr("edge."+std::to_string(i)));row.set("kind",V::makeStr("edge"));row.set("status",V::makeStr("bound"));indices.arr.push_back(V::makeNum(i));row.set("indices",indices);references.arr.push_back(row);}
    topology.set("references",references);result.set("topology",topology);
    return result;
}

void addEdge(BRepBuilderAPI_MakeWire& wire, const TopoDS_Edge& edge) {
    wire.Add(edge);
    if (!wire.IsDone()) throw std::runtime_error("Curve segments are not connected");
}

TopoDS_Edge arcEdge(const gp_Pnt& start, const gp_Pnt& end, double radius, const gp_Dir& normal, int sweep, bool major=false) {
    if (!(radius > 1e-8) || sweep != 1 && sweep != -1) throw std::runtime_error("Arc needs a positive radius and sweep +1 or -1");
    gp_Vec chord(start, end);
    const double length = chord.Magnitude();
    if (length < 1e-8 || length > radius * 2 + 1e-6) throw std::runtime_error("Arc chord must fit the radius");
    if (std::abs(gp_Vec(normal).Dot(chord.Normalized())) > 1e-10) throw std::runtime_error("Arc endpoints are not coplanar with the authored normal");
    const gp_Dir plane(normal);
    const gp_Dir left(plane.Crossed(chord.Normalized()));
    const double half = length * 0.5;
    const double offset = std::sqrt(std::max(0.0, radius * radius - half * half));
    const gp_Pnt center = gp_Pnt((start.XYZ() + end.XYZ()) * 0.5).Translated(gp_Vec(left) * ((major?offset:-offset) * sweep));
    const gp_Pnt bulge = center.Translated(gp_Vec(left) * (radius * sweep));
    GC_MakeArcOfCircle maker(start, bulge, end);
    if (!maker.IsDone()) throw std::runtime_error("Arc construction failed");
    BRepBuilderAPI_MakeEdge edge(maker.Value());
    if (!edge.IsDone()) throw std::runtime_error("Arc edge construction failed");
    return edge.Edge();
}

Vec3 vector3(const gp_Pnt& point){return {point.X(),point.Y(),point.Z()};}
gp_Pnt point3(Vec3 point){return {point.x,point.y,point.z};}
gp_Vec endTangent(const TopoDS_Edge& edge){
    BRepAdaptor_Curve curve(edge);gp_Pnt point;gp_Vec tangent;
    const bool reversed=edge.Orientation()==TopAbs_REVERSED;
    curve.D1(reversed?curve.FirstParameter():curve.LastParameter(),point,tangent);
    if(reversed)tangent.Reverse();
    if(tangent.Magnitude()<=1e-12)throw std::runtime_error("Previous segment has no nonzero end tangent");
    tangent.Normalize();return tangent;
}
TopoDS_Edge recipeArcEdge(const gp_Pnt& start,const gp_Pnt& end,const V& segment,const gp_Vec* previous){
    const V& mode=need(segment,"mode");if(mode.t!=V::Str||(mode.str!="three_point"&&mode.str!="tangent"))throw std::runtime_error("Arc mode must be three_point or tangent");
    if(segment.find("radius")||segment.find("sweep")||segment.find("major"))throw std::runtime_error("Arc recipe mode is exclusive with radius/sweep/major");
    AuthoredArc arc;
    if(mode.str=="three_point"){
        if(segment.find("tangent"))throw std::runtime_error("Three-point arc cannot also specify tangent");
        arc=arcThroughThreePoints(vector3(start),vector3(xyz(need(segment,"through"),"through")),vector3(end));
    }else{
        if(segment.find("through"))throw std::runtime_error("Tangent arc cannot also specify through");
        gp_Vec tangent;
        if(auto explicitTangent=segment.find("tangent"))tangent=gp_Vec(direction(*explicitTangent,"tangent"));
        else if(previous)tangent=*previous;
        else throw std::runtime_error("First tangent arc requires an authored start tangent");
        if(previous&&gp_Dir(tangent).Dot(gp_Dir(*previous))<1-1e-10)throw std::runtime_error("Tangent arc start direction does not continue the previous segment");
        arc=arcFromStartTangent(vector3(start),vector3(end),{tangent.X(),tangent.Y(),tangent.Z()});
    }
    if(auto authoredNormal=segment.find("normal")){
        const auto normal=direction(*authoredNormal,"normal");
        if(gp_Vec(normal).Crossed(gp_Vec(arc.normal.x,arc.normal.y,arc.normal.z)).Magnitude()>1e-10)throw std::runtime_error("Arc recipe points and tangent are not coplanar with the authored normal");
    }
    GC_MakeArcOfCircle maker(start,point3(arc.point(.5)),end);
    if(!maker.IsDone())throw std::runtime_error("Exact arc recipe construction failed");
    BRepBuilderAPI_MakeEdge edge(maker.Value());if(!edge.IsDone())throw std::runtime_error("Exact arc recipe edge failed");
    return edge.Edge();
}

TopoDS_Edge nurbsEdge(const V& segment, const gp_Pnt& cursor) {
    const V& poles = need(segment, "poles");
    if (poles.t != V::Arr || poles.arr.size() < 2 || poles.arr.size() > 64) throw std::runtime_error("NURBS curve needs 2..64 poles");
    const int degree = (int)num(need(segment, "degree"), "degree");
    const int count = (int)poles.arr.size();
    if (degree < 1 || degree > 8 || count < degree + 1) throw std::runtime_error("NURBS degree must be 1..8 and below the pole count");
    TColgp_Array1OfPnt points(1, count);
    TColStd_Array1OfReal weights(1, count);
    const V* authored = segment.find("weights");
    if (authored && (authored->t != V::Arr || (int)authored->arr.size() != count)) throw std::runtime_error("NURBS weights must match the poles");
    for (int i = 1; i <= count; ++i) {
        points.SetValue(i, xyz(poles.arr[(size_t)i - 1], "pole"));
        weights.SetValue(i, authored ? num(authored->arr[(size_t)i - 1], "weight") : 1);
        if (!(weights.Value(i) > 0)) throw std::runtime_error("NURBS weights must be positive");
    }
    if (points.Value(1).Distance(cursor) > 1e-4) throw std::runtime_error("NURBS pole does not continue the curve");
    const int unique = count - degree + 1;
    TColStd_Array1OfReal knots(1, unique);
    TColStd_Array1OfInteger mults(1, unique);
    for (int i = 1; i <= unique; ++i) {
        knots.SetValue(i, unique == 1 ? 0 : (double)(i - 1) / (double)(unique - 1));
        mults.SetValue(i, (i == 1 || i == unique) ? degree + 1 : 1);
    }
    Handle(Geom_BSplineCurve) curve = new Geom_BSplineCurve(points, weights, knots, mults, degree);
    BRepBuilderAPI_MakeEdge edge(curve);
    if (!edge.IsDone()) throw std::runtime_error("NURBS edge construction failed");
    return edge.Edge();
}

V authoredCurve(const V& request) {
    gp_Pnt cursor = request.find("start") ? xyz(need(request, "start"), "start") : gp_Pnt(0, 0, 0);
    const V& segments = need(request, "segments");
    if (segments.t != V::Arr || segments.arr.empty() || segments.arr.size() > 64)
        throw std::runtime_error("Curve needs 1..64 segments");
    BRepBuilderAPI_MakeWire wire;
    TopoDS_Edge previousEdge;bool havePrevious=false;
    for (const V& segment : segments.arr) {
        const std::string kind = need(segment, "kind").t == V::Str ? need(segment, "kind").str : "";
        TopoDS_Edge edge;
        if (kind == "line") {
            gp_Pnt end = xyz(need(segment, "to"), "to");
            if (cursor.Distance(end) < 1e-8) throw std::runtime_error("Curve line is degenerate");
            BRepBuilderAPI_MakeEdge maker(cursor, end);
            if (!maker.IsDone()) throw std::runtime_error("Curve line failed");
            edge = maker.Edge();
            cursor = end;
        } else if (kind == "arc") {
            gp_Pnt end = xyz(need(segment, "to"), "to");
            if(auto mode=segment.find("mode")){
                gp_Vec previousTangent;const gp_Vec* previous=nullptr;
                if(mode->t==V::Str&&mode->str=="tangent"&&havePrevious){previousTangent=endTangent(previousEdge);previous=&previousTangent;}
                edge=recipeArcEdge(cursor,end,segment,previous);
            }
            else{
                const gp_Dir normal = segment.find("normal") ? direction(need(segment, "normal"), "normal") : gp_Dir(0, 0, 1);
                const double sweepValue=segment.find("sweep")?num(need(segment,"sweep"),"sweep"):1;
                if(sweepValue!=1&&sweepValue!=-1)throw std::runtime_error("Arc sweep must be exactly +1 or -1");
                bool major=false;if(auto value=segment.find("major")){if(value->t!=V::Bool)throw std::runtime_error("Arc major must be boolean");major=value->b;}
                if(segment.find("through")||segment.find("tangent"))throw std::runtime_error("Arc through/tangent requires an explicit recipe mode");
                edge = arcEdge(cursor, end, num(need(segment, "radius"), "radius"), normal, (int)sweepValue,major);
            }
            cursor = end;
        } else if (kind == "nurbs") {
            edge = nurbsEdge(segment, cursor);
            cursor = xyz(segment.find("poles")->arr.back(), "pole");
        } else throw std::runtime_error("Curve segment kind must be line, arc or nurbs");
        addEdge(wire, edge);
        previousEdge=edge;havePrevious=true;
    }
    return curveResult(wire.Wire());
}

V intersectionCurve(const V& request) {
    const V& encoded = need(request, "input_brep");
    if (encoded.t != V::Str) throw std::runtime_error("Intersection input is not a B-rep");
    const V& frame = need(request, "frame");
    if (frame.t != V::Arr || frame.arr.size() != 16) throw std::runtime_error("Intersection plane frame must have 16 numbers");
    double m[16];
    for (int i = 0; i < 16; ++i) m[i] = num(frame.arr[(size_t)i], "frame");
    const gp_Pnt origin(m[12], m[13], m[14]);
    gp_Vec normal(m[8], m[9], m[10]);
    if (normal.Magnitude() < 1e-12) throw std::runtime_error("Intersection plane normal is zero");
    TopoDS_Shape solid = readBrep(encoded.str);
    BRepAlgoAPI_Section section(solid, gp_Pln(origin, gp_Dir(normal)), Standard_False);
    section.Approximation(Standard_False);
    section.Build();
    if (!section.IsDone()) throw std::runtime_error("Intersection curve failed");
    const auto edges = shapes(section.Shape(), TopAbs_EDGE);
    if (edges.empty() || edges.size() > 128) throw std::runtime_error("Intersection produced no usable edges");
    BRepBuilderAPI_MakeWire wire;
    for (const auto& edge : edges) wire.Add(TopoDS::Edge(edge));
    if (!wire.IsDone() || shapes(wire.Wire(), TopAbs_EDGE).size() != edges.size())
        throw std::runtime_error("Intersection produced more than one wire");
    return curveResult(wire.Wire());
}

V bridgeCurve(const V& request) {
    const gp_Pnt start = xyz(need(request, "p0"), "p0"), end = xyz(need(request, "p1"), "p1");
    gp_Vec out = gp_Vec(direction(need(request, "t0"), "t0"));
    gp_Vec in = gp_Vec(direction(need(request, "t1"), "t1"));
    const double span = start.Distance(end);
    if (span < 1e-6) throw std::runtime_error("Bridge endpoints coincide");
    out *= span;
    in *= span;
    const int continuity = (int)num(need(request, "continuity"), "continuity");
    TColgp_Array1OfPnt poles(1, continuity == 2 ? 6 : 4);
    if (continuity == 1) {
        poles.SetValue(1, start);
        poles.SetValue(2, start.Translated(out / 3));
        poles.SetValue(3, end.Translated(in.Reversed() / 3));
        poles.SetValue(4, end);
    } else if (continuity == 2) {
        poles.SetValue(1, start);
        poles.SetValue(2, start.Translated(out / 5));
        poles.SetValue(3, start.Translated(out * 2 / 5));
        poles.SetValue(4, end.Translated(in.Reversed() * 2 / 5));
        poles.SetValue(5, end.Translated(in.Reversed() / 5));
        poles.SetValue(6, end);
    } else throw std::runtime_error("Bridge continuity must be 1 or 2");
    Handle(Geom_BezierCurve) curve = new Geom_BezierCurve(poles);
    BRepBuilderAPI_MakeEdge edge(curve);
    if (!edge.IsDone()) throw std::runtime_error("Bridge edge failed");
    BRepBuilderAPI_MakeWire wire(edge.Edge());
    if (!wire.IsDone()) throw std::runtime_error("Bridge wire failed");
    V result = curveResult(wire.Wire());
    V report = *result.find("report");
    report.set("continuity", V::makeNum(continuity));
    report.set("curvature_matched", V::makeBool(continuity == 2));
    result.set("report", report);
    return result;
}
}

V modelCurveNative(const V& request) {
    const std::string op = need(request, "op").t == V::Str ? need(request, "op").str : "";
    if (op == "curve") return authoredCurve(request);
    if (op == "curve_brep") return curveResult(readBrep(need(request,"wire_brep").str));
    if (op == "intersection_curve") return intersectionCurve(request);
    if (op == "bridge") return bridgeCurve(request);
    throw std::runtime_error("Unsupported curve operation");
}
}
