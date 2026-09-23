#include "ark/occt_source.hpp"
#include "ark/notch.hpp"
#include "../src/occt/common.hpp"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace fs=std::filesystem;
using ark::occt::V;
using namespace ark::occt;
static void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static gp_Trsf shift(double x,double y,double z){gp_Trsf m;m.SetTranslation(gp_Vec(x,y,z));return m;}
static TopoDS_Shape boxTube(double length,double height,double width,double wall){
    return cut(BRepPrimAPI_MakeBox(length,height,width).Shape(),
               BRepPrimAPI_MakeBox(gp_Pnt(-1,wall,wall),length+2,height-2*wall,width-2*wall).Shape());
}
struct Fixture{TopoDS_Shape source,damaged;double length;};
static Fixture fixture(){
    constexpr double width=29,height=38,wall=1.8,inner=3.25;
    const double neutral=inner+wall/2,outer=inner+wall,angle=70*std::acos(-1.)/180;
    constexpr double before=480,after=420,relief=45;
    const double h=wall/2;
    TopoDS_Shape left=cut(boxTube(before,height,width,wall),
                          BRepPrimAPI_MakeBox(gp_Pnt(before-relief,wall,-1),relief+1,height,width+2).Shape());
    TopoDS_Shape right=cut(boxTube(after,height,width,wall),
                           BRepPrimAPI_MakeBox(gp_Pnt(-1,wall,-1),relief+1,height,width+2).Shape());
    left=cut(left,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(100,height/2,-1),gp_Dir(0,0,1)),3,width+2).Shape());
    BRepBuilderAPI_MakePolygon polygon;
    for(int k=0;k<6;++k)polygon.Add(gp_Pnt(210+4*std::cos(k*std::acos(-1.)/3),
                                           height/2+4*std::sin(k*std::acos(-1.)/3),-1));
    polygon.Close();
    left=cut(left,BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(polygon.Wire()).Face(),gp_Vec(0,0,width+2)).Shape());
    right=cut(right,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(170,height/2,-1),gp_Dir(0,0,1)),2.4,width+2).Shape());
    const gp_Ax2 cyl(gp_Pnt(before,h+neutral,0),gp_Dir(0,0,1),gp_Dir(0,-1,0));
    TopoDS_Shape hinge=cut(BRepPrimAPI_MakeCylinder(cyl,outer,width,angle).Shape(),
                          BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(before,h+neutral,-1),gp_Dir(0,0,1)),inner,width+2).Shape());
    gp_Trsf turn;turn.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),angle);
    const gp_Trsf placement=shift(before+neutral*std::sin(angle),h+neutral*(1-std::cos(angle)),0)*turn*shift(0,-h,0);
    TopoDS_Shape source=fuse(fuse(left,hinge),transformed(right,placement));
    const double s=std::sin(angle/2),c=-std::cos(angle/2);
    const gp_Pnt drill(before+(inner-1)*s,h+neutral+(inner-1)*c,width/2);
    TopoDS_Shape damaged=cut(source,BRepPrimAPI_MakeCylinder(gp_Ax2(drill,gp_Dir(s,c,0)),1.1,wall+2).Shape());
    return {source,damaged,before+after+neutral*angle};
}
static bool rejectsImport(const fs::path& source,const fs::path& cache){
    try{importSourceTubeNative(source.string(),cache.string());return false;}
    catch(const std::exception&){return true;}
}
int main(){
    const fs::path root=fs::temp_directory_path()/"ark-native-source-tests";
    fs::create_directories(root);
    const Fixture sample=fixture();
    const fs::path source=root/"source.step",rotated=root/"rotated.step",damaged=root/"damaged.step";
    writeStep(sample.source,source);
    const V first=importSourceTubeNative(source.string(),(root/"base").string());
    const V& report=*first.find("report");
    check(report.find("opening_count")->asNum()==6,"Synthetic opening inventory lost contours");
    check(report.find("opening_kinds")->find("circular")->asNum()==4,"Circular opening count changed");
    check(report.find("opening_kinds")->find("polygonal")->asNum()==2,"Polygonal opening count changed");
    const auto& b=report.find("flat_bounds_mm")->arr;
    check(std::abs(b[3].num-b[0].num-sample.length)<1e-5,"Flat length differs from finite neutral arc");
    check(report.find("source_projection_verified")->b,"Source projection proof missing");
    check(report.find("refold_symmetric_difference_volume_mm3")->num<report.find("volume_tolerance_mm3")->num,
          "Source refold Boolean difference exceeds tolerance");
    gp_Trsf rigid;rigid.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.47);
    rigid.SetTranslationPart(gp_Vec(123,-75,210));
    writeStep(transformed(sample.source,rigid),rotated);
    const V second=importSourceTubeNative(rotated.string(),(root/"rotated-cache").string());
    const V& secondReport=*second.find("report");
    check(secondReport.find("opening_count")->asNum()==6,"Rotated source lost openings");
    check(std::abs(secondReport.find("flat_volume_mm3")->num-report.find("flat_volume_mm3")->num)<
          report.find("volume_tolerance_mm3")->num,"Rotated source changed flat volume");
    const auto& firstHoles=first.find("holes")->arr;const auto& secondHoles=second.find("holes")->arr;
    check(firstHoles.size()==secondHoles.size(),"Metamorphic opening count differs");
    for(size_t i=0;i<firstHoles.size();++i){
        check(firstHoles[i].find("kind")->str==secondHoles[i].find("kind")->str,"Metamorphic opening kind changed");
        const auto& a=firstHoles[i].find("flat_center")->arr;const auto& c=secondHoles[i].find("flat_center")->arr;
        for(int k=0;k<3;++k)check(std::abs(a[k].num-c[k].num)<1e-5,"Metamorphic opening center changed");
    }
    writeStep(sample.damaged,damaged);
    check(rejectsImport(damaged,root/"damaged-cache"),"Hole through deforming hinge was accepted");
    const fs::path exported=root/"verified.step";
    const V receipt=exportSourceTubeNative(first,exported.string());
    check(receipt.find("brep_valid")->b&&fs::exists(exported),"Embedded source STEP export failed");
    V tampered=first;std::string encoded=tampered.find("flat_step_base64")->str;
    encoded[encoded.size()/2]=encoded[encoded.size()/2]=='A'?'B':'A';
    tampered.set("flat_step_base64",V::makeStr(encoded));
    bool refused=false;try{exportSourceTubeNative(tampered,(root/"tampered.step").string());}catch(const std::exception&){refused=true;}
    check(refused,"Changed embedded STEP escaped SHA gate");
    writeFile(source,readFile(source)+"\n");
    refused=false;try{exportSourceTubeNative(first,(root/"drifted.step").string());}catch(const std::exception&){refused=true;}
    check(refused,"Changed source STEP escaped drift gate");
    for(const char* face:{"bottom","top","left","right"}){
        V request;check(arkjson::parse(R"({"width_mm":40,"height_mm":30,"wall_mm":2,"length_mm":500,"hinge_offset_mm":1,"notches":[{"station_mm":250,"angle_deg":65,"hinge_face":"bottom"}]})",request),"Invalid test request");
        V points=*request.find("notches"),notch=points.arr.front();notch.set("hinge_face",V::makeStr(face));points.arr[0]=notch;request.set("notches",points);
        V plan=ark::planNotchedBlank(request);
        const V result=exportNotchNative(plan,(root/(std::string(face)+".step")).string());
        check(result.find("brep_valid")->b&&result.find("solids")->num==1,"Notch STEP invalid");
        check(result.find("roundtrip_symmetric_difference_volume_mm3")->num<result.find("volume_tolerance_mm3")->num,
              "Notch STEP readback changed cut solid");
        V altered=plan;V notches=*altered.find("notches"),pointArray=*notches.arr[0].find("cutter_polygon");
        V coord=pointArray.arr[0];coord.arr[0]=V::makeNum(coord.arr[0].num+1);pointArray.arr[0]=coord;
        notches.arr[0].set("cutter_polygon",pointArray);altered.set("notches",notches);
        refused=false;try{exportNotchNative(altered,(root/"bad-notch.step").string());}catch(const std::exception&){refused=true;}
        check(refused,"Modified notch cutter escaped independent equation gate");
        coord.arr[0]=V::makeNum(std::numeric_limits<double>::quiet_NaN());pointArray.arr[0]=coord;
        notches.arr[0].set("cutter_polygon",pointArray);altered.set("notches",notches);
        refused=false;try{exportNotchNative(altered,(root/"nonfinite-notch.step").string());}catch(const std::exception&){refused=true;}
        check(refused,"Non-finite notch cutter escaped validation");
    }
    std::cout<<"Native source projection, rigid metamorphism, hinge-hole rejection, source drift and notch STEP gates passed\n";
}
