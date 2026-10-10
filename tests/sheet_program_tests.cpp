#include "ark/occt_sheet.hpp"
#include "ark/agent.hpp"
#include "ark/sheet_preview.hpp"
#include "../src/occt/common.hpp"
#include <BRepClass3d_SolidClassifier.hxx>
#include <iostream>
#include <stdexcept>
using namespace ark::occt;
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static V json(const char* s){V v;check(arkjson::parse(s,v),"test JSON");return v;}
static V success(const V& r){if(r.find("isError")->b)throw std::runtime_error(r.find("structuredContent")->dump());return *r.find("structuredContent");}
static bool rejects(const V& q){try{modelSheetNative(q);return false;}catch(const std::exception&){return true;}}
int main(){try{
    V program=json(R"({"width":60,"thickness":2,"k_factor":0.42,"base":{"id":"base","length":80},"flanges":[
      {"id":"back","parent":"base","edge":"u_max","length":25,"width":"edge_length-10","offset":5,"inner_radius":4,"angle":90},
      {"id":"return","parent":"back","edge":"u_max","length":20,"width":"edge_length-10","offset":5,"inner_radius":3,"angle":-45},
      {"id":"side","parent":"base","edge":"v_max","length":15,"width":"edge_length-10","offset":5,"inner_radius":3,"angle":-60}],
      "openings":[{"id":"mount","panel":"base","kind":"circle","x":"panel_length/2","y":"panel_width/2","radius":3},
      {"id":"back_mount","panel":"back","kind":"circle","x":"panel_length-8","y":"panel_width/2","radius":2}]})");
    V result=modelSheetNative(program);auto& report=*result.find("report");check(report.find("panel_count")->num==4,"panel tree count");check(report.find("refold_frame_error_mm")->num<1e-6,"neutral frame proof");check(report.find("refold_symmetric_difference_mm3")->num<report.find("volume_tolerance_mm3")->num,"solid refold proof");
    auto formed=readBrep(result.find("brep_base64")->str),flat=readBrep(result.find("flat_brep_base64")->str);
    for(auto& hole:result.find("holes")->arr)for(auto pose:{std::pair{"formed_center",formed},std::pair{"flat_center",flat}}){auto& c=hole.find(pose.first)->arr;BRepClass3d_SolidClassifier classify(pose.second,gp_Pnt(c[0].num,c[1].num,c[2].num),1e-7);check(classify.State()==TopAbs_OUT,"opening must be actual empty material in both poses");}
    auto preview0=ark::buildSheetPreview(result,0),preview1=ark::buildSheetPreview(result,1),previewHalf=ark::buildSheetPreview(result,.5);
    check(preview0.mesh.triCount()>0&&previewHalf.mesh.triCount()>0,"animated display geometry");
    for(size_t i=0;i<preview0.poses.size();++i){auto& expected=result.find("panels")->arr[i].find("refold_pose")->arr;for(int c=0;c<16;++c){check(std::abs(preview0.poses[i].m[c]-ark::Mat4::identity().m[c])<1e-10,"flat panel pose identity");check(std::abs(preview1.poses[i].m[c]-expected[c].num)<1e-8,"animation endpoint agrees exact refold");}}
    for(const char* edge:{"u_min","u_max","v_min","v_max"})for(double angle:{-110.,-35.,35.,110.}){
        V q=json(R"({"width":40,"thickness":1.5,"k_factor":0.5,"base":{"id":"base","length":60},"flanges":[{"id":"flange","parent":"base","edge":"u_max","length":22,"width":"edge_length-4","offset":2,"inner_radius":3,"angle":90}]})");V fs=*q.find("flanges");fs.arr[0].set("edge",V::makeStr(edge));fs.arr[0].set("angle",V::makeNum(angle));q.set("flanges",fs);auto r=modelSheetNative(q);check(std::abs(r.find("report")->find("volume_delta_mm3")->num)<1e-5,"midwall signed edge conservation");
    }
    V invalid=program,flanges=*program.find("flanges");flanges.arr[0].set("parent",V::makeStr("return"));invalid.set("flanges",flanges);check(rejects(invalid),"cycle/forward dependency rejected");
    invalid=program;flanges=*program.find("flanges");flanges.arr[0].set("width",V::makeNum(1000));invalid.set("flanges",flanges);check(rejects(invalid),"out-of-edge attachment rejected");
    invalid=program;V holes=*program.find("openings");holes.arr[0].set("panel",V::makeStr("missing"));invalid.set("openings",holes);check(rejects(invalid),"orphan opening rejected");
    V clearance=json(R"({"width":60,"thickness":2,"k_factor":0.5,"base":{"id":"base","length":80},"openings":[{"panel":"base","kind":"circle","x":20,"y":20,"radius":3},{"panel":"base","kind":"circle","x":25,"y":25,"radius":3}]})");check(modelSheetNative(clearance).find("holes")->arr.size()==2,"disjoint circles with overlapping envelopes accepted exactly");
    holes=*clearance.find("openings");holes.arr[1].set("x",V::makeNum(23));holes.arr[1].set("y",V::makeNum(20));clearance.set("openings",holes);check(rejects(clearance),"overlapping circles rejected exactly");
    auto temp=fs::temp_directory_path()/"ark-sheet-program.step";writeStep(flat,temp);check(symmetricDifference(flat,readStep(temp))<report.find("volume_tolerance_mm3")->num,"multi-bend flat STEP roundtrip");fs::remove(temp);
    ark::AgentSession session;success(session.call("doc_apply",json(R"({"commands":[{"op":"param.set","name":"length","expr":"80"},{"op":"sheet.create","p":{"width":60,"thickness":2,"k_factor":0.42},"base":{"id":"base","length":"length"}},{"op":"sheet.flange.add","id":1,"flange":{"id":"back","parent":"base","edge":"u_max","length":25,"inner_radius":4,"angle":90}},{"op":"sheet.opening.add","id":1,"opening":{"id":"mount","panel":"back","kind":"circle","x":"panel_length-8","y":"panel_width/2","radius":2}}]})")));
    auto saved=ark::documentToJson(session.document);auto savedAuthoring=ark::documentToJson(session.document,true,false);auto rev=session.revision;auto animation=success(session.call("sheet_preview",json(R"({"id":1,"progress":0.5})")));check(session.revision==rev&&ark::documentToJson(session.document)==saved,"animation must not edit intent");
    auto fail=session.call("doc_apply",json(R"({"commands":[{"op":"sheet.flange.remove","id":1,"entity":"back"}]})"));check(fail.find("isError")->b&&session.revision==rev,"dependent removal atomically rejected");
    success(session.call("doc_apply",json(R"({"commands":[{"op":"param.set","name":"length","expr":"100"},{"op":"sheet.flange.edit","id":1,"entity":"back","values":{"length":35,"angle":-60}}]})")));
    auto& hole=session.document.evalBodies[0].nativeData.find("holes")->arr[0];check(hole.find("id")->str=="mount"&&std::abs(hole.find("local_center")->arr[0].num-27)<1e-10,"opening identity and panel-relative position regenerate");
    success(session.call("doc_undo",V::makeObj()));check(ark::documentToJson(session.document,true,false)==savedAuthoring,"sheet undo preserves authored intent");
    std::cout<<"Sheet panel tree, signed edge metamorphisms, analytic integrals, refold, hole material, STEP and atomic editing passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
