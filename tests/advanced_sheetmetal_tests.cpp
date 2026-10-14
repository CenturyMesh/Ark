#include "ark/occt_sheet.hpp"
#include "ark/agent.hpp"
#include "ark/sheet_preview.hpp"
#include "../src/occt/common.hpp"
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ark::occt;
namespace {
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
V json(const char* source){V out;check(arkjson::parse(source,out),"test JSON");return out;}
V accepted(const V& response){if(response.find("isError")->b)throw std::runtime_error(response.find("structuredContent")->dump());return *response.find("structuredContent");}
bool rejects(const V& source){try{modelSheetNative(source);return false;}catch(const std::exception&){return true;}}
void proof(const V& result){const auto& r=*result.find("report");check(r.find("brep_valid")->b,"exact formed B-rep valid");check(r.find("flat_valid")->b,"exact flat B-rep valid");check(r.find("flat_solids")->num==1,"flat remains one connected sheet");check(r.find("refold_symmetric_difference_mm3")->num<=r.find("volume_tolerance_mm3")->num,"whole exact solid refold");check(std::abs(r.find("formed_volume_mm3")->num-r.find("analytic_formed_volume_mm3")->num)<=r.find("volume_tolerance_mm3")->num,"independent formed integral");check(std::abs(r.find("flat_volume_mm3")->num-r.find("analytic_flat_volume_mm3")->num)<=r.find("volume_tolerance_mm3")->num,"independent flat integral");check(!r.find("manufacturing_authoritative")->b,"local geometry is not manufacturing qualification");}
V miter(){return json(R"({"width":80,"thickness":2,"k_factor":0.42,"base":{"id":"base","length":120},"miters":[{"id":"rim","parent":"base","edges":["u_max","v_max"],"gap":1,"profile":[{"length":35,"angle":90,"inner_radius":3},{"length":12,"angle":-90,"inner_radius":3}]}]})");}
void miters(){
    for(auto edges:{json(R"(["u_max","v_max"])") ,json(R"(["u_min","v_min"])") ,json(R"(["u_min","u_max","v_min","v_max"])")})for(double sign:{-1.,1.}){
        auto q=miter(),rows=*q.find("miters");rows.arr[0].set("edges",edges);V profile=*rows.arr[0].find("profile");profile.arr[0].set("angle",V::makeNum(90*sign));profile.arr[1].set("angle",V::makeNum(-90*sign));rows.arr[0].set("profile",profile);q.set("miters",rows);
        const auto result=modelSheetNative(q);proof(result);check(result.find("report")->find("miter_count")->num==1,"source miter group count");check(std::abs(result.find("report")->find("advanced_ops")->arr[0].find("measured_min_gap_mm")->num-1)<1e-6,"miter symmetric gap is measured on finite exact lips");check(result.find("panels")->arr.size()==1+edges.arr.size()*2,"common profile generates actual bend panels");
        bool shaped=false;for(auto& panel:result.find("panels")->arr)if(panel.find("id")->str.find(".2")!=std::string::npos){shaped|=std::abs(panel.find("area_mm2")->num-panel.find("length_mm")->num*panel.find("width_mm")->num)>1;}check(shaped,"miter modifies actual lip material");
        const auto flat=readBrep(result.find("flat_brep_base64")->str),formed=readBrep(result.find("brep_base64")->str);check(BRepCheck_Analyzer(flat).IsValid()&&BRepCheck_Analyzer(formed).IsValid(),"miter exact solids");
    }
    auto q=miter(),rows=*q.find("miters");rows.arr[0].set("gap",V::makeNum(0));q.set("miters",rows);check(rejects(q),"miter zero seam gap rejected");q=miter();rows=*q.find("miters");V profile=*rows.arr[0].find("profile");profile.arr[0].set("angle",V::makeNum(70));rows.arr[0].set("profile",profile);q.set("miters",rows);check(rejects(q),"unsupported noncoplanar miter rejected");
}
void contourAndTable(){
    auto q=json(R"({"width":70,"thickness":2,"k_factor":0.5,"base":{"id":"base","length":100},"swept_flanges":[{"id":"channel","parent":"base","edge":"u_max","width":"edge_length-10","offset":5,"path":"straight_edge","profile":[{"length":30,"angle":90,"inner_radius":3},{"length":12,"angle":-60,"inner_radius":4}]}]})");
    auto result=modelSheetNative(q);proof(result);check(result.find("bends")->arr.size()==2,"swept constant section has actual two bends");check(result.find("report")->find("swept_flange_count")->num==1,"swept source retained");
    auto rows=*q.find("swept_flanges");rows.arr[0].set("path",V::makeStr("arc"));q.set("swept_flanges",rows);check(rejects(q),"curved swept flange has explicit unsupported development");
    q=json(R"({"width":60,"thickness":2,"k_factor":0.5,"base":{"id":"base","length":90},"flanges":[{"id":"back","parent":"base","edge":"u_max","length":25,"inner_radius":3,"angle":90}],"bend_table":{"interpolation":"exact","entries":[{"thickness":2,"inner_radius":3,"angle":90,"bend_allowance":5.969026041820607}]}})");
    result=modelSheetNative(q);proof(result);check(result.find("report")->find("bend_table_applied")->b,"bend table influences development");check(std::abs(result.find("bends")->arr[0].find("k_factor")->num-.4)<1e-9,"allowance table derives authored K factor");
    auto table=*q.find("bend_table"),entries=*table.find("entries");entries.arr[0].set("angle",V::makeNum(80));table.set("entries",entries);q.set("bend_table",table);check(rejects(q),"missing exact key cannot extrapolate");
}
void edgeTreatments(){
    auto q=json(R"({"width":80,"thickness":2,"k_factor":0.5,"base":{"id":"base","length":120},"corners":[{"id":"round","kind":"relief","panel":"base","corner":0,"shape":"round","radius":5},{"id":"trim","kind":"trim","panel":"base","corner":1,"width":8,"height":7},{"id":"break","kind":"break","panel":"base","corner":2,"distance":6}],"rips":[{"id":"rip","panel":"base","edge":"v_max","station":30,"width":4,"depth":12}]})");
    auto result=modelSheetNative(q);proof(result);const double volume=(120*80-3.141592653589793*25/4-56-18-48)*2;check(std::abs(result.find("report")->find("formed_volume_mm3")->num-volume)<1e-5,"quarter circle, rectangular trim, chamfer and edge notch exact material removal");
    auto formed=readBrep(result.find("brep_base64")->str);for(auto point:{gp_Pnt(1,1,1),gp_Pnt(119,1,1),gp_Pnt(119,1,79),gp_Pnt(32,1,75)}){BRepClass3d_SolidClassifier state(formed,point,1e-7);check(state.State()==TopAbs_OUT,"edge treatment removes real material");}
    auto rows=*q.find("rips");rows.arr[0].set("depth",V::makeNum(80));q.set("rips",rows);check(rejects(q),"rip cannot silently split a sheet");
    q=json(R"({"width":80,"thickness":2,"k_factor":0.5,"base":{"id":"base","length":120},"flanges":[{"id":"back","parent":"base","edge":"u_max","length":30,"width":70,"offset":5,"inner_radius":3,"angle":90},{"id":"side","parent":"base","edge":"v_max","length":30,"width":110,"offset":5,"inner_radius":3,"angle":90}],"corners":[{"id":"seam","kind":"closure","panel":"back","target":"side","side":"end","gap":1,"from":8}]})");
    result=modelSheetNative(q);proof(result);check(result.find("report")->find("advanced_ops")->arr[0].find("extension_mm")->num>0,"closure adds real material toward target");check(std::abs(result.find("report")->find("advanced_ops")->arr[0].find("measured_gap_mm")->num-1)<1e-6,"closure declared gap binds to finite exact target face");check(!result.find("report")->find("advanced_ops")->arr[0].find("weld_material_added")->b,"closure gap is not fictitious weld material");
}
void tabs(){
    auto q=json(R"({"width":80,"thickness":2,"k_factor":0.5,"base":{"id":"base","length":120},"flanges":[{"id":"back","parent":"base","edge":"u_max","length":30,"width":60,"offset":10,"inner_radius":3,"angle":90}],"tabs":[{"id":"joint","panel":"back","edge":"u_max","station":25,"width":"panel_width/6","length":6,"slot_panel":"base","slot_x":50,"slot_y":40,"clearance":0.3}]})");
    const auto result=modelSheetNative(q);proof(result);check(result.find("report")->find("advanced_ops")->arr.size()==2,"tab and paired slot both authored");const auto& slot=result.find("report")->find("advanced_ops")->arr[0];check(std::abs(slot.find("analytic_volume_delta_mm3")->num+10.6*2.6*2)<1e-7,"paired slot sizes expressions in the tab owner's actual panel context");check(!slot.find("assembly_fit_verified")->b,"declared paired slot does not imply assembled fit");
    auto rows=*q.find("tabs");rows.arr[0].set("clearance",V::makeNum(0));q.set("tabs",rows);check(rejects(q),"zero clearance tab recipe rejected");
}
void persistenceAndAtomicity(){
    ark::AgentSession session;auto request=json(R"({"expected_revision":0,"commands":[{"op":"sheet.create","name":"Advanced","p":{"width":80,"thickness":2,"k_factor":0.42},"base":{"id":"base","length":120}},{"op":"sheet.miter.add","id":1,"miter":{"id":"rim","parent":"base","edges":["u_max","v_max"],"gap":1,"profile":[{"length":35,"angle":90,"inner_radius":3},{"length":12,"angle":-90,"inner_radius":3}]}}]})");
    accepted(session.call("doc_apply",request));const auto revision=session.revision;const auto saved=ark::documentToJson(session.document),source=ark::documentToJson(session.document,true,false);V serialized;check(arkjson::parse(source,serialized)&&serialized.find("version")->num==37,"advanced source requires format 37");
    const auto inspected=accepted(session.call("sheet_inspect",json(R"({"id":1})")));check(inspected.find("miter_count")->num==1&&inspected.find("advanced_ops")->arr[0].find("measured_min_gap_mm"),"compact inspection preserves exact advanced receipts");check(inspected.find("panels")->arr.back().find("outline")&&inspected.find("bends")->arr[0].find("bend_allowance_source"),"compact inspection keeps source shaped contours and allowance authority");
    auto bad=json(R"({"expected_revision":0,"commands":[{"op":"sheet.miter.edit","id":1,"entity":"rim","values":{"gap":2}}]})");check(session.call("doc_apply",bad).find("isError")->b&&session.revision==revision,"stale revision rejects advanced source edits");bad.set("expected_revision",V::makeNum(revision));auto commands=*bad.find("commands");commands.arr[0].set("values",json(R"({"gap":0})"));bad.set("commands",commands);check(session.call("doc_apply",bad).find("isError")->b&&ark::documentToJson(session.document)==saved,"bad geometry rollback preserves source and revision");
    ark::Document restored;std::string error;check(ark::documentFromJson(saved,restored,&error),"format 37 advanced intent restores");restored.recompute();proof(restored.evalBodies.front().nativeData);
    V downgraded=serialized;downgraded.set("version",V::makeNum(36));ark::Document legacy;check(!ark::documentFromJson(downgraded.dump(),legacy,&error),"advanced recipes tagged as old format reject explicitly");
    commands.arr[0].set("values",json(R"({"gap":1.5})"));bad.set("commands",commands);accepted(session.call("doc_apply",bad));check(std::abs(session.document.evalBodies.front().nativeData.find("report")->find("advanced_ops")->arr[0].find("gap_mm")->num-1.5)<1e-9,"edited source regenerates common seam gap");accepted(session.call("doc_undo",V::makeObj()));check(ark::documentToJson(session.document,true,false)==source,"undo restores common miter source");
}
}
int main(){try{std::cout<<"miter\n";miters();std::cout<<"contour/table\n";contourAndTable();std::cout<<"corner/rip\n";edgeTreatments();std::cout<<"tabs\n";tabs();std::cout<<"source/atomic\n";persistenceAndAtomicity();std::cout<<"advanced sheet metal exact geometry and source tests passed\n";}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
