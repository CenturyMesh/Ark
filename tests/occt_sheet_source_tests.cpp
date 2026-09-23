#include "ark/occt_sheet.hpp"
#include "../src/occt/common.hpp"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax1.hxx>
#include <Standard_Failure.hxx>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace ark::occt;
namespace {
int checks=0;void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
V json(const char* text){V v;if(!arkjson::parse(text,v))throw std::runtime_error("Bad test JSON");return v;}
template<class F>void reject(F f,const char* message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,message);}
std::string utf8(const fs::path& p){auto s=p.u8string();return {s.begin(),s.end()};}
struct Temp{fs::path dir;Temp(){dir=fs::temp_directory_path()/("ark-native-sheet-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));if(!fs::create_directory(dir))throw std::runtime_error("Test directory already exists");}~Temp(){std::error_code ec;fs::remove_all(dir,ec);}};
}
int main(){try{
    Temp temp;check(sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 known answer");
    auto program=json(R"({"width":60,"leg_a":80,"leg_b":50,"thickness":2,"inner_radius":5,"angle":90,"k_factor":0.5,"openings":[{"panel":0,"kind":"circle","x":25,"y":20,"radius":4},{"panel":1,"kind":"polygon","points":[[15,20],[25,20],[25,30],[15,30]]}]})");
    auto designed=modelSheetNative(program);auto source=readBrep(designed.find("brep_base64")->str);auto sourcePath=temp.dir/"formed.step";writeStep(source,sourcePath);
    V request=V::makeObj();request.set("source_path",V::makeStr(utf8(sourcePath)));request.set("k_factor",V::makeNum(.5));auto imported=importSheetNative(request);auto report=*imported.find("report");
    check(report.find("source_projection_verified")->b,"Source sheet projection verified");check(report.find("bend_count")->num==1&&report.find("opening_count")->num==2,"One physical bend and two exact openings");
    check(std::abs(report.find("thickness_mm")->num-2)<1e-6,"Paired-skin and panel thickness agree");
    check(report.find("refold_symmetric_difference_volume_mm3")->num<=report.find("volume_tolerance_mm3")->num,"Independent full-source refold");
    auto flat=readBrep(imported.find("flat_brep_base64")->str);check(std::abs(properties(flat)-properties(source))<1e-5,"Mid-wall development preserves volume");
    check(std::abs(report.find("flat_length_mm")->num-(130+6*std::acos(-1.)/2))<1e-6,"Source-derived bend allowance");
    auto output=temp.dir/"flat.step";V exportRequest=V::makeObj();exportRequest.set("package",imported);exportRequest.set("path",V::makeStr(utf8(output)));exportRequest.set("pose",V::makeStr("flat"));auto receipt=exportSheetNative(exportRequest);
    check(symmetricDifference(flat,readStep(output))<1e-5,"Exported source sheet matches native flat B-rep");check(receipt.find("step_sha256")->str==sha256(readFile(output)),"Export receipt binds STEP bytes");
    exportRequest.set("path",V::makeStr(utf8(temp.dir/"wrong.dxf")));reject([&]{exportSheetNative(exportRequest);},"STEP cannot be mislabeled as a different exchange format");exportRequest.set("path",V::makeStr(utf8(output)));
    auto changed=imported;changed.set("flat_brep_sha256",V::makeStr("changed"));exportRequest.set("package",changed);reject([&]{exportSheetNative(exportRequest);},"Changed source projection rejected");exportRequest.set("package",imported);
    request.set("k_factor",V::makeNum(.3));auto calibrated=importSheetNative(request);auto cr=*calibrated.find("report");
    check(!cr.find("geometric_material_conservation")->b,"Non-midwall K never claims volume conservation");
    check(std::abs((cr.find("formed_volume_mm3")->num-cr.find("flat_volume_mm3")->num)-(.2*60*4*std::acos(-1.)/2))<1e-5,"K-dependent volume change explicitly agrees with analytical model");
    // Rigid transformation changes neither intrinsic allowance nor hole placement
    // in the source-owned flat frame selected by unique larger flange volume.
    gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.73);gp_Trsf move;move.SetTranslation(gp_Vec(13,-41,27));auto pose=move*rotation;
    auto rotatedPath=temp.dir/"rotated.step";writeStep(transformed(source,pose),rotatedPath);request.set("source_path",V::makeStr(utf8(rotatedPath)));request.set("k_factor",V::makeNum(.5));auto rotated=importSheetNative(request);
    check(std::abs(rotated.find("report")->find("flat_length_mm")->num-report.find("flat_length_mm")->num)<1e-6,"Rigid source transform preserves developed length");
    auto& a=imported.find("holes")->arr;auto& b=rotated.find("holes")->arr;check(a.size()==b.size(),"Rigid transform preserves opening count");
    for(size_t i=0;i<a.size();++i)for(int axis=0;axis<3;++axis)check(std::abs(a[i].find("flat_center")->arr[axis].num-b[i].find("flat_center")->arr[axis].num)<1e-6,"Rigid transform preserves projected opening positions");
    // Independent planar source, without the sheet-model constructor.
    auto plate=BRepPrimAPI_MakeBox(80,2,60).Shape();plate=cut(plate,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20,-1,30),gp_Dir(0,1,0)),4,4).Shape());auto platePath=temp.dir/"flat-source.step";writeStep(plate,platePath);
    request.set("source_path",V::makeStr(utf8(platePath)));auto planar=importSheetNative(request);check(planar.find("report")->find("bend_count")->num==0&&planar.find("report")->find("opening_count")->num==1,"Flat sheet source and its actual hole supported");
    auto twoPath=temp.dir/"two-bodies.step";writeStep(compound({source,plate}),twoPath);request.set("source_path",V::makeStr(utf8(twoPath)));reject([&]{importSheetNative(request);},"Multiple solids require explicit body selection");request.set("body_index",V::makeNum(0));check(importSheetNative(request).find("report")->find("body_index")->num==0,"Explicit STEP body selection");
    request=json(R"({"k_factor":0.5})");auto damagedPath=temp.dir/"bend-hole.step";double root2=std::sqrt(.5);gp_Pnt center(6*root2,7-6*root2,30);gp_Dir radial(root2,-root2,0);auto start=center.Translated(gp_Vec(radial)*(-3));auto damaged=cut(source,BRepPrimAPI_MakeCylinder(gp_Ax2(start,radial),2,6).Shape());writeStep(damaged,damagedPath);request.set("source_path",V::makeStr(utf8(damagedPath)));reject([&]{importSheetNative(request);},"Cut through deforming bend is rejected");
    writeFile(sourcePath,readFile(sourcePath)+"\nchanged");reject([&]{exportSheetNative(exportRequest);},"Changed original source prevents stale export");
    std::cout<<checks<<" native source sheet checks passed\n";return 0;
}catch(const Standard_Failure& e){std::cerr<<"OCCT: "<<e.GetMessageString()<<'\n';return 1;}catch(const std::exception& e){std::cerr<<"After "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
