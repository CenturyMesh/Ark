#include "common.hpp"
#include <BRepGProp.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomConvert.hxx>
#include <GProp_GProps.hxx>
#include <gp_Ax2.hxx>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace ark::occt;
namespace {
int checks=0;
constexpr double pi=3.14159265358979323846;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);++checks;}
void relative(double actual,double expected,double tolerance,const char* why){if(std::abs(actual-expected)>tolerance*std::max(1.,std::abs(expected))){std::cerr<<std::setprecision(17)<<why<<" actual="<<actual<<" expected="<<expected<<" delta="<<(actual-expected)<<'\n';throw std::runtime_error(why);}++checks;}
TopoDS_Shape tube(double outer,double inner,double length,gp_Pnt origin={},gp_Dir axis=gp_Dir(0,0,1)){
    return cut(BRepPrimAPI_MakeCylinder(gp_Ax2(origin,axis),outer,length).Shape(),BRepPrimAPI_MakeCylinder(gp_Ax2(origin,axis),inner,length).Shape());
}
}
int main(){try{
    const auto started=std::chrono::steady_clock::now();
    const double outer=15,inner=13,length=400,volume=pi*(outer*outer-inner*inner)*length;
    auto rail=tube(outer,inner,length,{},gp_Dir(1,0,0));
    check(std::string(volumeIntegrationMethod(rail))=="analytic_gauss","Analytic hollow rail uses the analytic integration domain");
    GProp_GProps railProperties;volumeProperties(rail,railProperties);
    relative(railProperties.Mass(),volume,1e-12,"Analytic tube volume");
    relative(railProperties.CentreOfMass().X(),length/2,1e-12,"Analytic tube centroid");
    auto inertia=railProperties.MatrixOfInertia();
    relative(inertia.Value(1,1),volume*(outer*outer+inner*inner)/2,1e-10,"Tube axial inertia");
    relative(inertia.Value(2,2),volume*(3*(outer*outer+inner*inner)+length*length)/12,1e-10,"Tube transverse inertia");
    GProp_GProps reference;const double referenceError=BRepGProp::VolumePropertiesGK(rail,reference,1e-10,false,true,false,false,false);check(referenceError>=0,"Analytic volume reference integrates");relative(railProperties.Mass(),reference.Mass(),1e-12,"Analytic Gauss agrees with GK scalar reference");
    auto brace=tube(outer,inner,200,gp_Pnt(200,0,0));
    auto cutter=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(1,0,0)),outer,400).Shape();
    auto coped=cut(brace,cutter);
    check(std::string(volumeIntegrationMethod(coped))=="adaptive_gauss_polynomial_trim","Coped tube polynomial trims use adaptive Gauss");
    // Independent smooth integral of the hollow-cylinder/solid-cylinder intersection.
    // y=inner*sin(theta) removes the endpoint square-root singularity.
    constexpr int intervals=8192;const double step=(pi/2)/intervals;double integral=0;
    for(int i=0;i<=intervals;++i){double theta=step*i,sine=std::sin(theta),cosine=std::cos(theta);double value=inner*inner*cosine*cosine*std::sqrt(outer*outer-inner*inner*sine*sine);integral+=(i==0||i==intervals?1:i%2?4:2)*value;}
    integral*=step/3;
    const double before=pi*(outer*outer-inner*inner)*200,removed=8*std::pow(outer,3)/3-4*integral,expected=before-removed;
    GProp_GProps copeProperties;volumeProperties(coped,copeProperties);
    relative(copeProperties.Mass(),expected,1e-10,"Adaptive cope volume matches independent integration");
    const double removedZMoment=3*pi*std::pow(outer,4)/8-pi*inner*inner*(outer*outer/2-inner*inner/8);
    relative(copeProperties.CentreOfMass().Z(),(before*100-removedZMoment)/expected,1e-10,"Adaptive cope centroid matches independent integral");
    GProp_GProps legacy;BRepGProp::VolumeProperties(coped,legacy);check(std::abs(legacy.Mass()-expected)>.001,"Cope repro would fail the legacy nonadaptive metric");
    occ::handle<Geom_Circle> circle=new Geom_Circle(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,1)),100);
    occ::handle<Geom_TrimmedCurve> trimmed=new Geom_TrimmedCurve(circle,0,2*pi);auto spline=GeomConvert::CurveToBSplineCurve(trimmed);
    auto edge=BRepBuilderAPI_MakeEdge(spline).Edge();auto wire=BRepBuilderAPI_MakeWire(edge).Wire();auto face=BRepBuilderAPI_MakeFace(wire,true).Face();auto disk=BRepPrimAPI_MakePrism(face,gp_Vec(0,0,2)).Shape();
    check(std::string(volumeIntegrationMethod(disk))=="gauss_kronrod_rational_or_freeform","Rational disk retains Gauss-Kronrod knot spans");
    GProp_GProps diskProperties;volumeProperties(disk,diskProperties);const double diskVolume=pi*100*100*2;
    relative(diskProperties.Mass(),diskVolume,1e-10,"Rational disk precise volume");relative(diskProperties.CentreOfMass().Z(),1,1e-10,"Rational disk centroid");
    inertia=diskProperties.MatrixOfInertia();relative(inertia.Value(3,3),diskVolume*100*100/2,1e-10,"Rational disk axial inertia");relative(inertia.Value(1,1),diskVolume*(3*100*100+4)/12,1e-10,"Rational disk transverse inertia");
    std::cout<<"Native volume integration checks: "<<checks<<" passed; elapsed_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
