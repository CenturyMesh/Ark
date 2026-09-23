#include "ark/occt_sheet.hpp"
#include "../src/occt/common.hpp"
#include <BRepClass3d_SolidClassifier.hxx>
#include <TopAbs_State.hxx>
#include <gp_Pnt.hxx>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

using ark::occt::V;
using namespace ark::occt;
namespace fs=std::filesystem;
static void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static bool rejects(const V& request){try{modelSheetNative(request);return false;}catch(const std::exception&){return true;}}
static V request(double angle,double k,bool holes){
    V q=V::makeObj();
    q.set("width",V::makeNum(30));q.set("leg_a",V::makeNum(80));q.set("leg_b",V::makeNum(70));
    q.set("thickness",V::makeNum(2));q.set("inner_radius",V::makeNum(4));
    q.set("angle",V::makeNum(angle));q.set("k_factor",V::makeNum(k));
    if(holes){
        V list;check(arkjson::parse(R"([{"panel":0,"kind":"circle","x":30,"y":15,"radius":3},{"panel":1,"kind":"polygon","points":[[20,10],[32,10],[32,18],[20,18]]}])",list),"Invalid test openings");
        q.set("openings",list);
    }
    return q;
}
static TopAbs_State state(const TopoDS_Shape& shape,gp_Pnt point){
    BRepClass3d_SolidClassifier classifier(shape,point,1e-7);return classifier.State();
}
static void checkVolume(const V& result,double angleDeg,double k,double removed){
    const V& report=*result.find("report");
    const double theta=angleDeg*std::acos(-1.)/180,t=2,w=30,a=80,b=70,r=4;
    const double allowance=theta*(r+k*t),flatLength=a+allowance+b;
    const double formed=w*t*(a+b+theta*(r+t/2))-removed;
    const double flat=w*t*flatLength-removed;
    const double delta=theta*w*t*t*(.5-k);
    const double tolerance=report.find("volume_tolerance_mm3")->num;
    check(std::abs(report.find("bend_allowance_mm")->num-allowance)<1e-10,"K allowance changed");
    check(std::abs(report.find("flat_length_mm")->num-flatLength)<1e-10,"Flat length changed");
    check(std::abs(report.find("formed_volume_mm3")->num-formed)<tolerance,"Formed volume disagrees with analytic sector");
    check(std::abs(report.find("flat_volume_mm3")->num-flat)<tolerance,"Flat volume disagrees with analytic stock");
    check(std::abs(report.find("volume_delta_mm3")->num-delta)<tolerance,"K-dependent material difference changed");
    check(report.find("geometric_material_conservation")->b==(k==.5),"Midwall conservation flag incorrect");
    check(!report.find("manufacturing_authoritative")->b&&!report.find("source_projection_verified")->b,
          "Designed sheet claimed source or manufacturing authority");
}
int main(){
    const double theta=90*std::acos(-1.)/180;
    const V middle=modelSheetNative(request(90,.5,true));
    const double removed=2*(std::acos(-1.)*3*3+12*8);
    checkVolume(middle,90,.5,removed);
    const V& holes=*middle.find("holes");check(holes.t==V::Arr&&holes.arr.size()==2,"Opening report count changed");
    const auto& rootFlat=holes.arr[0].find("flat_center")->arr;
    const auto& rootFormed=holes.arr[0].find("formed_center")->arr;
    check(rootFlat[0].num==30&&rootFlat[1].num==1&&rootFlat[2].num==15,"Root flat opening moved");
    check(rootFormed[0].num==-50&&rootFormed[1].num==1&&rootFormed[2].num==15,"Root formed opening moved");
    const double allowance=theta*5;
    const auto& childFlat=holes.arr[1].find("flat_center")->arr;
    const auto& childFormed=holes.arr[1].find("formed_center")->arr;
    check(std::abs(childFlat[0].num-(80+allowance+26))<1e-10&&childFlat[2].num==14,
          "Child flat opening ignored bend allowance");
    check(std::abs(childFormed[0].num-5)<1e-10&&std::abs(childFormed[1].num-32)<1e-10&&
          childFormed[2].num==14,"Child formed frame incorrect");
    const TopoDS_Shape formed=readBrep(middle.find("brep_base64")->str);
    const TopoDS_Shape flat=readBrep(middle.find("flat_brep_base64")->str);
    check(state(formed,gp_Pnt(-50,1,15))==TopAbs_OUT&&state(flat,gp_Pnt(30,1,15))==TopAbs_OUT,
          "Root through-hole retained material");
    check(state(formed,gp_Pnt(5,32,14))==TopAbs_OUT&&
          state(flat,gp_Pnt(80+allowance+26,1,14))==TopAbs_OUT,
          "Child polygon through-hole retained material");
    check(state(formed,gp_Pnt(-45,1,15))==TopAbs_IN&&state(flat,gp_Pnt(35,1,15))==TopAbs_IN,
          "Root wall adjacent to hole lost material");
    check(state(formed,gp_Pnt(5,46,14))==TopAbs_IN&&
          state(flat,gp_Pnt(80+allowance+40,1,14))==TopAbs_IN,
          "Child wall adjacent to hole lost material");
    const fs::path temp=fs::temp_directory_path()/"ark-sheet-native-roundtrip";
    fs::create_directories(temp);writeStep(formed,temp/"formed.step");writeStep(flat,temp/"flat.step");
    const double tolerance=middle.find("report")->find("volume_tolerance_mm3")->num;
    check(symmetricDifference(formed,readStep(temp/"formed.step"))<tolerance,"Formed STEP changed sheet material");
    check(symmetricDifference(flat,readStep(temp/"flat.step"))<tolerance,"Flat STEP changed sheet material");
    checkVolume(modelSheetNative(request(60,.3,false)),60,.3,0);
    checkVolume(modelSheetNative(request(110,.7,false)),110,.7,0);
    V invalid=request(90,.5,false);invalid.set("angle",V::makeNum(170));check(rejects(invalid),"Angle boundary accepted");
    invalid=request(90,.5,false);invalid.set("k_factor",V::makeNum(-.1));check(rejects(invalid),"K factor outside domain accepted");
    invalid=request(90,.5,false);invalid.set("k_factor",V::makeNum(std::numeric_limits<double>::quiet_NaN()));
    check(rejects(invalid),"Non-finite K factor accepted");
    invalid=request(90,.5,false);V nearHinge;
    arkjson::parse(R"([{"panel":0,"kind":"circle","x":78,"y":15,"radius":3}])",nearHinge);
    invalid.set("openings",nearHinge);check(rejects(invalid),"Opening cut through finite hinge accepted");
    invalid=request(90,.5,false);V selfCross;
    arkjson::parse(R"([{"panel":1,"kind":"polygon","points":[[20,10],[32,18],[20,18],[32,10]]}])",selfCross);
    invalid.set("openings",selfCross);check(rejects(invalid),"Self-intersecting opening accepted");
    invalid=request(90,.5,true);V overlap=*invalid.find("openings");V extra=overlap.arr[0];extra.set("x",V::makeNum(34));
    overlap.arr.push_back(extra);invalid.set("openings",overlap);check(rejects(invalid),"Overlapping opening envelopes accepted");
    std::cout<<"Native sheet geometry, K-dependent analytic volumes, hole classification and STEP readback passed\n";
}
