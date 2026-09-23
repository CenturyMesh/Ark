#include "ark/occt_source.hpp"
#include "common.hpp"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace ark::occt {
namespace {
using V = arkjson::Value;
namespace fs = std::filesystem;
const V& field(const V& object,const char* key) {
    const V* value=object.find(key);
    if(!value)throw std::runtime_error(std::string("Missing notch plan field: ")+key);
    return *value;
}
double number(const V& object,const char* key) {
    const V& value=field(object,key);
    if(value.t!=V::Num||!std::isfinite(value.num))throw std::runtime_error(std::string("Invalid finite number: ")+key);
    return value.num;
}
std::string string(const V& object,const char* key) {
    const V& value=field(object,key);
    if(value.t!=V::Str)throw std::runtime_error(std::string("Invalid text: ")+key);
    return value.str;
}
std::vector<double> triple(const V& value) {
    if(value.t!=V::Arr||value.arr.size()!=3)throw std::runtime_error("Invalid three-coordinate vector");
    std::vector<double> result;
    for(const auto& item:value.arr) {
        if(item.t!=V::Num||!std::isfinite(item.num))throw std::runtime_error("Non-finite cutter coordinate");
        result.push_back(item.num);
    }
    return result;
}
gp_Pnt point(const std::vector<double>& x) {return {x[0],x[1],x[2]};}
double volume(const TopoDS_Shape& shape) {
    GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);return properties.Mass();
}
int solidCount(const TopoDS_Shape& shape) {
    int count=0;for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next())++count;return count;
}
TopoDS_Shape notchCut(const TopoDS_Shape& a,const TopoDS_Shape& b) {
    BRepAlgoAPI_Cut op(a,b);op.Build();
    if(!op.IsDone()||op.Shape().IsNull())throw std::runtime_error("OCCT Boolean cut failed");
    return op.Shape();
}
TopoDS_Shape notchCommon(const TopoDS_Shape& a,const TopoDS_Shape& b) {
    BRepAlgoAPI_Common op(a,b);op.Build();
    if(!op.IsDone()||op.Shape().IsNull())throw std::runtime_error("OCCT Boolean common failed");
    return op.Shape();
}
std::vector<double> place(const std::string& face,double height,double width,double x,double y,double z) {
    if(face=="bottom")return {x,y,z};
    if(face=="top")return {x,height-y,width-z};
    if(face=="left")return {x,height-z,y};
    if(face=="right")return {x,z,width-y};
    throw std::runtime_error("Unknown hinge face");
}
void writeText(const fs::path& path,const std::string& text) {
    std::ofstream output(path,std::ios::binary);output<<text;
    if(!output)throw std::runtime_error("Cannot write STEP validation file");
}
}

V exportNotchNative(const V& plan,const std::string& target) {
    if(string(plan,"format")!="ark-cut-plan"||number(plan,"version")!=1||
       string(plan,"model")!="ideal_zero_radius_hinge_preview"||
       field(plan,"manufacturing_authoritative").t!=V::Bool||field(plan,"manufacturing_authoritative").b)
        throw std::runtime_error("Expected an explicitly non-authoritative ARK preview plan");
    const V& notches=field(plan,"notches");
    if(notches.t!=V::Arr||notches.arr.size()>64)throw std::runtime_error("Expected 0..64 notches");
    const V& stock=field(plan,"stock");
    const double width=number(stock,"width_mm"),height=number(stock,"height_mm"),wall=number(stock,"wall_mm"),
                 length=number(stock,"length_mm"),hinge=number(stock,"hinge_offset_mm");
    if(!(width<=10000&&width>2*wall&&wall>0&&height<=10000&&height>2*wall&&length>0&&length<=1e6&&hinge>=0&&hinge<=wall))
        throw std::runtime_error("Invalid stock dimensions");
    TopoDS_Shape shape=notchCut(BRepPrimAPI_MakeBox(length,height,width).Shape(),
                           BRepPrimAPI_MakeBox(gp_Pnt(-1,wall,wall),length+2,height-2*wall,width-2*wall).Shape());
    const double stockVolume=volume(shape);
    double removed=0;
    std::vector<std::pair<double,double>> envelopes;
    struct Retained{std::string face;double lo,hi;};
    std::vector<Retained> retained;
    for(const V& notch:notches.arr) {
        const std::string face=string(notch,"hinge_face");
        if(face!="bottom"&&face!="top"&&face!="left"&&face!="right")throw std::runtime_error("Unknown hinge face");
        const bool broad=(face=="bottom"||face=="top");
        const double depth=broad?height:width,breadth=broad?width:height;
        const double angle=number(notch,"angle_deg"),station=number(notch,"station_mm");
        if(!(angle>0&&angle<170))throw std::runtime_error("Invalid notch angle/station");
        const double slope=std::tan(angle*std::acos(-1.)/360.),opening=(depth-hinge)*slope;
        const double lo=station-opening,hi=station+opening;
        if(lo<=0||hi>=length)throw std::runtime_error("Notch envelope reaches a stock end");
        for(const auto& e:envelopes)if(lo<=e.second&&hi>=e.first)throw std::runtime_error("Notch envelopes overlap");
        envelopes.emplace_back(lo,hi);retained.push_back({face,lo,hi});
        const double lower=(wall-hinge)*slope,upper=(depth+1-hinge)*slope;
        const std::vector<std::vector<double>> expected{
            place(face,height,width,station-lower,wall,-1),
            place(face,height,width,station-upper,depth+1,-1),
            place(face,height,width,station+upper,depth+1,-1),
            place(face,height,width,station+lower,wall,-1)};
        const V& polygon=field(notch,"cutter_polygon");
        if(polygon.t!=V::Arr||polygon.arr.size()!=4)throw std::runtime_error("Invalid cutter polygon");
        BRepBuilderAPI_MakePolygon wire;
        for(size_t i=0;i<4;++i) {
            const auto supplied=triple(polygon.arr[i]);
            for(int k=0;k<3;++k)if(std::abs(supplied[k]-expected[i][k])>1e-8)
                throw std::runtime_error("Cutter polygon disagrees with independent notch equations");
            wire.Add(point(supplied));
        }
        wire.Close();
        if(!wire.IsDone())throw std::runtime_error("Invalid cutter wire");
        const auto suppliedDirection=triple(field(notch,"cutter_extrusion"));
        const auto origin=place(face,height,width,0,0,0),end=place(face,height,width,0,0,breadth+2);
        for(int k=0;k<3;++k)if(std::abs(suppliedDirection[k]-(end[k]-origin[k]))>1e-8)
            throw std::runtime_error("Cutter extrusion disagrees with selected hinge face");
        const TopoDS_Shape cutter=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()).Face(),
                                                        gp_Vec(suppliedDirection[0],suppliedDirection[1],suppliedDirection[2])).Shape();
        shape=notchCut(shape,cutter);
        const auto integral=[&](double a,double b,double breadthAtDepth) {
            return slope*breadthAtDepth*((b-hinge)*(b-hinge)-(a-hinge)*(a-hinge));
        };
        removed+=integral(wall,depth-wall,2*wall)+integral(depth-wall,depth,breadth);
    }
    double maxHingeError=0;
    for(const Retained& r:retained) {
        const double ymin=r.face=="top"?height-wall:0,ymax=r.face=="bottom"?wall:height;
        const double zmin=r.face=="right"?width-wall:0,zmax=r.face=="left"?wall:width;
        const TopoDS_Shape region=BRepPrimAPI_MakeBox(gp_Pnt(r.lo,ymin,zmin),r.hi-r.lo,ymax-ymin,zmax-zmin).Shape();
        maxHingeError=std::max(maxHingeError,std::abs(volume(notchCommon(shape,region))-(r.hi-r.lo)*(ymax-ymin)*(zmax-zmin)));
    }
    const double expected=stockVolume-removed,actual=volume(shape),tolerance=std::max(1e-6,expected*1e-9);
    if(!BRepCheck_Analyzer(shape).IsValid()||solidCount(shape)!=1)throw std::runtime_error("Cut blank must be one valid connected solid");
    if(maxHingeError>tolerance)throw std::runtime_error("A retained hinge wall lost material");
    if(std::abs(expected-actual)>tolerance)throw std::runtime_error("B-rep volume differs from analytical cut integral");
    const fs::path destination=fs::u8path(target);
    fs::create_directories(destination.parent_path());
    fs::path temporary=destination;temporary.replace_extension(".pending.step");
    STEPControl_Writer writer;
    if(writer.Transfer(shape,STEPControl_AsIs)!=IFSelect_RetDone||writer.Write(temporary.string().c_str())!=IFSelect_RetDone)
        throw std::runtime_error("STEP export failed");
    STEPControl_Reader reader;
    if(reader.ReadFile(temporary.string().c_str())!=IFSelect_RetDone||reader.TransferRoots()==0)
        throw std::runtime_error("STEP readback failed");
    const TopoDS_Shape readback=reader.OneShape();
    const double difference=std::abs(volume(notchCut(shape,readback)))+std::abs(volume(notchCut(readback,shape)));
    if(!BRepCheck_Analyzer(readback).IsValid()||solidCount(readback)!=1||
       std::abs(volume(readback)-actual)>tolerance||difference>tolerance)
        throw std::runtime_error("STEP roundtrip changed the solid");
    fs::rename(temporary,destination);
    fs::path sidecar=destination;sidecar.replace_extension(".arkcut");writeText(sidecar,plan.dump()+"\n");
    V report=V::makeObj();
    report.set("format",V::makeStr("ark-step-validation"));report.set("version",V::makeNum(1));
    report.set("brep_valid",V::makeBool(true));report.set("solids",V::makeNum(1));
    report.set("stock_volume_mm3",V::makeNum(stockVolume));report.set("expected_removed_volume_mm3",V::makeNum(removed));
    report.set("retained_wall_volume_max_error_mm3",V::makeNum(maxHingeError));
    report.set("measured_remaining_volume_mm3",V::makeNum(actual));report.set("volume_error_mm3",V::makeNum(std::abs(expected-actual)));
    report.set("roundtrip_symmetric_difference_volume_mm3",V::makeNum(difference));report.set("volume_tolerance_mm3",V::makeNum(tolerance));
    report.set("manufacturing_authoritative",V::makeBool(false));report.set("source_reconstruction_verified",V::makeBool(false));
    report.set("finite_wall_deformation_verified",V::makeBool(false));report.set("model",V::makeStr("ideal_zero_radius_hinge_preview"));
    report.set("native_kernel_version",V::makeStr(kernelVersion()));
    report.set("step_sha256",V::makeStr(sha256(readFile(destination))));
    sidecar=destination;sidecar.replace_extension(".validation.json");writeText(sidecar,report.dump()+"\n");
    return report;
}
} // namespace ark::occt
