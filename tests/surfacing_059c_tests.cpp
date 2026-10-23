#include "ark/occt_modeling.hpp"
#include "../src/occt/common.hpp"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <GeomConvert.hxx>
#include <NCollection_Array1.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pln.hxx>
#include <gp_Ax3.hxx>
#include <cmath>
#include <iostream>
#include <stdexcept>
using V=arkjson::Value;
namespace {
unsigned checks=0;
void check(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
void closeTo(double a,double b,double tol,const char* why){check(std::abs(a-b)<=tol,std::string(why)+": "+std::to_string(a)+" != "+std::to_string(b));}
const V& field(const V& v,const char* key){const auto* p=v.find(key);if(!p)throw std::runtime_error(std::string("Missing ")+key+" in "+v.dump());return *p;}
V json(const char* text){V v;check(arkjson::parse(text,v),"fixture JSON");return v;}
V brep(const TopoDS_Shape& s){return V::makeStr(ark::occt::brepData(s));}
V run(const V& request){return ark::occt::modelSurfaceNative(request);}
template<class F>void rejects(F fn,const char* why){try{fn();}catch(const std::exception&){++checks;return;}check(false,std::string("Expected rejection: ")+why);}
TopoDS_Face plane(double u0=0,double u1=10,double v0=0,double v1=10){return BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(),gp_Dir(0,0,1),gp_Dir(1,0,0))),u0,u1,v0,v1).Face();}
TopoDS_Wire line(gp_Pnt a,gp_Pnt b){return BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(a,b).Edge()).Wire();}
void valid(const V& result){const auto shape=ark::occt::readBrep(field(result,"brep_base64").str);check(BRepCheck_Analyzer(shape).IsValid(),"valid exact result");check(!field(field(result,"topology"),"references").arr.empty(),"digest-bound semantic selection inventory");}
void mirror() {
    auto request=json(R"({"op":"surface_mirror","origin":[0,0,0],"normal":[1,0,0],"continuity":"G1"})");request.set("input_brep",brep(plane()));
    auto result=run(request);valid(result);const auto& report=field(result,"report");
    closeTo(field(report,"area_mm2").num,200,1e-6,"mirror doubles planar area");closeTo(field(report,"symmetry_seam_length_mm").num,10,1e-6,"exact symmetry seam length");
    check(field(report,"symmetry_seams").arr.size()==1,"one plane seam");closeTo(field(report,"observed_max_seam_tangent_angle_deg").num,0,1e-6,"planar G1 measured");
    check(!field(report,"global_g1_verified").b&&!field(report,"source_geometry_modified").b,"mirror does not claim global continuity or alter its half");
    const auto first=field(result,"brep_base64").str;check(first!=field(request,"input_brep").str,"mirror produces a new B-rep");

    const auto curved=run(json(R"({"op":"surface_nurbs","u_degree":2,"v_degree":1,"poles":[[[0,0,0],[0,10,0]],[[5,0,0],[5,10,0]],[[10,0,1],[10,10,1]]]})"));
    request.set("input_brep",field(curved,"brep_base64"));result=run(request);valid(result);
    closeTo(field(field(result,"report"),"area_mm2").num,2*field(field(curved,"report"),"area_mm2").num,1e-5,"curved mirror conserves exact support area");
    closeTo(field(field(result,"report"),"observed_max_seam_tangent_angle_deg").num,0,.001,"parabolic half meets mirrored tangent plane");

    auto ridge=run(json(R"({"op":"surface_nurbs","u_degree":1,"v_degree":1,"poles":[[[0,0,0],[0,10,0]],[[10,0,2],[10,10,2]]]})"));
    request.set("input_brep",field(ridge,"brep_base64"));rejects([&]{run(request);},"G1 rejects a crease without silently smoothing");
    request.set("continuity",V::makeStr("G0"));result=run(request);closeTo(field(field(result,"report"),"observed_max_seam_tangent_angle_deg").num,2*std::atan(.2)*180/std::acos(-1.),1e-5,"G0 reports measured crease angle");
    request.set("input_brep",brep(plane(-2,10)));rejects([&]{run(request);},"crossing half");
    request.set("input_brep",brep(plane(1,10)));rejects([&]{run(request);},"gap does not auto-heal");
    request.set("input_brep",brep(plane()));request.set("normal",json("[0,0,1]"));rejects([&]{run(request);},"coplanar overlapping mirror");
    request.set("normal",json("[1,0,0]"));request.set("tolerance",V::makeNum(.001));rejects([&]{run(request);},"no tolerance broadening");
}
void trim() {
    auto request=json(R"({"op":"surface_trim_tool","tool_kind":"surface","keep_point":[8,5,0]})");request.set("input_brep",brep(plane()));
    const auto cutter=BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(4,0,0),gp_Dir(1,0,0),gp_Dir(0,1,0))),-1,11,-1,1).Face();
    request.set("tool_brep",brep(cutter));auto result=run(request);valid(result);
    closeTo(field(field(result,"report"),"retained_area_mm2").num,60,1e-6,"surface cutter keeps chosen region");
    check(field(field(result,"report"),"area_conservation_checked").b,"every split region is accounted for");
    request.set("keep_point",json("[4,5,0]"));rejects([&]{run(request);},"seed on cutter rejects");
    request.set("keep_point",json("[8,5,1]"));rejects([&]{run(request);},"off-surface seed rejects");
    request.set("keep_point",json("[1,5,0]"));request.set("tool_kind",V::makeStr("solid"));request.set("tool_brep",brep(BRepPrimAPI_MakeBox(gp_Pnt(4,-1,-1),2,12,2).Shape()));
    result=run(request);valid(result);const auto& report=field(result,"report");
    closeTo(field(report,"retained_area_mm2").num,40,1e-6,"solid cutter keeps the seed region only");closeTo(field(report,"removed_area_mm2").num,60,1e-6,"solid trim reports both removed pieces");
    check(field(report,"regions").arr.size()==3,"solid cut accounts for all three native pieces");
    request.set("tool_kind",V::makeStr("curve"));request.set("tool_brep",brep(line({4,-1,0},{4,11,0})));
    result=run(request);valid(result);closeTo(field(field(result,"report"),"retained_area_mm2").num,40,1e-6,"open exact curve trims a plane");
    request.set("tool_brep",brep(line({4,-1,1},{4,11,1})));rejects([&]{run(request);},"off-surface curve is not silently projected");

    const auto warped=run(json(R"({"op":"surface_nurbs","u_degree":1,"v_degree":1,"poles":[[[0,0,0],[0,10,0]],[[10,0,0],[10,10,4]]]})"));
    request.set("input_brep",field(warped,"brep_base64"));request.set("tool_brep",brep(line({4,0,0},{4,10,1.6})));request.set("keep_point",json("[2,5,0.4]"));
    result=run(request);valid(result);check(field(field(result,"report"),"split_regions").num==2,"3D wire trims nonplanar native support");
    request.set("tool_kind",V::makeStr("surface"));request.set("tool_brep",brep(cutter));rejects([&]{run(request);},"finite short cutter is not implicitly extended");
    request.set("tool_kind",V::makeStr("guess"));rejects([&]{run(request);},"explicit cutter type required");
}
TopoDS_Wire spline(double y) {
    NCollection_Array1<gp_Pnt> controls(1,4);controls(1)={0,y,0};controls(2)={3,y,2};controls(3)={7,y,-1};controls(4)={10,y,1};
    Handle(Geom_BezierCurve) bezier=new Geom_BezierCurve(controls);const auto curve=GeomConvert::CurveToBSplineCurve(bezier);
    return BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(curve).Edge()).Wire();
}
void ruled() {
    auto request=json(R"({"op":"surface_ruled","reverse_second":false})");request.set("first_brep",brep(line({0,0,0},{10,0,0})));request.set("second_brep",brep(line({0,5,0},{10,5,0})));
    auto result=run(request);valid(result);closeTo(field(field(result,"report"),"area_mm2").num,50,1e-6,"two straight rails make an exact ruled rectangle");
    const auto first=spline(0),second=spline(5);request.set("first_brep",brep(first));request.set("second_brep",brep(second));result=run(request);valid(result);
    BRepAdaptor_Curve curve(TopoDS::Edge(ark::occt::shapes(first,TopAbs_EDGE).front()));
    closeTo(field(field(result,"report"),"area_mm2").num,5*GCPnts_AbscissaPoint::Length(curve,1e-9),1e-5,"parallel 3D spline ruled area matches analytic extrusion integral");
    check(field(field(result,"report"),"source_preservation_samples").num==66,"both spline rails independently checked");
    closeTo(field(field(result,"report"),"observed_max_source_error_mm").num,0,1e-6,"exact spline rail preservation");
    request.set("reverse_second",V::makeBool(true));rejects([&]{run(request);},"crossing boundary correspondence does not auto-flip");
    request.set("reverse_second",V::makeBool(false));request.set("second_brep",brep(first));rejects([&]{run(request);},"zero-width ruling rejects");
    request.set("second_brep",brep(plane()));rejects([&]{run(request);},"surface cannot masquerade as a spline rail");
    BRepBuilderAPI_MakePolygon poly;poly.Add({0,5,0});poly.Add({5,5,0});poly.Add({10,5,1});request.set("second_brep",brep(poly.Wire()));rejects([&]{run(request);},"ambiguous composite rail topology rejects");
}
V fillConstraint(unsigned edge,const V& support) {
    V c=V::makeObj();c.set("edge",V::makeNum(edge));c.set("continuity",V::makeNum(1));c.set("support_brep",field(support,"brep_base64"));c.set("topology",field(support,"topology"));
    for(const auto& ref:field(field(support,"topology"),"references").arr)if(field(ref,"kind").str=="face"&&field(ref,"status").str=="bound") {c.set("face",field(ref,"name"));return c;}
    throw std::runtime_error("support has no semantic face");
}
void fillWire() {
    BRepBuilderAPI_MakePolygon polygon;polygon.Add({0,0,0});polygon.Add({10,0,0});polygon.Add({10,10,2});polygon.Add({0,10,0});polygon.Close();
    auto request=json(R"({"op":"surface_fill","profile":{}})");auto profile=V::makeObj();profile.set("wire_brep",brep(polygon.Wire()));request.set("profile",profile);
    auto result=run(request);valid(result);check(field(field(result,"report"),"boundary_max_error_mm").num<=1e-6,"closed nonplanar 3D wire fill meets its G0 bound");
    auto invalid=request;auto mixed=profile;mixed.set("wire_brep",brep(ark::occt::compound({polygon.Wire(),plane()})));invalid.set("profile",mixed);rejects([&]{run(invalid);},"fill does not silently discard extra geometry");
    mixed=profile;mixed.set("entities",V::makeArr());invalid.set("profile",mixed);rejects([&]{run(invalid);},"conflicting exact and alternate profiles reject");
    profile.set("wire_brep",brep(line({0,0,0},{10,0,0})));invalid.set("profile",profile);rejects([&]{run(invalid);},"open wire cannot be a fill boundary");

    const auto loop=json(R"({"entities":[{"kind":"bezier","points":[[0,0,0],[2,0,0],[4,0,0],[6,0,2],[8,0,2],[10,0,2]]},{"kind":"line","points":[[10,0,2],[10,5,2]]},{"kind":"bezier","points":[[10,5,2],[8,5,2],[6,5,2],[4,5,0],[2,5,0],[0,5,0]]},{"kind":"line","points":[[0,5,0],[0,0,0]]}]})");
    const auto wire=ark::occt::profileCurveNative(loop);profile=V::makeObj();profile.set("wire_brep",field(wire,"brep_base64"));request.set("profile",profile);
    auto lower=ark::occt::completeSelectionReferences(ark::occt::shapeResult(plane(-20,20,-20,20),false));gp_Trsf transform;transform.SetTranslation({0,0,2});
    auto upper=ark::occt::completeSelectionReferences(ark::occt::shapeResult(ark::occt::transformed(plane(-20,20,-20,20),transform),false));
    auto constraints=V::makeArr();constraints.arr.push_back(fillConstraint(1,upper));constraints.arr.push_back(fillConstraint(3,lower));request.set("constraints",constraints);request.set("mode",V::makeStr("blend"));
    result=run(request);valid(result);const auto& report=field(result,"report");
    check(field(report,"support_boundary_evidence").arr.size()==2,"3D closed-wire fill has both G1 supports checked");
    for(const auto& row:field(report,"support_boundary_evidence").arr){check(field(row,"samples_checked").num==33,"G1 support uses independent native checkpoints");check(field(row,"max_tangent_plane_angle_deg").num<=.001,"native 3D wire G1 tangent bound");}
    check(!field(report,"global_g1_verified").b,"sampled support proof is not a global continuity certificate");
    constraints.arr[0]=fillConstraint(1,lower);request.set("constraints",constraints);rejects([&]{run(request);},"contradictory wire support rejects");
}
}
int main(){try{mirror();trim();ruled();fillWire();std::cout<<checks<<" exact surfacing 059C checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}}
