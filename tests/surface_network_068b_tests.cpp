#include "ark/occt_modeling.hpp"
#include "../src/occt/common.hpp"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRep_Tool.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Geom_BezierCurve.hxx>
#include <NCollection_Array1.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <cmath>
#include <iostream>
#include <stdexcept>
using V=arkjson::Value;
static int checks=0;
static void check(bool x,const char* why){++checks;if(!x)throw std::runtime_error(why);}
static V json(const char* text){V v;check(arkjson::parse(text,v),"Fixture JSON");return v;}
static const V& get(const V& v,const char* key){const auto* p=v.find(key);if(!p)throw std::runtime_error(std::string("Missing ")+key);return *p;}
static void close(double a,double b,double tolerance,const char* why){check(std::abs(a-b)<=tolerance,why);}
template<class F>static void reject(F run,const char* why){bool failed=false;try{run();}catch(const std::exception&){failed=true;}check(failed,why);}
static V line(gp_Pnt a,gp_Pnt b){return V::makeStr(ark::occt::brepData(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(a,b).Edge()).Wire()));}
static V bezier(std::initializer_list<gp_Pnt> values){NCollection_Array1<gp_Pnt> poles(1,(int)values.size());int n=1;for(const auto& p:values)poles(n++)=p;Handle(Geom_BezierCurve) c=new Geom_BezierCurve(poles);return V::makeStr(ark::occt::brepData(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(c).Edge()).Wire()));}
int main(){try{
    V request=json(R"({"op":"surface_network","u_count":3})"),curves=V::makeArr();
    for(double v:{0.,.5,1.}){const double base=2*v*(1-v);curves.arr.push_back(bezier({{0,10*v,base},{10,10*v,base+2+2.5*v},{20,10*v,base+5*v}}));}
    for(double u:{0.,.5,1.}){const double base=4*u*(1-u);curves.arr.push_back(bezier({{20*u,0,base},{20*u,5,base+1+2.5*u},{20*u,10,base+5*u}}));}
    request.set("input_breps",curves);const auto original=request.dump();const auto result=ark::occt::modelSurfaceNative(request);check(request.dump()==original,"Network mutated source requests");
    const auto shape=ark::occt::readBrep(get(result,"brep_base64").str);check(BRepCheck_Analyzer(shape).IsValid(),"Network B-rep invalid");const auto faces=ark::occt::shapes(shape,TopAbs_FACE);check(faces.size()==1,"Network is not one face");auto surface=BRep_Tool::Surface(TopoDS::Face(faces.front()));
    for(int i=0;i<=12;++i)for(int j=0;j<=10;++j){const double u=i/12.,v=j/10.;const auto p=surface->Value(u,v);close(p.X(),20*u,1e-9,"Independent network X");close(p.Y(),10*v,1e-9,"Independent network Y");close(p.Z(),4*u*(1-u)+2*v*(1-v)+5*u*v,1e-9,"Independent quadratic Gordon surface");}
    const auto& report=get(result,"report");check(get(report,"u_degree").num==3&&get(report,"v_degree").num==3,"Network output must provide exact cubic control basis");close(get(report,"curve_observed_max_error_mm").num,0,1e-9,"Original curves must be retained");check(get(report,"curve_checkpoints").num==390,"All six curves need independent checkpoints");check(!get(report,"global_injectivity_verified").b,"No unsupported global injectivity claim");
    V invalid=request,bad=curves;bad.arr[4]=bezier({{10,0,2},{10,5,4.25},{10,10,4.5}});invalid.set("input_breps",bad);reject([&]{ark::occt::modelSurfaceNative(invalid);},"Inconsistent crossings accepted");
    invalid=request;invalid.set("u_stations",json("[0,0.75,1]"));reject([&]{ark::occt::modelSurfaceNative(invalid);},"Wrong station correspondence accepted");invalid=request;invalid.set("u_stations",json("[0,0,1]"));reject([&]{ark::occt::modelSurfaceNative(invalid);},"Duplicate station accepted");invalid=request;invalid.set("tolerance",V::makeNum(.1));reject([&]{ark::occt::modelSurfaceNative(invalid);},"Relaxed native tolerance accepted");
    V reversed=request;V reversedCurves=curves;reversedCurves.arr[0]=bezier({{20,0,0},{10,0,2},{0,0,0}});reversed.set("input_breps",reversedCurves);reject([&]{ark::occt::modelSurfaceNative(reversed);},"Unreviewed reversal accepted");reversed.set("u_reverse",json("[true,false,false]"));const auto restored=ark::occt::modelSurfaceNative(reversed);close(get(get(restored,"report"),"curve_observed_max_error_mm").num,0,1e-9,"Explicit reverse did not restore network");
    V loft=json(R"({"op":"loft_diagnostics","profiles":[]})"),profiles=V::makeArr();for(const auto& encoded:{line({0,0,0},{20,0,0}),line({20,10,0},{0,10,0})}){V profile=V::makeObj();profile.set("wire_brep",encoded);profiles.arr.push_back(profile);}loft.set("profiles",profiles);auto diagnostics=ark::occt::modelSurfaceNative(loft);check(get(get(diagnostics,"pairs").arr.front(),"review_reversal").b,"Reversed loft section was not diagnosed");loft.set("controls",json(R"({"section_alignment":[{"reverse":false,"seam_edge":0,"seam_angle":0},{"reverse":true,"seam_edge":0,"seam_angle":0}]})"));diagnostics=ark::occt::modelSurfaceNative(loft);check(!get(get(diagnostics,"pairs").arr.front(),"review_reversal").b,"Explicit correspondence correction remains flagged");close(get(get(diagnostics,"pairs").arr.front(),"correspondence_rms_mm").num,10,1e-7,"Native arc-length correspondence distance");check(!get(diagnostics,"automatic_changes").b,"Diagnostics must never alter orientation");
    std::cout<<checks<<" native curve-network and loft checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
