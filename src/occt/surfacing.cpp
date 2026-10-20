#include "ark/occt_modeling.hpp"
#include "ark/occt_solid.hpp"
#include "common.hpp"
#include "surface_repair.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Iterator.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_PipeError.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <Geom2d_Curve.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomProjLib.hxx>
#include <GeomLib.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom_BoundedSurface.hxx>
#include <GeomFill_TrihedronLaw.hxx>
#include <GeomFill_CurveAndTrihedron.hxx>
#include <GeomFill_EvolvedSection.hxx>
#include <GeomFill_Sweep.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <Law_Linear.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_BezierSurface.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Line.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomAbs_Shape.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <NCollection_List.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
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
    if (kind.str == "arc") {
        const gp_Pnt center = point(required(entity, "center"), frame);
        const double radius = number(required(entity, "radius"), "radius");
        const double a0 = number(required(entity, "a0"), "a0") * (3.14159265358979323846 / 180.0);
        const double a1 = number(required(entity, "a1"), "a1") * (3.14159265358979323846 / 180.0);
        if (radius <= kTolerance) throw std::runtime_error("arc radius too small");
        const double sweep = a1 - a0;
        if (!(std::abs(sweep) > 1e-12 && std::abs(sweep) <= 6.28318530717958647692 + 1e-8))
            throw std::runtime_error("Arc sweep must be in (0, 360] degrees");
        const gp_Dir normal(frame[8], frame[9], frame[10]);
        const gp_Dir xAxis(frame[0], frame[1], frame[2]);
        const gp_Circ circle(gp_Ax2(center, normal, xAxis), radius);
        TopoDS_Edge edge;
        if (std::abs(std::abs(sweep) - 6.28318530717958647692) < 1e-6) {
            BRepBuilderAPI_MakeEdge maker(circle);
            if (!maker.IsDone()) throw std::runtime_error("arc edge construction failed");
            edge = maker.Edge();
        } else {
            const bool reverse = sweep < 0;
            Handle(Geom_Circle) geometry = new Geom_Circle(circle);
            Handle(Geom_TrimmedCurve) trimmed = new Geom_TrimmedCurve(geometry, reverse ? a1 : a0, reverse ? a0 : a1);
            BRepBuilderAPI_MakeEdge maker(trimmed);
            if (!maker.IsDone()) throw std::runtime_error("arc edge construction failed");
            edge = maker.Edge();
            if (reverse) edge.Reverse();
        }
        TopoDS_Vertex first, last;
        TopExp::Vertices(edge, first, last, true);
        return {edge, BRep_Tool::Pnt(first), BRep_Tool::Pnt(last)};
    }
    const V& points = required(entity, "points");
    const size_t count = points.t == V::Arr ? points.arr.size() : 0;
    if ((kind.str=="line"&&count!=2)||(kind.str=="bezier"&&(count<2||count>26))||(kind.str!="line"&&kind.str!="bezier"))
        throw std::runtime_error("line needs 2 points; bezier needs 2..26 controls");
    std::vector<gp_Pnt> poles;
    for (const auto& item : points.arr) poles.push_back(point(item, frame));
    if (kind.str=="line"&&poles.front().Distance(poles.back()) <= kTolerance)
        throw std::runtime_error("zero-length/openly degenerate edge");
    if (kind.str == "line") {
        BRepBuilderAPI_MakeEdge maker(poles[0], poles[1]);
        if (!maker.IsDone()) throw std::runtime_error("line edge construction failed");
        return {maker.Edge(), poles[0], poles[1]};
    }
    NCollection_Array1<gp_Pnt> controls(1, (int)count);
    for (int i = 1; i <= (int)count; ++i) controls.SetValue(i, poles[static_cast<size_t>(i - 1)]);
    Handle(Geom_BezierCurve) curve;
    if(const V* weights=entity.find("weights")){
        if(weights->t!=V::Arr||weights->arr.size()!=count)throw std::runtime_error("Bezier weights must match its controls");
        NCollection_Array1<double> rationalWeights(1,(int)count);for(int i=1;i<=(int)count;++i){double weight=number(weights->arr[(size_t)i-1],"Bezier weight");if(weight<=0)throw std::runtime_error("Bezier weights must be positive");rationalWeights.SetValue(i,weight);}curve=new Geom_BezierCurve(controls,rationalWeights);
    }else curve = new Geom_BezierCurve(controls);
    BRepBuilderAPI_MakeEdge maker(curve);
    if (!maker.IsDone()) throw std::runtime_error("Bezier edge construction failed");
    return {maker.Edge(), poles.front(), poles.back()};
}

struct Profile {
    TopoDS_Wire wire;
    std::vector<EdgeRecord> edges;
    std::array<double, 16> frame{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool closed = false;
    std::vector<size_t> sourceIndices;
};

Profile profileOf(const V& value, bool requireClosed) {
    if (const V* encoded = value.find("wire_brep")) {
        if (encoded->t != V::Str || encoded->str.empty()) throw std::runtime_error("wire_brep must be a B-rep");
        const std::vector<TopoDS_Shape> wires = shapes(readBrep(encoded->str), TopAbs_WIRE);
        if (wires.size() != 1) throw std::runtime_error("wire_brep must contain one wire");
        Profile result;
        result.wire = TopoDS::Wire(wires[0]);
        for (BRepTools_WireExplorer walk(result.wire); walk.More(); walk.Next()) {
            TopoDS_Edge edge = walk.Current();
            TopoDS_Vertex first, last;
            TopExp::Vertices(edge, first, last, true);
            if (first.IsNull() || last.IsNull()) throw std::runtime_error("wire_brep edge has no vertices");
            result.edges.push_back({edge, BRep_Tool::Pnt(first), BRep_Tool::Pnt(last)});
        }
        if (result.edges.empty()) throw std::runtime_error("wire_brep has no edges");
        result.closed = result.edges.front().first.Distance(result.edges.back().last) <= kTolerance;
        if (requireClosed && !result.closed) throw std::runtime_error("profile is not explicitly closed");
        for(size_t i=0;i<result.edges.size();++i)result.sourceIndices.push_back(i);
        return result;
    }
    const auto frame = frameOf(value);
    const V& entities = required(value, "entities");
    if (entities.t != V::Arr || entities.arr.empty() || entities.arr.size() > 128)
        throw std::runtime_error("profile needs 1..128 entities");
    Profile result;
    for (const V& entity : entities.arr) {
        if(required(entity,"kind").str=="polyline"){
            const V& points=required(entity,"points");if(points.t!=V::Arr||points.arr.size()<2||points.arr.size()>4096)throw std::runtime_error("Polyline requires 2..4096 points");for(size_t i=1;i<points.arr.size();++i){V segment=V::makeObj(),pair=V::makeArr();pair.arr={points.arr[i-1],points.arr[i]};segment.set("kind",V::makeStr("line"));segment.set("points",pair);EdgeRecord edge=edgeOf(segment,frame);if(!result.edges.empty()&&result.edges.back().last.Distance(edge.first)>kTolerance)throw std::runtime_error("Polyline is not connected to the authored profile");result.edges.push_back(edge);}continue;
        }
        if(required(entity,"kind").str=="brep"){
            const auto& encoded=required(entity,"brep_base64");if(encoded.t!=V::Str)throw std::runtime_error("Projected profile B-rep must be text");
            auto wires=shapes(readBrep(encoded.str),TopAbs_WIRE);if(wires.size()!=1)throw std::runtime_error("Projected entity must contain exactly one wire");
            gp_Trsf placement;placement.SetValues(frame[0],frame[4],frame[8],frame[12],frame[1],frame[5],frame[9],frame[13],frame[2],frame[6],frame[10],frame[14]);
            auto wire=TopoDS::Wire(transformed(wires[0],placement));
            for(BRepTools_WireExplorer walk(wire);walk.More();walk.Next()){
                TopoDS_Vertex first,last;TopExp::Vertices(walk.Current(),first,last,true);if(first.IsNull()||last.IsNull())throw std::runtime_error("Projected edge has no endpoints");
                EdgeRecord edge{walk.Current(),BRep_Tool::Pnt(first),BRep_Tool::Pnt(last)};
                if(!result.edges.empty()&&result.edges.back().last.Distance(edge.first)>kTolerance)throw std::runtime_error("Projected profile edges are not connected");result.edges.push_back(edge);
            }
            continue;
        }
        EdgeRecord edge = edgeOf(entity, frame);
        if (!result.edges.empty() && result.edges.back().last.Distance(edge.first) > kTolerance)
            throw std::runtime_error("profile entities are not connected in supplied order");
        result.edges.push_back(std::move(edge));
    }
    result.frame = frame;
    result.closed = result.edges.front().first.Distance(result.edges.back().last) <= kTolerance;
    if (requireClosed && !result.closed) throw std::runtime_error("profile is not explicitly closed");
    BRepBuilderAPI_MakeWire maker;
    for (auto& edge : result.edges) {
        maker.Add(edge.edge);
        if(!maker.IsDone())throw std::runtime_error("profile wire construction failed");
        // MakeWire can replace an incoming edge to share the wire's vertices.
        // GeneratedFace must receive that actual wire edge, not its precursor.
        edge.edge=maker.Edge();
    }
    if (!maker.IsDone()) throw std::runtime_error("profile wire construction failed");
    result.wire = maker.Wire();
    for(size_t i=0;i<result.edges.size();++i)result.sourceIndices.push_back(i);
    return result;
}

void validateLoftControls(const V& request){
    const V* controls=request.find("controls");if(!controls)return;
    if(controls->t!=V::Obj)throw std::runtime_error("Loft controls must be an object");
    for(const auto& field:controls->obj)if(field.first!="section_alignment"&&field.first!="guide_samples"&&field.first!="max_guide_deviation_mm"&&field.first!="require_ordered_contacts")throw std::runtime_error("Unknown loft control: "+field.first);
    if(const V* align=controls->find("section_alignment")){const V& profiles=required(request,"profiles");if(align->t!=V::Arr||align->arr.size()!=profiles.arr.size())throw std::runtime_error("Loft section_alignment must match section count");for(const V& item:align->arr){if(item.t!=V::Obj)throw std::runtime_error("Loft section alignment must be an object");for(const auto& field:item.obj)if(field.first!="reverse"&&field.first!="seam_edge"&&field.first!="seam_angle")throw std::runtime_error("Unknown loft section alignment field: "+field.first);if(const V* reverse=item.find("reverse");reverse&&reverse->t!=V::Bool)throw std::runtime_error("Loft reverse must be boolean");if(const V* seam=item.find("seam_edge"))integer(*seam,"seam_edge",0,127);if(const V* angle=item.find("seam_angle"))number(*angle,"seam_angle");}}
    if(const V* samples=controls->find("guide_samples"))integer(*samples,"guide_samples",2,129);
    if(const V* error=controls->find("max_guide_deviation_mm")){double value=number(*error,"max_guide_deviation_mm");if(value<kTolerance||value>.05)throw std::runtime_error("Loft guide deviation limit is restricted to 1e-6..0.05 mm");}
    if(const V* ordered=controls->find("require_ordered_contacts");ordered&&ordered->t!=V::Bool)throw std::runtime_error("Loft require_ordered_contacts must be boolean");
    if(!request.find("guides")&&(controls->find("guide_samples")||controls->find("max_guide_deviation_mm")||controls->find("require_ordered_contacts")))throw std::runtime_error("Loft guide controls require authored guides");
}

Profile alignedSection(const V& request,size_t index,bool closed){
    const V& value=required(request,"profiles").arr[index];Profile result=profileOf(value,closed);
    const V* controls=request.find("controls"),*alignment=controls?controls->find("section_alignment"):nullptr;
    if(!alignment)return result;
    const V& item=alignment->arr[index];
    const size_t seam=item.find("seam_edge")?(size_t)integer(*item.find("seam_edge"),"seam_edge",0,127):0;
    const double angle=item.find("seam_angle")?number(*item.find("seam_angle"),"seam_angle"):0;
    if(seam>=result.edges.size())throw std::runtime_error("Loft seam_edge is outside section edge count");
    if(seam&&!result.closed)throw std::runtime_error("Loft seam_edge requires a closed profile");
    if(angle!=0){
        if(result.edges.size()!=1||!result.closed)throw std::runtime_error("Loft seam_angle requires one closed analytic circle");
        const BRepAdaptor_Curve source(result.edges.front().edge);if(source.GetType()!=GeomAbs_Circle)throw std::runtime_error("Loft seam_angle requires one closed analytic circle");
        gp_Circ circle=source.Circle();gp_Ax2 position=circle.Position();gp_Trsf rotate;rotate.SetRotation(gp_Ax1(circle.Location(),circle.Axis().Direction()),angle*3.14159265358979323846/180.);position.Transform(rotate);circle.SetPosition(position);
        BRepBuilderAPI_MakeEdge maker(circle);if(!maker.IsDone())throw std::runtime_error("Loft circle seam construction failed");const gp_Pnt first=circle.Location().Translated(gp_Vec(circle.XAxis().Direction())*circle.Radius());result.edges[0]={maker.Edge(),first,first};
    }
    if(seam){std::rotate(result.edges.begin(),result.edges.begin()+seam,result.edges.end());std::rotate(result.sourceIndices.begin(),result.sourceIndices.begin()+seam,result.sourceIndices.end());}
    if(const V* reverse=item.find("reverse");reverse&&reverse->b){std::reverse(result.edges.begin(),result.edges.end());std::reverse(result.sourceIndices.begin(),result.sourceIndices.end());for(EdgeRecord& edge:result.edges){edge.edge.Reverse();std::swap(edge.first,edge.last);}}
    BRepBuilderAPI_MakeWire wire;for(const EdgeRecord& edge:result.edges){wire.Add(edge.edge);if(!wire.IsDone())throw std::runtime_error("Loft aligned section is not connected");}result.wire=wire.Wire();return result;
}

bool flag(const V& request,const char* key);
void configureLoftMaker(BRepOffsetAPI_ThruSections& maker,const V& request,bool guided){
    const bool compatible=request.find("compatible")?flag(request,"compatible"):guided;
    maker.CheckCompatibility(compatible);
    if(const V* degree=request.find("max_degree"))maker.SetMaxDegree(integer(*degree,"max_degree",1,8));
    if(flag(request,"smooth")){maker.SetSmoothing(true);int continuity=request.find("continuity")?integer(*request.find("continuity"),"continuity",0,2):1;maker.SetContinuity(continuity==0?GeomAbs_C0:continuity==2?GeomAbs_C2:GeomAbs_C1);}
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

gp_Pnt centroidOf(const Profile& profile) {
    gp_XYZ sum(0, 0, 0);
    int count = 0;
    for (const EdgeRecord& edge : profile.edges) {
        BRepAdaptor_Curve curve(edge.edge);
        const double first = curve.FirstParameter(), last = curve.LastParameter();
        for (int station = 0; station < 8; ++station) {
            const double t = first + (last - first) * (station + 0.5) / 8.0;
            sum += curve.Value(t).XYZ();
            ++count;
        }
    }
    if (!count) throw std::runtime_error("Loft section has no curve to guide");
    return gp_Pnt(sum / count);
}

TopoDS_Edge trimRange(const TopoDS_Edge& edge, double u0, double u1) {
    if (u1 - u0 <= 1e-8) return {};
    double first = 0, last = 0;
    Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
    if (curve.IsNull()) throw std::runtime_error("Loft section edge has no curve");
    Handle(Geom_TrimmedCurve) segment = new Geom_TrimmedCurve(curve, u0, u1);
    BRepBuilderAPI_MakeEdge maker(segment);
    if (!maker.IsDone()) throw std::runtime_error("Loft guide split failed");
    return maker.Edge();
}

TopoDS_Edge trimCircle(const TopoDS_Edge& edge, double u0, double span) {
    BRepAdaptor_Curve adaptor(edge);
    if (adaptor.GetType() != GeomAbs_Circle) throw std::runtime_error("Loft guide split crossed a non-periodic edge");
    Handle(Geom_Circle) circle = new Geom_Circle(adaptor.Circle());
    Handle(Geom_TrimmedCurve) segment = new Geom_TrimmedCurve(circle, u0, u0 + span);
    BRepBuilderAPI_MakeEdge maker(segment);
    if (!maker.IsDone()) throw std::runtime_error("Loft guide split failed");
    return maker.Edge();
}

struct GuideMark { int edge = -1; double param = 0; int guide = -1; gp_Pnt point; };

GuideMark locatePoint(const Profile& profile, const gp_Pnt& point, int guide) {
    GuideMark best;
    double bestDist = 1e9;
    for (int index = 0; index < static_cast<int>(profile.edges.size()); ++index) {
        double first = 0, last = 0;
        Handle(Geom_Curve) curve = BRep_Tool::Curve(profile.edges[static_cast<size_t>(index)].edge, first, last);
        if (curve.IsNull()) continue;
        GeomAPI_ProjectPointOnCurve projector(point, curve, first, last);
        if (projector.NbPoints() < 1 || projector.LowerDistance() >= bestDist) continue;
        bestDist = projector.LowerDistance();
        best.edge = index;
        best.param = projector.LowerDistanceParameter();
        best.point = projector.NearestPoint();
        best.guide = guide;
    }
    if (best.edge < 0 || bestDist > 1e-3) throw std::runtime_error("Loft guide does not meet a section");
    return best;
}

std::vector<TopoDS_Edge> walkForward(const Profile& profile, const GuideMark& from, const GuideMark& to, bool wrap) {
    std::vector<TopoDS_Edge> out;
    const int count = static_cast<int>(profile.edges.size());
    auto push = [&](const TopoDS_Edge& edge) { if (!edge.IsNull()) out.push_back(edge); };
    auto curveOf = [&](const TopoDS_Edge& edge, double& first, double& last) {
        if (BRep_Tool::Curve(edge, first, last).IsNull()) throw std::runtime_error("Loft section edge has no curve");
    };
    if (from.edge == to.edge && !wrap) {
        push(trimRange(profile.edges[static_cast<size_t>(from.edge)].edge, from.param, to.param));
    } else if (from.edge == to.edge && wrap) {
        BRepAdaptor_Curve adaptor(profile.edges[static_cast<size_t>(from.edge)].edge);
        const double span = (adaptor.LastParameter() - from.param) + (to.param - adaptor.FirstParameter());
        if (span <= 1e-8) throw std::runtime_error("Loft guide split is empty");
        push(trimCircle(profile.edges[static_cast<size_t>(from.edge)].edge, from.param, span));
    } else if (!wrap) {
        if (from.edge > to.edge) throw std::runtime_error("Loft guide split walked backwards");
        double first = 0, last = 0;
        curveOf(profile.edges[static_cast<size_t>(from.edge)].edge, first, last);
        push(trimRange(profile.edges[static_cast<size_t>(from.edge)].edge, from.param, last));
        for (int edge = from.edge + 1; edge < to.edge; ++edge) push(profile.edges[static_cast<size_t>(edge)].edge);
        curveOf(profile.edges[static_cast<size_t>(to.edge)].edge, first, last);
        push(trimRange(profile.edges[static_cast<size_t>(to.edge)].edge, first, to.param));
    } else {
        double first = 0, last = 0;
        curveOf(profile.edges[static_cast<size_t>(from.edge)].edge, first, last);
        push(trimRange(profile.edges[static_cast<size_t>(from.edge)].edge, from.param, last));
        int guard = 0;
        for (int edge = (from.edge + 1) % count; edge != to.edge; edge = (edge + 1) % count) {
            if (++guard > count) throw std::runtime_error("Loft guide split left the wire");
            push(profile.edges[static_cast<size_t>(edge)].edge);
        }
        curveOf(profile.edges[static_cast<size_t>(to.edge)].edge, first, last);
        push(trimRange(profile.edges[static_cast<size_t>(to.edge)].edge, first, to.param));
    }
    if (out.empty()) throw std::runtime_error("Loft guide split is empty");
    return out;
}

V guidedResult(const TopoDS_Shape& shape, bool solid, const gp_Pnt& start, const gp_Pnt& end) {
    V result = shapeResult(shape, solid), topology = V::makeObj(), rows = V::makeArr();
    topology.set("brep_sha256", V::makeStr(sha256(base64Decode(required(result, "brep_base64").str))));
    auto bind = [&](const std::string& name, const TopoDS_Shape& source) {
        V row = V::makeObj(), indices = V::makeArr();
        int index = -1, ordinal = 0;
        for (const TopoDS_Shape& candidate : shapes(shape, TopAbs_FACE)) {
            if (candidate.IsSame(source)) { index = ordinal; break; }
            ++ordinal;
        }
        row.set("name", V::makeStr(name));
        row.set("kind", V::makeStr("face"));
        row.set("status", V::makeStr(index < 0 ? "deleted" : "bound"));
        if (index >= 0) indices.arr.push_back(V::makeNum(index));
        row.set("indices", indices);
        rows.arr.push_back(std::move(row));
    };
    int starts = 0, ends = 0, sides = 0;
    for (const TopoDS_Shape& item : shapes(shape, TopAbs_FACE)) {
        TopoDS_Face face = TopoDS::Face(item);
        BRepAdaptor_Surface surface(face);
        if (solid && surface.GetType() == GeomAbs_Plane) {
            gp_XYZ sum(0, 0, 0);
            int count = 0;
            for (const TopoDS_Shape& vertex : shapes(face, TopAbs_VERTEX)) {
                sum += BRep_Tool::Pnt(TopoDS::Vertex(vertex)).XYZ();
                ++count;
            }
            const gp_Pnt mid(count ? sum / count : gp_Pnt());
            const bool atStart = mid.Distance(start) <= mid.Distance(end);
            int& slot = atStart ? starts : ends;
            bind(std::string(atStart ? "cap.start" : "cap.end") + (slot ? "." + std::to_string(slot) : ""), face);
            ++slot;
        } else bind("side." + std::to_string(sides++), face);
    }
    topology.set("references", rows);
    result.set("topology", topology);
    V report = *result.find("report");
    report.set("operation", V::makeStr("loft"));
    result.set("report", std::move(report));
    return result;
}

double directedMark(const Profile& profile,const GuideMark& mark){
    double length=0;
    for(int i=0;i<mark.edge;++i){BRepAdaptor_Curve curve(profile.edges[(size_t)i].edge);length+=GCPnts_AbscissaPoint::Length(curve,1e-8);}
    const auto& edge=profile.edges[(size_t)mark.edge].edge;BRepAdaptor_Curve curve(edge);
    const double origin=edge.Orientation()==TopAbs_REVERSED?curve.LastParameter():curve.FirstParameter();
    if(std::abs(origin-mark.param)>1e-12)length+=GCPnts_AbscissaPoint::Length(curve,std::min(origin,mark.param),std::max(origin,mark.param),1e-8);
    return length;
}

V loftEvidence(V result,const V& request){
    V report=*result.find("report"),sectionRows=V::makeArr();
    const V& profiles=required(request,"profiles");
    std::vector<Profile> sections;
    std::vector<size_t> indices;
    for(size_t i=0;i<profiles.arr.size();++i){V row=V::makeObj();row.set("section",V::makeNum(i));if(pointProfile(profiles.arr[i])){row.set("point",V::makeBool(true));sectionRows.arr.push_back(row);continue;}Profile section=alignedSection(request,i,false);row.set("edge_count",V::makeNum(section.edges.size()));row.set("closed",V::makeBool(section.closed));row.set("start_mm",vec(section.edges.front().first.XYZ()));row.set("end_mm",vec(section.edges.back().last.XYZ()));sectionRows.arr.push_back(row);sections.push_back(std::move(section));indices.push_back(i);}
    report.set("section_alignment",sectionRows);
    report.set("section_alignment_applied",V::makeBool(request.find("controls")&&request.find("controls")->find("section_alignment")));
    report.set("self_intersections_checked",V::makeBool(false));
    const V* values=request.find("guides");if(!values||values->arr.empty()){result.set("report",report);return result;}
    const V* controls=request.find("controls");
    const int samples=controls&&controls->find("guide_samples")?integer(*controls->find("guide_samples"),"guide_samples",2,129):17;
    const double maxError=controls&&controls->find("max_guide_deviation_mm")?number(*controls->find("max_guide_deviation_mm"),"max_guide_deviation_mm"):.05;
    const bool enforce=controls!=nullptr;
    const bool ordered=controls?(!controls->find("require_ordered_contacts")||flag(*controls,"require_ordered_contacts")):false;
    const TopoDS_Shape shape=readBrep(required(result,"brep_base64").str);
    V guideRows=V::makeArr();double maximum=0,maxContact=0;
    std::vector<std::vector<std::pair<double,size_t>>> orderBySection(sections.size());
    for(size_t g=0;g<values->arr.size();++g){Profile guide=profileOf(values->arr[g],false);V row=V::makeObj(),contacts=V::makeArr();double error=0,previous=-1;int sense=0;unsigned checks=0;
        for(size_t s=0;s<sections.size();++s){BRepExtrema_DistShapeShape contact(guide.wire,sections[s].wire);contact.Perform();if(!contact.IsDone()||contact.NbSolution()<1)throw std::runtime_error("Loft guide contact measurement failed");const double gap=contact.Value();maxContact=std::max(maxContact,gap);if(enforce&&gap>kTolerance)throw std::runtime_error("Loft guide "+std::to_string(g)+" misses section "+std::to_string(indices[s])+" by "+std::to_string(gap)+" mm");const GuideMark mark=locatePoint(guide,contact.PointOnShape1(1),(int)g);const double position=directedMark(guide,mark);if(ordered&&s){double delta=position-previous;if(std::abs(delta)<=kTolerance)throw std::runtime_error("Loft guide contacts coincide or repeat a section station");int observedSense=delta>0?1:-1;if(sense&&sense!=observedSense)throw std::runtime_error("Loft guide contacts do not follow section order");sense=observedSense;}previous=position;V item=V::makeObj();item.set("section",V::makeNum(indices[s]));item.set("gap_mm",V::makeNum(gap));item.set("guide_distance_mm",V::makeNum(position));contacts.arr.push_back(item);const GuideMark sectionMark=locatePoint(sections[s],contact.PointOnShape2(1),(int)g);orderBySection[s].push_back({directedMark(sections[s],sectionMark),g});}
        auto endpointGap=[&](const gp_Pnt& endpoint,const Profile& section){BRepBuilderAPI_MakeVertex vertex(endpoint);BRepExtrema_DistShapeShape distance(vertex.Vertex(),section.wire);distance.Perform();if(!distance.IsDone()||distance.NbSolution()<1)throw std::runtime_error("Loft guide endpoint coherence measurement failed");return distance.Value();};
        const double direct=std::max(endpointGap(guide.edges.front().first,sections.front()),endpointGap(guide.edges.back().last,sections.back()));
        const double reversed=std::max(endpointGap(guide.edges.front().first,sections.back()),endpointGap(guide.edges.back().last,sections.front()));
        const double endpointError=std::min(direct,reversed);
        if(ordered&&endpointError>kTolerance)throw std::runtime_error("Loft guide endpoints do not coherently meet the first and last sections");
        for(const EdgeRecord& edge:guide.edges){BRepAdaptor_Curve curve(edge.edge);for(int sample=0;sample<samples;++sample){const gp_Pnt p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*sample/(samples-1.));BRepBuilderAPI_MakeVertex vertex(p);BRepExtrema_DistShapeShape distance(vertex.Vertex(),shape);distance.Perform();if(!distance.IsDone()||distance.NbSolution()<1)throw std::runtime_error("Loft guide surface deviation measurement failed");error=std::max(error,distance.Value());++checks;}}
        if(enforce&&error>maxError)throw std::runtime_error("Loft guide "+std::to_string(g)+" exceeds sampled surface deviation limit: "+std::to_string(error)+" mm > "+std::to_string(maxError));
        maximum=std::max(maximum,error);row.set("guide",V::makeNum(g));row.set("contacts",contacts);row.set("endpoint_max_gap_mm",V::makeNum(endpointError));row.set("sample_count",V::makeNum(checks));row.set("observed_max_surface_deviation_mm",V::makeNum(error));row.set("contact_order_direction",V::makeNum(sense));guideRows.arr.push_back(row);
    }
    if(ordered&&values->arr.size()>1){std::vector<size_t> baseline;for(size_t s=0;s<orderBySection.size();++s){auto& marks=orderBySection[s];std::sort(marks.begin(),marks.end());for(size_t i=1;i<marks.size();++i)if(marks[i].first-marks[i-1].first<=kTolerance)throw std::runtime_error("Loft guides have coincident section contacts");std::vector<size_t> order;for(const auto& mark:marks)order.push_back(mark.second);if(sections[s].closed){auto zero=std::find(order.begin(),order.end(),0);std::rotate(order.begin(),zero,order.end());}if(s==0)baseline=order;else if(order!=baseline)throw std::runtime_error("Loft guide cyclic order changes across sections; explicit section alignment is required");}}
    report.set("guide_evidence",guideRows);report.set("guide_max_contact_gap_mm",V::makeNum(maxContact));report.set("guide_observed_max_surface_deviation_mm",V::makeNum(maximum));
    report.set("guide_deviation_limit_mm",V::makeNum(maxError));report.set("guide_deviation_enforced",V::makeBool(enforce));report.set("guide_contact_order_checked",V::makeBool(ordered));
    report.set("guide_between_samples_bound_verified",V::makeBool(false));report.set("silhouette_bounds_mm",*report.find("bounds_mm"));report.set("silhouette_scope",V::makeStr("exact_axis_aligned_BRep_bounds; no view-dependent contour qualification"));
    result.set("report",report);return result;
}

V guidedLoft(const V& request) {
    const V& profiles = required(request, "profiles");
    if (profiles.t != V::Arr || profiles.arr.size() < 2 || profiles.arr.size() > 32)
        throw std::runtime_error("loft needs 2..32 profiles");
    const V* solidValue = request.find("solid");
    const V* ruledValue = request.find("ruled");
    if ((solidValue && solidValue->t != V::Bool) || (ruledValue && ruledValue->t != V::Bool))
        throw std::runtime_error("solid and ruled must be booleans");
    const bool solid = solidValue && solidValue->b;
    if (ruledValue && ruledValue->b) throw std::runtime_error("A ruled loft cannot follow guide curves");
    const V& guideValues = required(request, "guides");
    if (guideValues.t != V::Arr || guideValues.arr.empty() || guideValues.arr.size() > 8)
        throw std::runtime_error("Loft guides need 1..8 curves");
    struct Section { Profile profile; gp_Pnt centroid; };
    std::vector<Section> sections;
    for (size_t sectionIndex=0;sectionIndex<profiles.arr.size();++sectionIndex) {
        const V& value=profiles.arr[sectionIndex];
        if (pointProfile(value)) throw std::runtime_error("A point section cannot be used with guide curves");
        Section section;
        section.profile = alignedSection(request,sectionIndex,solid);
        section.centroid = centroidOf(section.profile);
        sections.push_back(std::move(section));
    }
    std::vector<Profile> guides;
    for (const V& value : guideValues.arr) {
        if (pointProfile(value)) throw std::runtime_error("Loft guide must be an open curve");
        Profile guide = profileOf(value, false);
        if (guide.closed) throw std::runtime_error("Loft guide must be an open curve");
        guides.push_back(std::move(guide));
    }
    if (guides.size() == 1) {
        // A contact sweep keeps the section width, so a guide outside that width never pulls the loft.
        struct Span { gp_Pnt from; gp_Pnt to; gp_Dir normal; gp_Pnt pierce; TopoDS_Wire wire; double start; double length; };
        struct Station { double t; TopoDS_Wire wire; bool authored; };
        std::vector<Span> spans;
        std::vector<Station> stations;
        double traveled = 0;
        for (size_t index = 0; index + 1 < sections.size(); ++index) {
            const double length = sections[index].centroid.Distance(sections[index + 1].centroid);
            if (length <= 1e-6) throw std::runtime_error("Loft guide spine needs separated section centroids");
            BRepExtrema_DistShapeShape dist(guides.front().wire, sections[index].profile.wire);
            dist.Perform();
            if (!dist.IsDone() || dist.NbSolution() < 1 || dist.Value() > 1e-3)
                throw std::runtime_error("Loft guide does not meet a section");
            const GuideMark pierce = locatePoint(sections[index].profile, dist.PointOnShape2(1), 0);
            const gp_Dir normal(sections[index].profile.frame[8], sections[index].profile.frame[9], sections[index].profile.frame[10]);
            if (index == 0) stations.push_back({0, sections[index].profile.wire, true});
            spans.push_back({sections[index].centroid, sections[index + 1].centroid, normal, pierce.point,
                             sections[index].profile.wire, traveled, length});
            traveled += length;
            stations.push_back({traveled, sections[index + 1].profile.wire, true});
        }
        BRepExtrema_DistShapeShape endDist(guides.front().wire, sections.back().profile.wire);
        endDist.Perform();
        if (!endDist.IsDone() || endDist.NbSolution() < 1 || endDist.Value() > 1e-3)
            throw std::runtime_error("Loft guide does not meet a section");
        auto addSample = [&](const gp_Pnt& sample) {
            int best = -1;
            double bestT = 0, bestDist = 1e9;
            gp_Pnt station;
            for (size_t index = 0; index < spans.size(); ++index) {
                const gp_Vec chord(spans[index].from, spans[index].to);
                const double length2 = chord.SquareMagnitude();
                const double u = gp_Vec(spans[index].from, sample).Dot(chord) / length2;
                if (u <= 0.02 || u >= 0.98) continue;
                const gp_Pnt projected = spans[index].from.Translated(chord * u);
                const double distance = sample.Distance(projected);
                if (distance < bestDist) { bestDist = distance; best = static_cast<int>(index); bestT = u; station = projected; }
            }
            if (best < 0) return;
            const Span& span = spans[static_cast<size_t>(best)];
            const gp_Vec axis(span.normal);
            gp_Vec base(span.from, span.pierce);
            base -= axis * base.Dot(axis);
            gp_Vec offset(station, sample);
            offset -= axis * offset.Dot(axis);
            if (base.Magnitude() <= 1e-6 || offset.Magnitude() <= 1e-6)
                throw std::runtime_error("Loft guide scale is degenerate");
            const double scale = offset.Magnitude() / base.Magnitude();
            if (scale < 1e-3 || scale > 100) throw std::runtime_error("Loft guide scale is degenerate");
            const double angle = std::atan2(base.Crossed(offset).Dot(axis), base.Dot(offset));
            gp_Trsf scaled, rotated, moved;
            scaled.SetScale(span.from, scale);
            rotated.SetRotation(gp_Ax1(span.from, span.normal), angle);
            moved.SetTranslation(span.from, station);
            BRepBuilderAPI_Transform transformer(span.wire, moved * rotated * scaled, true);
            if (!transformer.IsDone() || transformer.Shape().ShapeType() != TopAbs_WIRE)
                throw std::runtime_error("Loft guide section failed");
            stations.push_back({span.start + bestT * span.length, TopoDS::Wire(transformer.Shape()), false});
        };
        for (const EdgeRecord& edge : guides.front().edges) {
            BRepAdaptor_Curve curve(edge.edge);
            const double first = curve.FirstParameter(), last = curve.LastParameter();
            for (int step = 0; step <= 4; ++step) addSample(curve.Value(first + (last - first) * step / 4.0));
        }
        std::sort(stations.begin(), stations.end(), [](const Station& a, const Station& b) { return a.t < b.t; });
        std::vector<Station> ordered;
        for (const Station& station : stations) {
            if (!ordered.empty() && std::abs(station.t - ordered.back().t) <= 0.2) {
                if (station.authored && !ordered.back().authored) ordered.back() = station;
                continue;
            }
            ordered.push_back(station);
        }
        if (ordered.size() < 2) throw std::runtime_error("Loft guide spine failed");
        BRepOffsetAPI_ThruSections maker(solid, false, tolerance(request));
        configureLoftMaker(maker,request,true);
        for (const Station& station : ordered) maker.AddWire(station.wire);
        maker.Build();
        if (!maker.IsDone()) throw std::runtime_error("OCCT guided loft failed");
        TopoDS_Shape shape = maker.Shape();
        if (solid && properties(shape) < 0) shape.Reverse();
        for (const EdgeRecord& edge : guides.front().edges) {
            for (const gp_Pnt& vertex : {edge.first, edge.last}) {
                BRepBuilderAPI_MakeVertex probe(vertex);
                BRepExtrema_DistShapeShape distance(probe.Vertex(), shape);
                distance.Perform();
                if (!distance.IsDone() || distance.NbSolution() < 1 || distance.Value() > 0.05)
                    throw std::runtime_error("Loft guide is not on the surface");
            }
        }
        return loftEvidence(guidedResult(shape, solid, sections.front().centroid, sections.back().centroid),request);
    }
    if (guides.size() == 2) {
        struct Station { double t; TopoDS_Wire wire; bool authored; };
        std::vector<Station> stations;
        double traveled = 0;
        auto crossPlane = [](const Profile& guide, const gp_Pnt& origin, const gp_Dir& normal) {
            auto side = [&](const BRepAdaptor_Curve& curve, double t) {
                return gp_Vec(origin, curve.Value(t)).Dot(gp_Vec(normal));
            };
            for (const EdgeRecord& edge : guide.edges) {
                BRepAdaptor_Curve curve(edge.edge);
                const double first = curve.FirstParameter(), last = curve.LastParameter();
                double prevT = first, prevS = side(curve, first);
                if (std::abs(prevS) <= 1e-6) return curve.Value(first);
                for (int step = 1; step <= 16; ++step) {
                    const double t = first + (last - first) * step / 16.0;
                    const double s = side(curve, t);
                    if (prevS * s <= 0) {
                        double a = prevT, b = t, sa = prevS;
                        for (int cut = 0; cut < 24; ++cut) {
                            const double mid = 0.5 * (a + b);
                            const double sm = side(curve, mid);
                            if (sa * sm <= 0) b = mid;
                            else { a = mid; sa = sm; }
                        }
                        return curve.Value(0.5 * (a + b));
                    }
                    prevT = t;
                    prevS = s;
                }
            }
            throw std::runtime_error("Loft guide does not cross a section station");
        };
        for (size_t index = 0; index + 1 < sections.size(); ++index) {
            const gp_Pnt from = sections[index].centroid, to = sections[index + 1].centroid;
            const double length = from.Distance(to);
            if (length <= 1e-6) throw std::runtime_error("Loft guide spine needs separated section centroids");
            const gp_Dir spine(gp_Vec(from, to));
            const gp_Dir normal(sections[index].profile.frame[8], sections[index].profile.frame[9], sections[index].profile.frame[10]);
            gp_Pnt pierce[2];
            for (int guideIndex = 0; guideIndex < 2; ++guideIndex) {
                BRepExtrema_DistShapeShape dist(guides[static_cast<size_t>(guideIndex)].wire, sections[index].profile.wire);
                dist.Perform();
                if (!dist.IsDone() || dist.NbSolution() < 1 || dist.Value() > 1e-3)
                    throw std::runtime_error("Loft guide does not meet a section");
                pierce[guideIndex] = locatePoint(sections[index].profile, dist.PointOnShape2(1), guideIndex).point;
            }
            if (index == 0) stations.push_back({0, sections[index].profile.wire, true});
            std::vector<double> cuts;
            for (const Profile& guide : guides) for (const EdgeRecord& edge : guide.edges) {
                for (const gp_Pnt& vertex : {edge.first, edge.last}) {
                    const double u = gp_Vec(from, vertex).Dot(gp_Vec(from, to)) / (length * length);
                    if (u > 0.02 && u < 0.98) cuts.push_back(u);
                }
            }
            std::sort(cuts.begin(), cuts.end());
            double previous = -1;
            for (double u : cuts) {
                if (previous >= 0 && std::abs(u - previous) * length <= 0.2) continue;
                previous = u;
                const gp_Pnt station = from.Translated(gp_Vec(from, to) * u);
                const gp_Pnt targetA = crossPlane(guides[0], station, spine);
                const gp_Pnt targetB = crossPlane(guides[1], station, spine);
                const gp_Vec source(pierce[0], pierce[1]), destination(targetA, targetB);
                if (source.Magnitude() <= 1e-6 || destination.Magnitude() <= 1e-6)
                    throw std::runtime_error("Loft guide scale is degenerate");
                const double scale = destination.Magnitude() / source.Magnitude();
                if (scale < 1e-3 || scale > 100) throw std::runtime_error("Loft guide scale is degenerate");
                const double angle = std::atan2(source.Crossed(destination).Dot(gp_Vec(normal)), source.Dot(destination));
                gp_Trsf scaled, rotated, moved;
                scaled.SetScale(pierce[0], scale);
                rotated.SetRotation(gp_Ax1(pierce[0], normal), angle);
                moved.SetTranslation(pierce[0], targetA);
                BRepBuilderAPI_Transform transformer(sections[index].profile.wire, moved * rotated * scaled, true);
                if (!transformer.IsDone() || transformer.Shape().ShapeType() != TopAbs_WIRE)
                    throw std::runtime_error("Loft guide section failed");
                stations.push_back({traveled + u * length, TopoDS::Wire(transformer.Shape()), false});
            }
            traveled += length;
            stations.push_back({traveled, sections[index + 1].profile.wire, true});
        }
        BRepExtrema_DistShapeShape endDist(guides[0].wire, sections.back().profile.wire);
        endDist.Perform();
        BRepExtrema_DistShapeShape endDistB(guides[1].wire, sections.back().profile.wire);
        endDistB.Perform();
        if (!endDist.IsDone() || endDist.NbSolution() < 1 || endDist.Value() > 1e-3 ||
            !endDistB.IsDone() || endDistB.NbSolution() < 1 || endDistB.Value() > 1e-3)
            throw std::runtime_error("Loft guide does not meet a section");
        std::sort(stations.begin(), stations.end(), [](const Station& a, const Station& b) { return a.t < b.t; });
        std::vector<Station> ordered;
        for (const Station& station : stations) {
            if (!ordered.empty() && std::abs(station.t - ordered.back().t) <= 0.2) {
                if (station.authored && !ordered.back().authored) ordered.back() = station;
                continue;
            }
            ordered.push_back(station);
        }
        if (ordered.size() < 2) throw std::runtime_error("Loft guide spine failed");
        BRepOffsetAPI_ThruSections maker(solid, false, tolerance(request));
        configureLoftMaker(maker,request,true);
        for (const Station& station : ordered) maker.AddWire(station.wire);
        maker.Build();
        if (!maker.IsDone()) throw std::runtime_error("OCCT guided loft failed");
        TopoDS_Shape shape = maker.Shape();
        if (solid && properties(shape) < 0) shape.Reverse();
        for (const Profile& guide : guides) for (const EdgeRecord& edge : guide.edges) {
            for (const gp_Pnt& vertex : {edge.first, edge.last}) {
                BRepBuilderAPI_MakeVertex probe(vertex);
                BRepExtrema_DistShapeShape distance(probe.Vertex(), shape);
                distance.Perform();
                if (!distance.IsDone() || distance.NbSolution() < 1 || distance.Value() > 0.05)
                    throw std::runtime_error("Loft guide is not on the surface");
            }
        }
        return loftEvidence(guidedResult(shape, solid, sections.front().centroid, sections.back().centroid),request);
    }
    struct Station { double t; TopoDS_Wire wire; bool authored; };
    std::vector<Station> stations;
    double traveled = 0;
    auto crossPlane = [](const Profile& guide, const gp_Pnt& origin, const gp_Dir& normal) {
        auto side = [&](const BRepAdaptor_Curve& curve, double t) {
            return gp_Vec(origin, curve.Value(t)).Dot(gp_Vec(normal));
        };
        for (const EdgeRecord& edge : guide.edges) {
            BRepAdaptor_Curve curve(edge.edge);
            const double first = curve.FirstParameter(), last = curve.LastParameter();
            double prevT = first, prevS = side(curve, first);
            if (std::abs(prevS) <= 1e-6) return curve.Value(first);
            for (int step = 1; step <= 16; ++step) {
                const double t = first + (last - first) * step / 16.0;
                const double s = side(curve, t);
                if (prevS * s <= 0) {
                    double a = prevT, b = t, sa = prevS;
                    for (int cut = 0; cut < 24; ++cut) {
                        const double mid = 0.5 * (a + b);
                        const double sm = side(curve, mid);
                        if (sa * sm <= 0) b = mid;
                        else { a = mid; sa = sm; }
                    }
                    return curve.Value(0.5 * (a + b));
                }
                prevT = t;
                prevS = s;
            }
        }
        throw std::runtime_error("Loft guide does not cross a section station");
    };
    for (size_t index = 0; index + 1 < sections.size(); ++index) {
        const gp_Pnt from = sections[index].centroid, to = sections[index + 1].centroid;
        const double length = from.Distance(to);
        if (length <= 1e-6) throw std::runtime_error("Loft guide spine needs separated section centroids");
        const gp_Dir spine(gp_Vec(from, to));
        std::vector<GuideMark> pierces;
        for (size_t guideIndex = 0; guideIndex < guides.size(); ++guideIndex) {
            BRepExtrema_DistShapeShape dist(guides[guideIndex].wire, sections[index].profile.wire);
            dist.Perform();
            if (!dist.IsDone() || dist.NbSolution() < 1 || dist.Value() > 1e-3)
                throw std::runtime_error("Loft guide does not meet a section");
            pierces.push_back(locatePoint(sections[index].profile, dist.PointOnShape2(1), static_cast<int>(guideIndex)));
        }
        std::sort(pierces.begin(), pierces.end(), [](const GuideMark& a, const GuideMark& b) {
            if (a.edge != b.edge) return a.edge < b.edge;
            return a.param < b.param;
        });
        for (size_t mark = 1; mark < pierces.size(); ++mark)
            if (pierces[mark].edge == pierces[mark - 1].edge && std::abs(pierces[mark].param - pierces[mark - 1].param) <= 1e-6)
                throw std::runtime_error("Loft guides meet a section at the same point");
        if (index == 0) stations.push_back({0, sections[index].profile.wire, true});
        std::vector<double> cuts;
        for (const Profile& guide : guides) for (const EdgeRecord& edge : guide.edges) {
            for (const gp_Pnt& vertex : {edge.first, edge.last}) {
                const double u = gp_Vec(from, vertex).Dot(gp_Vec(from, to)) / (length * length);
                if (u > 0.02 && u < 0.98) cuts.push_back(u);
            }
        }
        std::sort(cuts.begin(), cuts.end());
        double previous = -1;
        for (double u : cuts) {
            if (previous >= 0 && std::abs(u - previous) * length <= 0.2) continue;
            previous = u;
            const gp_Pnt station = from.Translated(gp_Vec(from, to) * u);
            std::vector<gp_Pnt> targets;
            for (const Profile& guide : guides) targets.push_back(crossPlane(guide, station, spine));
            std::vector<gp_Pnt> points;
            for (size_t mark = 0; mark < pierces.size(); ++mark) {
                const size_t next = (mark + 1) % pierces.size();
                const gp_Vec offA(pierces[mark].point, targets[static_cast<size_t>(pierces[mark].guide)]);
                const gp_Vec offB(pierces[next].point, targets[static_cast<size_t>(pierces[next].guide)]);
                const auto edges = walkForward(sections[index].profile, pierces[mark], pierces[next], next == 0);
                std::vector<gp_Pnt> samples;
                for (const TopoDS_Edge& edge : edges) {
                    BRepAdaptor_Curve curve(edge);
                    const double first = curve.FirstParameter(), last = curve.LastParameter();
                    for (int step = 0; step < 4; ++step) samples.push_back(curve.Value(first + (last - first) * step / 4.0));
                }
                for (size_t sample = 0; sample < samples.size(); ++sample) {
                    const double blend = samples.size() <= 1 ? 0 : static_cast<double>(sample) / static_cast<double>(samples.size());
                    const gp_Pnt moved = samples[sample].Translated(offA * (1.0 - blend) + offB * blend);
                    if (points.empty() || points.back().Distance(moved) > 1e-3) points.push_back(moved);
                }
            }
            if (!points.empty() && points.front().Distance(points.back()) <= 1e-3) points.pop_back();
            if (points.size() < 3) throw std::runtime_error("Loft guide section failed");
            using PointArray = NCollection_HArray1<gp_Pnt>;
            Handle(PointArray) poles = new PointArray(1, static_cast<int>(points.size()));
            for (int pole = 1; pole <= static_cast<int>(points.size()); ++pole) poles->SetValue(pole, points[static_cast<size_t>(pole - 1)]);
            GeomAPI_Interpolate interpolate(poles, true, 1e-4);
            interpolate.Perform();
            if (!interpolate.IsDone()) throw std::runtime_error("Loft guide section failed");
            BRepBuilderAPI_MakeEdge edge(interpolate.Curve());
            if (!edge.IsDone()) throw std::runtime_error("Loft guide section failed");
            BRepBuilderAPI_MakeWire wire(edge.Edge());
            if (!wire.IsDone()) throw std::runtime_error("Loft guide section failed");
            stations.push_back({traveled + u * length, wire.Wire(), false});
        }
        traveled += length;
        stations.push_back({traveled, sections[index + 1].profile.wire, true});
    }
    for (const Profile& guide : guides) {
        BRepExtrema_DistShapeShape endDist(guide.wire, sections.back().profile.wire);
        endDist.Perform();
        if (!endDist.IsDone() || endDist.NbSolution() < 1 || endDist.Value() > 1e-3)
            throw std::runtime_error("Loft guide does not meet a section");
    }
    std::sort(stations.begin(), stations.end(), [](const Station& a, const Station& b) { return a.t < b.t; });
    std::vector<Station> ordered;
    for (const Station& station : stations) {
        if (!ordered.empty() && std::abs(station.t - ordered.back().t) <= 0.2) {
            if (station.authored && !ordered.back().authored) ordered.back() = station;
            continue;
        }
        ordered.push_back(station);
    }
    if (ordered.size() < 2) throw std::runtime_error("Loft guide spine failed");
    BRepOffsetAPI_ThruSections maker(solid, false, tolerance(request));
    configureLoftMaker(maker,request,true);
    for (const Station& station : ordered) maker.AddWire(station.wire);
    maker.Build();
    if (!maker.IsDone()) throw std::runtime_error("OCCT guided loft failed");
    TopoDS_Shape shape = maker.Shape();
    if (solid && properties(shape) < 0) shape.Reverse();
    for (const Profile& guide : guides) for (const EdgeRecord& edge : guide.edges) {
        for (const gp_Pnt& vertex : {edge.first, edge.last}) {
            BRepBuilderAPI_MakeVertex probe(vertex);
            BRepExtrema_DistShapeShape distance(probe.Vertex(), shape);
            distance.Perform();
            if (!distance.IsDone() || distance.NbSolution() < 1 || distance.Value() > 0.05)
                throw std::runtime_error("Loft guide is not on the surface");
        }
    }
    return loftEvidence(guidedResult(shape, solid, sections.front().centroid, sections.back().centroid),request);
}

V loft(const V& request) {
    validateLoftControls(request);
    if (const V* guides = request.find("guides")) {
        if (guides->t != V::Arr) throw std::runtime_error("Loft guides must be curves");
        if (!guides->arr.empty()) return guidedLoft(request);
    }
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
    std::string sectionSummary;
    std::vector<std::pair<std::string, EdgeRecord>> firstSectionEdges, lastSectionEdges;
    TopoDS_Wire firstWire;
    auto namedEdges = [](const V& value, const Profile& profile) {
        std::vector<std::pair<std::string, EdgeRecord>> named;
        const V* sourceEntities = value.find("entities");
        std::set<std::string> names;
        for (size_t i = 0; i < profile.edges.size(); ++i) {
            const size_t source=profile.sourceIndices.empty()?i:profile.sourceIndices[i];
            const V* supplied = sourceEntities&&sourceEntities->t==V::Arr&&source<sourceEntities->arr.size()?sourceEntities->arr[source].find("name"):nullptr;
            std::string name = supplied && supplied->t == V::Str && !supplied->str.empty()
                ? supplied->str : "entity." + std::to_string(source);
            if (!names.insert(name).second) throw std::runtime_error("Duplicate loft section entity name");
            named.push_back({name, profile.edges[i]});
        }
        return named;
    };
    for (size_t profileIndex = 0; profileIndex < profiles.arr.size(); ++profileIndex) {
        const V& value = profiles.arr[profileIndex];
        if (pointProfile(value)) {
            if(const V* controls=request.find("controls"))if(const V* alignment=controls->find("section_alignment")){
                const V& item=alignment->arr[profileIndex];
                if((item.find("reverse")&&item.find("reverse")->b)||(item.find("seam_edge")&&number(*item.find("seam_edge"),"seam_edge")!=0)||(item.find("seam_angle")&&number(*item.find("seam_angle"),"seam_angle")!=0))throw std::runtime_error("Point loft sections do not support wire reversal or seam controls");
            }
            if (profileIndex != 0 && profileIndex + 1 != profiles.arr.size())
                throw std::runtime_error("a point section can only start or end a loft");
            const gp_Pnt tip = point(required(value, "point"), frameOf(value));
            BRepBuilderAPI_MakeVertex vertex(tip);
            if (!vertex.IsDone()) throw std::runtime_error("loft point section is invalid");
            maker.AddVertex(vertex.Vertex());
            continue;
        }
        Profile profile = alignedSection(request,profileIndex,solid);
        if (!sectionSummary.empty()) sectionSummary += "; ";
        sectionSummary += "section " + std::to_string(profileIndex) + ": " + std::to_string(profile.edges.size()) + " edges, " + (profile.closed ? "closed" : "open");
        if (!sawWire) {
            segments = profile.edges.size();
            closed = profile.closed;
            sawWire = true;
            firstWire = profile.wire;
            firstSectionEdges = namedEdges(value, profile);
        } else if (profile.closed != closed) {
            // OCCT cannot blend an open and a closed wire, even with compatibility repair.
            throw std::runtime_error("loft sections mix open and closed profiles: section " + std::to_string(profileIndex) + " is " + (profile.closed ? "closed" : "open") + " but the first wire section is " + (closed ? "closed" : "open") + "; close every section for a solid loft or keep all open for a surface");
        } else if (!compatible && profile.edges.size() != segments) {
            throw std::runtime_error("loft profiles have incompatible edge topology: section " + std::to_string(profileIndex) + " has " + std::to_string(profile.edges.size()) + " edges but the first wire section has " + std::to_string(segments) + "; give every section the same edge count or set p.compatible:1");
        }
        lastSectionEdges = namedEdges(value, profile);
        maker.AddWire(profile.wire);
    }
    if (!sawWire) throw std::runtime_error("loft needs at least one wire section");
    maker.Build();
    if (!maker.IsDone()) {
        const auto status = maker.GetStatus();
        const char* reason = status == BRepFill_ThruSectionErrorStatus_NotSameTopology ? "sections do not share one topology"
            : status == BRepFill_ThruSectionErrorStatus_ProfilesInconsistent ? "profiles are inconsistent (check winding, seam alignment and self-crossing sections)"
            : status == BRepFill_ThruSectionErrorStatus_WrongUsage ? "point sections may only start or end the loft"
            : status == BRepFill_ThruSectionErrorStatus_Null3DCurve ? "a section edge has no 3D curve"
            : "the surface through the sections could not be built (check that sections do not coincide or cross)";
        throw std::runtime_error(std::string("OCCT loft failed: ") + reason + "; " + sectionSummary + (solid && !closed ? "; solid:1 needs closed sections" : ""));
    }
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
    const size_t authored = rows.arr.size();
    std::map<std::string, std::vector<size_t>> groups;
    std::set<std::string> names;
    auto stemOf = [](const std::string& name) {
        const auto dot = name.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == name.size()) return std::string();
        for (size_t i = dot + 1; i < name.size(); ++i)
            if (name[i] < '0' || name[i] > '9') return std::string();
        return name.substr(0, dot);
    };
    for (size_t index = 0; index < authored; ++index) {
        const std::string name = required(rows.arr[index], "name").str;
        names.insert(name);
        const std::string stem = stemOf(name);
        if (!stem.empty()) groups[stem].push_back(index);
    }
    for (const auto& [stem, members] : groups) {
        if (members.size() < 2 || names.count(stem)) continue;
        std::string kind, surviving;
        std::set<int> bound;
        bool sameKind = true;
        V mergedFrom = V::makeArr();
        for (size_t index : members) {
            const V& row = rows.arr[index];
            const std::string rowKind = required(row, "kind").str;
            if (kind.empty()) kind = rowKind;
            else if (kind != rowKind) sameKind = false;
            mergedFrom.arr.push_back(required(row, "name"));
            if (required(row, "status").str != "bound") continue;
            const V& indices = required(row, "indices");
            if (indices.t != V::Arr || indices.arr.size() != 1 || indices.arr[0].t != V::Num) continue;
            bound.insert(static_cast<int>(indices.arr[0].num));
            if (surviving.empty()) surviving = required(row, "name").str;
        }
        if (!sameKind || bound.empty() || surviving.empty()) continue;
        V alias = V::makeObj(), indices = V::makeArr();
        alias.set("name", V::makeStr(stem));
        alias.set("kind", V::makeStr(kind));
        alias.set("status", V::makeStr(bound.size() == 1 ? "merged" : "split"));
        if (bound.size() == 1) {
            indices.arr.push_back(V::makeNum(*bound.begin()));
            alias.set("surviving", V::makeStr(surviving));
        } else {
            // Older histories could expose this stem as a merged alias after
            // losing wire-edge identities. Keep its name without binding any
            // one of the now correctly distinguished exact entities.
            alias.set("reason", V::makeStr("Authored section members resolve to distinct exact entities; select an individual member"));
            alias.set("split_into", mergedFrom);
        }
        alias.set("indices", std::move(indices));
        alias.set("merged_from", std::move(mergedFrom));
        rows.arr.push_back(std::move(alias));
    }
    topology.set("references", rows);
    result.set("topology", topology);
    return loftEvidence(result,request);
}

V surfaceEditResult(const TopoDS_Shape& face,const char* operation);
#include "advanced_surfaces_031b.inl"

V surfaceFill(const V& request) {
    if (request.find("constraints")) return constrainedSurfaceFill031B(request);
    if (request.find("validation") || request.find("mode"))
        throw std::runtime_error("Constrained fill/blend options require explicit support constraints");
    Profile profile = profileOf(required(request, "profile"), true);
    const double tol = tolerance(request);
    BRepOffsetAPI_MakeFilling maker(3, 15, 2, false, tol, tol);
    for (const auto& edge : profile.edges) maker.Add(edge.edge, GeomAbs_C0, true);
    maker.Build();
    if (!maker.IsDone()) throw std::runtime_error("OCCT surface filling failed");
    if (!std::isfinite(maker.G0Error()) || maker.G0Error() > tol)
        throw std::runtime_error("surface filling exceeds boundary tolerance");
    V result=shapeResult(maker.Shape(), false),report=*result.find("report");
    report.set("boundary_continuity_requested",V::makeStr("G0"));
    report.set("boundary_max_error_mm",V::makeNum(maker.G0Error()));
    report.set("boundary_tolerance_mm",V::makeNum(tol));
    report.set("neighbor_tangency_verified",V::makeBool(false));
    result.set("report",report);return result;
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

// 046B: a sketch profile with several loops or a region selection becomes one
// exact planar face with holes. Surface (non-solid) results keep one loop.
TopoDS_Shape regionBasis046B(const V& request, const V& profileValue, V& receipt) {
    if (!profileValue.find("regions")) return {};
    if (!flag(request, "solid", false)) throw std::runtime_error("Profile regions (several loops or native.regions) need p.solid=1; a surface result keeps one loop");
    return profileRegionFaces046B(profileValue, receipt).front();
}

V extrudeSurface(const V& request) {
    const V& profileValue = required(request, "profile");
    const auto frame = frameOf(profileValue);
    V regionReceipt;
    const TopoDS_Shape regionBasis = regionBasis046B(request, profileValue, regionReceipt);
    const Profile profile = regionBasis.IsNull() ? profileOf(profileValue, false) : Profile{};
    const double distance = number(required(request, "distance"), "distance");
    if (std::abs(distance) <= kTolerance) throw std::runtime_error("extrude distance must exceed tolerance");
    const bool solid = flag(request, "solid", false);
    const gp_Vec direction(frame[8] * distance, frame[9] * distance, frame[10] * distance);
    BRepPrimAPI_MakePrism prism(regionBasis.IsNull() ? basisOf(profile, solid) : regionBasis, direction, true, true);
    prism.Build();
    if (!prism.IsDone()) throw std::runtime_error("OCCT surface extrusion failed");
    TopoDS_Shape shape = prism.Shape();
    if (solid && properties(shape) < 0) shape.Reverse();
    if (!regionBasis.IsNull()) return tag(shapeResult(shape, solid), "extrude_surface", "profile_regions", regionReceipt);
    return tag(shapeResult(shape, solid), "extrude_surface");
}

V revolveSurface(const V& request) {
    const V& profileValue = required(request, "profile");
    const auto frame = frameOf(profileValue);
    V regionReceipt;
    const TopoDS_Shape regionBasis = regionBasis046B(request, profileValue, regionReceipt);
    const Profile profile = regionBasis.IsNull() ? profileOf(profileValue, false) : Profile{};
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
    const TopoDS_Shape basis = regionBasis.IsNull() ? basisOf(profile, solid) : regionBasis;
    const bool full = std::abs(degrees - 360.0) <= 1.0e-9;
    // Explain the common failure: profile vertices on both sides of the axis.
    auto revolutionFailure = [&]() {
        const gp_XYZ origin = revolution.Location().XYZ(), direction = revolution.Direction().XYZ();
        gp_XYZ reference(0, 0, 0);bool positive = false, negative = false;
        for (const TopoDS_Shape& vertex : shapes(basis, TopAbs_VERTEX)) {
            gp_XYZ radial = BRep_Tool::Pnt(TopoDS::Vertex(vertex)).XYZ() - origin;radial -= direction * radial.Dot(direction);
            if (radial.Modulus() <= 1.0e-6) continue;
            if (reference.Modulus() == 0) reference = radial;
            (radial.Dot(reference) > 0 ? positive : negative) = true;
        }
        return std::runtime_error(positive && negative
            ? "OCCT surface revolution failed: the profile crosses the revolution axis; keep it on one side (it may touch the axis)"
            : "OCCT surface revolution failed: check that the profile is planar, contains the axis direction and is not degenerate");
    };
    TopoDS_Shape shape;
    if (full) {
        BRepPrimAPI_MakeRevol revol(basis, revolution, true);
        revol.Build();
        if (!revol.IsDone()) throw revolutionFailure();
        shape = revol.Shape();
    } else {
        BRepPrimAPI_MakeRevol revol(basis, revolution, degrees * std::acos(-1.0) / 180.0, true);
        revol.Build();
        if (!revol.IsDone()) throw revolutionFailure();
        shape = revol.Shape();
    }
    if (solid && properties(shape) < 0) shape.Reverse();
    if (!regionBasis.IsNull()) return tag(shapeResult(shape, solid), "revolve_surface", "profile_regions", regionReceipt);
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

#include "advanced_sweep_surface_031b.inl"

// 046B hollow sweep: every loop of one region is swept with one shared
// placement and trihedron (no per-loop correction or contact), then the hole
// sweeps are cut from the outer sweep. Volume must equal outer minus holes.
V hollowSweep046B(const V& request) {
    if (request.find("laws") || request.find("sections")) throw std::runtime_error("A sweep section with profile regions keeps one section; laws and varying sections need one loop per station");
    if (!flag(request, "solid", false)) throw std::runtime_error("A sweep section with profile regions needs p.solid=1");
    if (flag(request, "contact", false)) throw std::runtime_error("A hollow sweep section cannot use contact: moving each loop onto the path separately would displace its holes; set contact:0");
    const bool correction = flag(request, "correction", true);
    Profile path = profileOf(required(request, "path"), false);
    const TopoDS_Face preparedSupport = prepareSweepPath031B(request, path);
    const V& sectionValue = required(request, "section");
    V regionReceipt;
    const TopoDS_Face face = TopoDS::Face(profileRegionFaces046B(sectionValue, regionReceipt).front());
    const TopoDS_Wire outer = BRepTools::OuterWire(face);
    std::vector<TopoDS_Wire> loops{outer};
    for (TopoDS_Iterator it(face); it.More(); it.Next()) if (it.Value().ShapeType() == TopAbs_WIRE && !it.Value().IsSame(outer)) loops.push_back(TopoDS::Wire(it.Value()));
    const auto frame = frameOf(sectionValue);
    const gp_Dir normal(frame[8], frame[9], frame[10]);
    gp_Trsf placement;
    std::string correctionMode = correction ? "already_normal_to_initial_tangent" : "none";
    if (correction) {
        // The existing straight-section rule, applied once to the whole section.
        const gp_Dir tangent = initialTangent(path);
        if (!normal.IsParallel(tangent, 1.0e-9)) {
            GProp_GProps props;
            BRepGProp::SurfaceProperties(face, props);
            const gp_Dir axis = normal.Crossed(tangent);
            placement.SetRotation(gp_Ax1(props.CentreOfMass(), axis), normal.AngleWithRef(tangent, axis));
            correctionMode = "shared_rigid_rotation_to_initial_tangent";
        }
    }
    std::vector<TopoDS_Shape> solids;
    std::vector<double> volumes;
    std::string orientation, orientationSolver = "native_pipe_trihedron";
    for (size_t k = 0; k < loops.size(); ++k) {
        const std::string role = k ? "hole loop " + std::to_string(k) : std::string("outer loop");
        const TopoDS_Wire wire = correctionMode == "shared_rigid_rotation_to_initial_tangent" ? TopoDS::Wire(transformed(loops[k], placement)) : loops[k];
        BRepOffsetAPI_MakePipeShell pipe(path.wire);
        pipe.SetMode(false);
        orientation = setSweepOrientation031B(request, path, pipe, preparedSupport, orientationSolver);
        pipe.Add(wire, false, false);
        pipe.Build();
        if (!pipe.IsDone()) throw std::runtime_error("OCCT sweep failed for the hollow section " + role);
        if (!pipe.MakeSolid()) throw std::runtime_error("OCCT sweep did not make a solid for the hollow section " + role);
        TopoDS_Shape shape = pipe.Shape();
        double volume = properties(shape);
        if (volume < 0) { shape.Reverse(); volume = -volume; }
        if (!(volume > 0)) throw std::runtime_error("Hollow section " + role + " swept to zero volume");
        solids.push_back(shape);
        volumes.push_back(volume);
    }
    TopoDS_Shape shape = solids.front();
    double expected = volumes.front();
    if (solids.size() > 1) {
        for (size_t k = 1; k < volumes.size(); ++k) expected -= volumes[k];
        shape = cut(solids.front(), compound(std::vector<TopoDS_Shape>(solids.begin() + 1, solids.end())));
    }
    const double measured = properties(shape), limit = std::max(1.0e-5, volumes.front() * 1.0e-7), error = measured - expected;
    if (std::abs(error) > limit)
        throw std::runtime_error("Hollow sweep does not conserve volume: outer " + std::to_string(volumes.front()) + " mm3 minus holes gives " + std::to_string(expected) + " mm3 but the result has " + std::to_string(measured) + " mm3 (limit " + std::to_string(limit) + "); a hole sweep leaves the outer sweep or the loops overlap along the path");
    V result = tag(shapeResult(shape, true), "sweep");
    V report = required(result, "report");
    report.set("orientation_mode", V::makeStr(orientation));
    report.set("orientation_solver", V::makeStr(orientationSolver));
    V hollow = V::makeObj(), holes = V::makeArr();
    for (size_t k = 1; k < volumes.size(); ++k) holes.arr.push_back(V::makeNum(volumes[k]));
    hollow.set("loops", V::makeNum(static_cast<double>(loops.size())));
    hollow.set("outer_volume_mm3", V::makeNum(volumes.front()));
    hollow.set("hole_volumes_mm3", holes);
    hollow.set("expected_volume_mm3", V::makeNum(expected));
    hollow.set("result_volume_mm3", V::makeNum(measured));
    hollow.set("conservation_error_mm3", V::makeNum(error));
    hollow.set("conservation_limit_mm3", V::makeNum(limit));
    hollow.set("correction", V::makeStr(correctionMode));
    hollow.set("method", V::makeStr("each loop swept with one shared placement and trihedron; outer minus holes by exact Boolean"));
    report.set("hollow_section", hollow);
    report.set("profile_regions", regionReceipt);
    result.set("report", report);
    return result;
}

V sweepSurface(const V& request) {
    if (const V* section = request.find("section"); section && section->t == V::Obj && section->find("regions")) return hollowSweep046B(request);
    if (request.find("laws")) return lawSweep031B(request);
    const bool solid = flag(request, "solid", false);
    Profile path = profileOf(required(request, "path"), false);
    const TopoDS_Face preparedSupport=prepareSweepPath031B(request, path);
    BRepOffsetAPI_MakePipeShell pipe(path.wire);
    pipe.SetMode(false); // corrected Frenet; straight paths stay parallel to the section plane
    std::string orientationSolver="native_pipe_trihedron";
    const std::string orientation = setSweepOrientation031B(request, path, pipe, preparedSupport, orientationSolver);
    const bool contact = flag(request, "contact", false);
    const bool correction = flag(request, "correction", true);
    V frames=V::makeArr();
    if(const V* sections=request.find("sections")){
        if(sections->t!=V::Arr||sections->arr.size()<2||sections->arr.size()>32)throw std::runtime_error("Varying sweep requires 2..32 stations");std::vector<double> lengths;double total=0;for(const auto& edge:path.edges){BRepAdaptor_Curve curve(edge.edge);double length=GCPnts_AbscissaPoint::Length(curve);if(length<=kTolerance)throw std::runtime_error("Sweep path has a degenerate edge");lengths.push_back(length);total+=length;}
        double previous=-1;gp_Vec previousTangent,transportedX;bool haveFrame=false;for(const V& station:sections->arr){double fraction=number(required(station,"fraction"),"fraction");if(fraction<0||fraction>1||fraction<=previous)throw std::runtime_error("Sweep station fractions must increase in 0..1");previous=fraction;double distance=fraction*total;size_t edgeIndex=0;while(edgeIndex+1<lengths.size()&&distance>lengths[edgeIndex]){distance-=lengths[edgeIndex];++edgeIndex;}const auto& source=path.edges[edgeIndex].edge;BRepAdaptor_Curve curve(source);bool reverse=source.Orientation()==TopAbs_REVERSED;double origin=reverse?curve.LastParameter():curve.FirstParameter();double parameter=origin;if(distance>kTolerance&&distance<lengths[edgeIndex]-kTolerance){GCPnts_AbscissaPoint solve(curve,reverse?-distance:distance,origin,kTolerance);if(!solve.IsDone())throw std::runtime_error("Exact sweep station cannot be located on its path");parameter=solve.Parameter();}else if(distance>=lengths[edgeIndex]-kTolerance)parameter=reverse?curve.FirstParameter():curve.LastParameter();gp_Pnt point;gp_Vec tangent;curve.D1(parameter,point,tangent);if(reverse)tangent.Reverse();if(tangent.Magnitude()<=1e-12)throw std::runtime_error("Sweep station has zero path tangent");tangent.Normalize();
            gp_Vec guide(1,0,0);if(const V* authored=station.find("x_axis")){if(authored->t!=V::Arr||authored->arr.size()!=3)throw std::runtime_error("Sweep x_axis requires XYZ");guide=gp_Vec(number(authored->arr[0],"axis"),number(authored->arr[1],"axis"),number(authored->arr[2],"axis"));}else if(haveFrame){guide=transportedX;double cosine=previousTangent.Dot(tangent);gp_Vec rotation=previousTangent.Crossed(tangent);double sine=rotation.Magnitude();if(cosine<-1+1e-10)throw std::runtime_error("Opposite sweep station tangents need an explicit X guide");if(sine>1e-12){rotation/=sine;guide=transportedX*cosine+rotation.Crossed(transportedX)*sine+rotation*(rotation.Dot(transportedX)*(1-cosine));}}else if(std::abs(tangent.X())>.9)guide=gp_Vec(0,1,0);gp_Vec x=guide-tangent*guide.Dot(tangent);if(x.Magnitude()<=1e-10)throw std::runtime_error("Sweep station X guide is parallel to its tangent");x.Normalize();transportedX=x;previousTangent=tangent;haveFrame=true;gp_Vec y=tangent.Crossed(x);double roll=station.find("roll")?number(*station.find("roll"),"roll")*(3.14159265358979323846/180.):0;gp_Vec rolled=x*std::cos(roll)+y*std::sin(roll);x=rolled;y=tangent.Crossed(x);
            const Profile section=profileOf(required(station,"section"),solid);gp_Trsf sourceFrame;const auto& m=section.frame;sourceFrame.SetValues(m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14]);gp_Trsf targetFrame;targetFrame.SetValues(x.X(),y.X(),tangent.X(),point.X(),x.Y(),y.Y(),tangent.Y(),point.Y(),x.Z(),y.Z(),tangent.Z(),point.Z());gp_Trsf placement=targetFrame*sourceFrame.Inverted();TopoDS_Wire positioned=TopoDS::Wire(transformed(section.wire,placement));TopoDS_Vertex location=BRepBuilderAPI_MakeVertex(point).Vertex();pipe.Add(positioned,location,false,false);
            V frame=V::makeObj(),center=V::makeArr(),normal=V::makeArr();center.arr={V::makeNum(point.X()),V::makeNum(point.Y()),V::makeNum(point.Z())};normal.arr={V::makeNum(tangent.X()),V::makeNum(tangent.Y()),V::makeNum(tangent.Z())};frame.set("fraction",V::makeNum(fraction));frame.set("origin",center);frame.set("normal",normal);frame.set("path_edge",V::makeNum(edgeIndex));frames.arr.push_back(frame);
        }
        if(sections->arr.front().find("fraction")->num!=0||sections->arr.back().find("fraction")->num!=1)throw std::runtime_error("Varying sweep must author its start/end sections");
    }else{
    const Profile section = profileOf(required(request, "section"), solid);
    // MakePipeShell accepts a wire or a vertex. A solid is the swept shell, capped by MakeSolid.
    // A straight section has no curve plane, so OCCT refuses WithCorrection; the sketch normal is that plane.
    TopoDS_Wire profile = section.wire;
    bool occtCorrection = false;
    if (correction && sectionDefinesPlane(section)) occtCorrection = true;
    else if (correction) profile = alignSectionPlane(section, initialTangent(path));
    pipe.Add(profile, contact, occtCorrection);
    }
    pipe.Build();
    if (!pipe.IsDone()) throw std::runtime_error("OCCT sweep failed");
    if (solid && !pipe.MakeSolid()) throw std::runtime_error("OCCT sweep did not make a solid");
    TopoDS_Shape shape = pipe.Shape();
    if (solid && properties(shape) < 0) shape.Reverse();
    V result=tag(shapeResult(shape, solid), "sweep");V report=required(result,"report");report.set("orientation_mode",V::makeStr(orientation));report.set("orientation_solver",V::makeStr(orientationSolver));if(!frames.arr.empty()){report.set("varying_sections",V::makeNum(frames.arr.size()));report.set("dynamic_tangent_frames",frames);report.set("station_authority",V::makeStr("exact_path_arclength_and_derivative"));report.set("display_mesh_authoritative",V::makeBool(false));}result.set("report",report);return result;
}

V surfaceEditResult(const TopoDS_Shape& face,const char* operation){
    V result=tag(shapeResult(face,false),operation),topology=V::makeObj(),references=V::makeArr(),reference=V::makeObj(),indices=V::makeArr();
    topology.set("brep_sha256",V::makeStr(sha256(base64Decode(required(result,"brep_base64").str))));
    reference.set("name",V::makeStr("surface"));reference.set("kind",V::makeStr("face"));reference.set("status",V::makeStr("bound"));indices.arr.push_back(V::makeNum(0));reference.set("indices",indices);references.arr.push_back(reference);topology.set("references",references);result.set("topology",topology);
    return result;
}
V extractSurfaceFace(const V& request){
    const auto& input=required(request,"input_brep");if(input.t!=V::Str)throw std::runtime_error("Surface extraction needs exact B-rep input");
    // Reuse the digest-bound semantic resolver; never guess an indexed face.
    const auto index=(size_t)semanticFaceIndexNative(request);
    const auto faces=shapes(readBrep(input.str),TopAbs_FACE);
    if(index>=faces.size())throw std::runtime_error("Surface extraction face is out of range");
    auto result=surfaceEditResult(faces[index],"surface_extract_face");
    V report=required(result,"report");report.set("source_face",required(request,"reference"));report.set("source_brep_sha256",V::makeStr(sha256(base64Decode(input.str))));result.set("report",report);return result;
}
V trimSurfacePlane(const V& request){
    const auto& input=required(request,"input_brep");if(input.t!=V::Str)throw std::runtime_error("Surface trim needs exact B-rep input");
    const auto source=readBrep(input.str);const auto sourceFaces=shapes(source,TopAbs_FACE);
    if(!shapes(source,TopAbs_SOLID).empty()||sourceFaces.size()!=1)throw std::runtime_error("Surface plane trim requires exactly one surface face; extract a face from a solid first");
    auto vector=[](const V& value){if(value.t!=V::Arr||value.arr.size()!=3)throw std::runtime_error("Plane vector needs three numbers");return gp_Vec(number(value.arr[0],"plane coordinate"),number(value.arr[1],"plane coordinate"),number(value.arr[2],"plane coordinate"));};
    const auto originVector=vector(required(request,"origin")),normal=vector(required(request,"normal"));if(normal.Magnitude()<1e-12)throw std::runtime_error("Surface trim normal is zero");
    const gp_Pnt origin(originVector.X(),originVector.Y(),originVector.Z());const gp_Dir direction(normal);
    const auto& keep=required(request,"keep");if(keep.t!=V::Str||(keep.str!="positive"&&keep.str!="negative"))throw std::runtime_error("Surface trim keep must be positive or negative");
    BRepBuilderAPI_MakeFace plane(gp_Pln(origin,direction));if(!plane.IsDone())throw std::runtime_error("Surface trim plane construction failed");
    // Face/half-space common can silently return empty compounds for a periodic
    // face. Split once instead, retaining and validating every resulting face.
    NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(sourceFaces.front());tools.Append(plane.Face());
    BRepAlgoAPI_Splitter split;split.SetArguments(arguments);split.SetTools(tools);split.Build();
    if(!split.IsDone()||split.HasErrors())throw std::runtime_error("Surface plane trim failed");
    const auto pieces=shapes(split.Shape(),TopAbs_FACE);
    const char* divisionError="Plane must divide the surface into one connected face on each side; tangent/outside, disconnected or ambiguous result rejected";
    if(pieces.size()!=2)throw std::runtime_error(divisionError);
    gp_Trsf toPlane;toPlane.SetTransformation(gp_Ax3(origin,direction));
    TopoDS_Shape positive,negative;double positiveArea=0,negativeArea=0;
    for(const auto& piece:pieces){
        const double area=properties(piece,'a');if(area<=1e-8)throw std::runtime_error(divisionError);
        shapeReport(piece,false);
        // Native support/curve bounds in the cutting-plane frame establish the
        // signed-distance range. No triangulation or sampled centroid chooses a side.
        const auto box=bounds(transformed(piece,toPlane));
        if(box[2]>=-kTolerance&&box[5]>kTolerance){if(!positive.IsNull())throw std::runtime_error(divisionError);positive=piece;positiveArea=area;}
        else if(box[5]<=kTolerance&&box[2]<-kTolerance){if(!negative.IsNull())throw std::runtime_error(divisionError);negative=piece;negativeArea=area;}
        else throw std::runtime_error(divisionError);
    }
    if(positive.IsNull()||negative.IsNull())throw std::runtime_error(divisionError);
    const double originalArea=properties(sourceFaces.front(),'a'),retainedArea=keep.str=="positive"?positiveArea:negativeArea;
    if(std::abs(positiveArea+negativeArea-originalArea)>std::max(1e-6,originalArea*1e-8)||originalArea-retainedArea<=1e-8)throw std::runtime_error("Surface trim failed area conservation");
    auto result=surfaceEditResult(keep.str=="positive"?positive:negative,"surface_trim_plane");V report=required(result,"report");report.set("original_area_mm2",V::makeNum(originalArea));report.set("removed_area_mm2",V::makeNum(originalArea-retainedArea));report.set("positive_area_mm2",V::makeNum(positiveArea));report.set("negative_area_mm2",V::makeNum(negativeArea));report.set("area_conservation_checked",V::makeBool(true));report.set("split_method",V::makeStr("exact_face_plane_splitter"));report.set("side_classification",V::makeStr("exact_brep_bounds_in_plane_frame"));report.set("classification_tolerance_mm",V::makeNum(kTolerance));result.set("report",report);return result;
}
V trimSurfaceWire(const V& request){
    const auto source=readBrep(required(request,"input_brep").str),curve=readBrep(required(request,"wire_brep").str);const auto faces=shapes(source,TopAbs_FACE),wires=shapes(curve,TopAbs_WIRE);
    if(!shapes(source,TopAbs_SOLID).empty()||faces.size()!=1)throw std::runtime_error("Wire trim requires one open surface face; extract a face first");
    if(wires.size()!=1||!BRep_Tool::IsClosed(wires.front()))throw std::runtime_error("Wire trim requires one explicitly closed exact wire");
    const auto& xyz=required(request,"keep_point");if(xyz.t!=V::Arr||xyz.arr.size()!=3)throw std::runtime_error("Keep point needs three coordinates");const gp_Pnt point(number(xyz.arr[0],"keep X"),number(xyz.arr[1],"keep Y"),number(xyz.arr[2],"keep Z"));
    NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(faces.front());tools.Append(wires.front());BRepAlgoAPI_Splitter split;split.SetArguments(arguments);split.SetTools(tools);split.Build();if(!split.IsDone()||split.HasErrors())throw std::runtime_error("Surface wire trim failed");const auto pieces=shapes(split.Shape(),TopAbs_FACE);if(pieces.size()<2||pieces.size()>64)throw std::runtime_error("Trim wire must divide the surface; project the closed wire onto it first");
    double total=0;TopoDS_Shape selected;unsigned matches=0;for(const auto& piece:pieces){total+=properties(piece,'a');BRepClass_FaceClassifier classifier(TopoDS::Face(piece),point,1e-6);if(classifier.State()==TopAbs_IN){BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(point).Vertex(),piece);distance.Perform();if(distance.IsDone()&&distance.Value()<=1e-6){selected=piece;++matches;}}}
    if(matches!=1)throw std::runtime_error("Choose one keep point on the retained region, away from its boundary");const double original=properties(source,'a'),retained=properties(selected,'a');if(std::abs(total-original)>std::max(1e-6,original*1e-8)||retained<=1e-8||original-retained<=1e-8)throw std::runtime_error("Surface trim failed area conservation");return surfaceEditResult(selected,"surface_trim_wire");
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
    sewing.SetMaxTolerance(kTolerance);
    sewing.SetLocalTolerancesMode(false);
    std::set<std::string> uniqueInputs;
    for (const V& encoded : inputs.arr) {
        if (encoded.t != V::Str || encoded.str.empty()) throw std::runtime_error("sew input must be base64");
        if(!uniqueInputs.insert(encoded.str).second)throw std::runtime_error("Sew received a duplicate B-rep input");
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
    if(sewing.NbDeletedFaces()!=0||sewing.NbDegeneratedShapes()!=0)
        throw std::runtime_error("Sew refuses deleted faces or degenerated input shapes");
    if(sewing.NbMultipleEdges()!=0)throw std::runtime_error("Sew produced a non-manifold multiple edge");
    const auto faces=shapes(sewn,TopAbs_FACE);
    std::vector<size_t> parents(faces.size());for(size_t i=0;i<parents.size();++i)parents[i]=i;
    auto leader=[&](size_t i){while(parents[i]!=i)i=parents[i];return i;};
    NCollection_IndexedDataMap<TopoDS_Shape,NCollection_List<TopoDS_Shape>,TopTools_ShapeMapHasher> owners;
    TopExp::MapShapesAndAncestors(sewn,TopAbs_EDGE,TopAbs_FACE,owners);
    for(int i=1;i<=owners.Extent();++i){size_t first=faces.size();for(NCollection_List<TopoDS_Shape>::Iterator owner(owners.FindFromIndex(i));owner.More();owner.Next())for(size_t face=0;face<faces.size();++face)if(faces[face].IsSame(owner.Value())){if(first==faces.size())first=face;else parents[leader(face)]=leader(first);break;}}
    std::set<size_t> components;for(size_t i=0;i<faces.size();++i)components.insert(leader(i));
    if(flag(request,"require_connected")&&components.size()!=1)
        throw std::runtime_error("Sew requires one connected surface; observed "+std::to_string(components.size())+" components");
    V boundaries=V::makeArr();double boundaryLength=0;
    for(int i=1;i<=freeEdges;++i){const TopoDS_Edge& edge=sewing.FreeEdge(i);BRepAdaptor_Curve curve(edge);double length=GCPnts_AbscissaPoint::Length(curve,1e-8);if(!std::isfinite(length)||length<=0)throw std::runtime_error("Sew free-boundary length failed");V row=V::makeObj();row.set("boundary",V::makeNum(i-1));row.set("length_mm",V::makeNum(length));row.set("start_mm",vec(curve.Value(curve.FirstParameter()).XYZ()));row.set("end_mm",vec(curve.Value(curve.LastParameter()).XYZ()));boundaries.arr.push_back(row);boundaryLength+=length;}
    if (solid) {
        if (freeEdges != 0) throw std::runtime_error("sew solid requires no free edges");
        const auto shells = shapes(sewn, TopAbs_SHELL);
        if (shells.size() != 1) throw std::runtime_error("sew solid requires one closed shell");
        if(components.size()!=1||!BRep_Tool::IsClosed(shells.front()))throw std::runtime_error("Sew solid requires one connected closed shell");
        BRepBuilderAPI_MakeSolid maker(TopoDS::Shell(shells.front()));
        maker.Build();
        if (!maker.IsDone()) throw std::runtime_error("OCCT sew did not make a solid");
        sewn = maker.Solid();
        if (properties(sewn) < 0) sewn.Reverse();
    }
    V result=tag(shapeResult(sewn,solid),"sew","free_edges",V::makeNum(freeEdges)),report=*result.find("report");
    report.set("sewing_tolerance_mm",V::makeNum(kTolerance));report.set("gap_healing",V::makeBool(false));
    report.set("component_count",V::makeNum(components.size()));report.set("shells",V::makeNum(shapes(sewn,TopAbs_SHELL).size()));
    report.set("contiguous_edges",V::makeNum(sewing.NbContigousEdges()));report.set("multiple_edges",V::makeNum(sewing.NbMultipleEdges()));
    report.set("free_boundary_length_mm",V::makeNum(boundaryLength));report.set("free_boundaries_mm",boundaries);
    report.set("closed",V::makeBool(freeEdges==0));report.set("connected",V::makeBool(components.size()==1));
    // Evaluate the native surface supports at their shared-edge pcurves.
    // Tangent planes are unoriented: reversed input faces do not turn a smooth
    // join into an apparent 180-degree crease. Samples do not prove global G1.
    V joins=V::makeArr();
    if(owners.Extent()>4096)throw std::runtime_error("Sew continuity inspection exceeds 4096 native edges");
    for(int i=1;i<=owners.Extent();++i){
        std::vector<TopoDS_Face> adjacent;
        for(NCollection_List<TopoDS_Shape>::Iterator owner(owners.FindFromIndex(i));owner.More();owner.Next())
            if(std::none_of(adjacent.begin(),adjacent.end(),[&](const TopoDS_Face& f){return f.IsSame(owner.Value());}))adjacent.push_back(TopoDS::Face(owner.Value()));
        if(adjacent.size()!=2)continue;
        const auto edge=TopoDS::Edge(owners.FindKey(i));V row=V::makeObj();row.set("edge_index",V::makeNum(i-1));
        double firstA,lastA,firstB,lastB;const auto a=BRep_Tool::CurveOnSurface(edge,adjacent[0],firstA,lastA),b=BRep_Tool::CurveOnSurface(edge,adjacent[1],firstB,lastB);
        double gap=0,angle=0;int checked=0;std::string reason;
        try{
            if(!BRep_Tool::SameParameter(edge))throw std::runtime_error("Shared edge does not have a verified common pcurve parameterization");
            if(a.IsNull()||b.IsNull())throw std::runtime_error("Missing shared-edge pcurve");
            const double first=std::max(firstA,firstB),last=std::min(lastA,lastB);
            if(!std::isfinite(first)||!std::isfinite(last)||last<=first)throw std::runtime_error("No finite shared pcurve parameter interval");
            BRepAdaptor_Surface sa(adjacent[0]),sb(adjacent[1]);
            for(int sample=1;sample<=17;++sample){
                const double t=first+(last-first)*sample/18.;const auto ua=a->Value(t),ub=b->Value(t);
                gp_Pnt pa,pb;gp_Vec au,av,bu,bv;sa.D1(ua.X(),ua.Y(),pa,au,av);sb.D1(ub.X(),ub.Y(),pb,bu,bv);
                gp_Vec na=au.Crossed(av),nb=bu.Crossed(bv);
                if(na.Magnitude()<=1e-14||nb.Magnitude()<=1e-14)throw std::runtime_error("Singular surface normal at a continuity sample");
                const double observedGap=pa.Distance(pb),observedAngle=std::acos(std::clamp(std::abs(na.Normalized().Dot(nb.Normalized())),0.,1.))*180/std::acos(-1.);
                if(!std::isfinite(observedGap)||!std::isfinite(observedAngle))throw std::runtime_error("Nonfinite continuity measurement");
                gap=std::max(gap,observedGap);angle=std::max(angle,observedAngle);++checked;
            }
        }catch(const Standard_Failure& failure){reason=failure.GetMessageString()?failure.GetMessageString():"OCCT continuity measurement failed";}
        catch(const std::exception& failure){reason=failure.what();}
        row.set("samples_checked",V::makeNum(checked));row.set("status",V::makeStr(reason.empty()?"measured":"unresolved"));
        row.set("observed_max_gap_mm",checked?V::makeNum(gap):V{});row.set("observed_max_tangent_plane_angle_deg",checked?V::makeNum(angle):V{});
        if(!reason.empty())row.set("reason",V::makeStr(reason));joins.arr.push_back(row);
    }
    report.set("shared_edge_continuity",joins);
    report.set("continuity_scope",V::makeStr("17 interior native pcurve checkpoints per shared edge; unoriented tangent planes; endpoints, between-sample extrema and G2 unverified"));
    report.set("global_g1_verified",V::makeBool(false));
    result.set("report",report);return result;
}

V collisionReport(const V& request){
    tolerance(request);
    for(const auto& field:request.obj)if(field.first!="op"&&field.first!="a_brep"&&field.first!="b_brep"&&field.first!="a_transform"&&field.first!="b_transform"&&field.first!="tolerance")throw std::runtime_error("Unknown collision field: "+field.first);
    auto input=[&](const char* brepKey,const char* transformKey){const V& encoded=required(request,brepKey);if(encoded.t!=V::Str||encoded.str.empty())throw std::runtime_error("Collision B-rep must be nonempty base64");TopoDS_Shape shape=readBrep(encoded.str);shapeReport(shape,true);const auto solids=shapes(shape,TopAbs_SOLID);if(shapes(shape,TopAbs_FACE).size()!=shapes(solids.front(),TopAbs_FACE).size())throw std::runtime_error("Collision refuses stray faces outside the input solid");V profile=V::makeObj();if(const V* frame=request.find(transformKey))profile.set("frame",*frame);const auto m=frameOf(profile);gp_Trsf transform;transform.SetValues(m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14]);return transformed(shape,transform);};
    const TopoDS_Shape a=input("a_brep","a_transform"),b=input("b_brep","b_transform");
    BRepExtrema_DistShapeShape distance(a,b);distance.Perform();
    if(!distance.IsDone()||distance.NbSolution()<1||!std::isfinite(distance.Value())||distance.Value()<0)throw std::runtime_error("Exact collision distance failed");
    const TopoDS_Shape overlap=common(a,b);
    if(!overlap.IsNull()&&!BRepCheck_Analyzer(overlap).IsValid())throw std::runtime_error("Exact collision common failed B-rep validation");
    const auto solids=shapes(overlap,TopAbs_SOLID);double volume=0;
    for(const auto& solid:solids){shapeReport(solid,true);volume+=properties(solid);}
    if(!std::isfinite(volume)||volume<0)throw std::runtime_error("Exact collision volume is invalid");
    const bool interference=volume>0,contact=!interference&&distance.Value()<=kTolerance;
    V result=V::makeObj(),points=V::makeArr();
    points.arr.push_back(vec(distance.PointOnShape1(1).XYZ()));points.arr.push_back(vec(distance.PointOnShape2(1).XYZ()));
    result.set("status",V::makeStr(interference?"interference":contact?"touching":"separated"));
    result.set("min_distance_mm",V::makeNum(distance.Value()));result.set("common_volume_mm3",V::makeNum(volume));
    result.set("interference",V::makeBool(interference));result.set("touching",V::makeBool(contact));
    result.set("nearest_points_mm",points);result.set("inner_solution",V::makeBool(distance.InnerSolution()));
    result.set("contact_tolerance_mm",V::makeNum(kTolerance));result.set("common_solids",V::makeNum(solids.size()));
    result.set("authority",V::makeStr("exact_native_BRep_distance_and_boolean_common"));
    result.set("display_mesh_authoritative",V::makeBool(false));result.set("trajectory_checked",V::makeBool(false));
    result.set("response_dynamics_checked",V::makeBool(false));
    result.set("a_brep_sha256",V::makeStr(sha256(base64Decode(required(request,"a_brep").str))));
    result.set("b_brep_sha256",V::makeStr(sha256(base64Decode(required(request,"b_brep").str))));return result;
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
    if (source.ShapeType() == TopAbs_SHELL) {
        // A closed shell has no free boundary to offset from; OCCT only reports a bare failure.
        TopTools_IndexedDataMapOfShapeListOfShape faces;
        TopExp::MapShapesAndAncestors(source, TopAbs_EDGE, TopAbs_FACE, faces);
        bool open = false;
        for (int i = 1; i <= faces.Extent() && !open; ++i)
            open = faces(i).Extent() == 1 && !BRep_Tool::Degenerated(TopoDS::Edge(faces.FindKey(i)));
        if (!open) throw std::runtime_error("thicken requires an open face or shell, but this shell is closed (no free boundary edges); thicken an open skin (an open profile with solid:0), or build the solid with solid:1 and hollow it with native_solid shell");
    }
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
    // Reversal negates the same integral, so the measured volume is reused.
    const double signedVolume = properties(solid);
    if (signedVolume < 0) solid.Reverse();
    return solidShapeResult(solid, std::abs(signedVolume));
}
} // namespace

V profileCurveNative(const V& profile){
    auto resolved=profileOf(profile,false);V request=V::makeObj();request.set("op",V::makeStr("curve_brep"));request.set("wire_brep",V::makeStr(brepData(resolved.wire)));return modelCurveNative(request);
}
V modelSurfaceNative(const V& request) {
    try {
        auto dispatch=[&]() -> V {
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
        if (operation.str == "surface_extract_face") return extractSurfaceFace(request);
        if (operation.str == "surface_trim_plane") return trimSurfacePlane(request);
        if (operation.str == "surface_trim_wire") return trimSurfaceWire(request);
        if (operation.str == "surface_untrim") return untrimExtendSurface031B(request, false);
        if (operation.str == "surface_extend") return untrimExtendSurface031B(request, true);
        if (operation.str == "surface_mutual_trim") return mutualTrimSurfaces031B(request);
        if (operation.str == "surface_repair") return repairSurfaceOrientation(request);
        if (operation.str == "sew") return sewSurfaces(request);
        if (operation.str == "collision") return collisionReport(request);
        if (operation.str == "helix" || operation.str == "curve" || operation.str == "curve_interpolate" || operation.str == "curve_analytic" || operation.str == "surface_spline" || operation.str == "curve_project_surface" || operation.str == "intersection_curve" || operation.str == "bridge")
            return modelCurveNative(request);
        throw std::runtime_error("unsupported surfacing operation");
        };
        return completeSelectionReferences(dispatch());
    } catch (const Standard_Failure& error) {
        throw std::runtime_error(std::string("OCCT surfacing failure: ") + error.GetMessageString());
    }
}
} // namespace ark::occt
