#include "ark/agent.hpp"
#include "ark/occt_sheet.hpp"
#include "ark/sheet_preview.hpp"
#include <BRepClass3d_SolidClassifier.hxx>
#include "../src/occt/common.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using arkjson::Value;
namespace fs=std::filesystem;
namespace {
Value json(const char* text){Value out;if(!arkjson::parse(text,out))throw std::runtime_error("Invalid sheet test JSON");return out;}
void check(bool valid,const char* message){if(!valid)throw std::runtime_error(message);}
Value accepted(const Value& response){
    if(response.find("isError")&&response.find("isError")->b)
        throw std::runtime_error(response.find("structuredContent")?response.find("structuredContent")->dump():"Sheet command failed");
    return *response.find("structuredContent");
}
bool rejected(const Value& request){try{(void)ark::occt::exportSheetFlatDxfNative(request);return false;}catch(const std::exception&){return true;}}
void exactFlatDxf(){
    Value program=json(R"({"width":100,"thickness":2,"k_factor":0.5,"base":{"id":"floor","length":200},
      "flanges":[
        {"id":"left","parent":"floor","edge":"u_max","length":35,"width":80,"offset":10,"inner_radius":4,"angle":90},
        {"id":"side","parent":"floor","edge":"v_max","length":20,"width":100,"offset":50,"inner_radius":3,"angle":-55}
      ],"openings":[
        {"id":"seat_slot","panel":"floor","kind":"slot","x":45,"y":27,"length":28,"width":6,"angle":25},
        {"id":"window","panel":"floor","kind":"polygon","points":[[100,20],[130,20],[130,45],[100,45]]},
        {"id":"service_window","panel":"floor","kind":"window","x":165,"y":60,"width":18,"height":12,"angle":15},
        {"id":"flange_hole","panel":"left","kind":"circle","x":18,"y":40,"radius":3}
      ]})");
    const Value model=ark::occt::modelSheetNative(program);
    check(model.find("report")->find("flat_valid")->b,"authored graph has exact flat solid");
    check(model.find("flat_brep_sha256"),"flat B-rep has a source digest");
    check(model.find("holes")->arr.size()==4,"slot, polygon, window and circle exist in flat and formed results");
    const Value compact=ark::inspectSheetNativeData(model);
    check(compact.find("bends")->arr.size()==2&&compact.find("openings")->arr.size()==4,"compact sheet inspection has bend and opening tables");
    check(compact.find("bends")->arr[0].find("bend_line_flat_mm")&&compact.find("bends")->arr[1].find("direction")->str=="negative","signed bend lines are inspectable");
    check(!compact.find("brep_base64")&&!compact.find("mesh"),"inspection omits large B-rep and mesh payloads");
    const auto file=fs::temp_directory_path()/"ark-sheet-completion.dxf";
    Value exportRequest=Value::makeObj();exportRequest.set("package",model);exportRequest.set("path",Value::makeStr(file.string()));
    const Value receipt=ark::occt::exportSheetFlatDxfNative(exportRequest);
    const std::string bytes=ark::occt::readFile(file);
    check(receipt.find("sha256")->str==ark::occt::sha256(bytes),"DXF receipt hashes published bytes");
    check(receipt.find("analytic_contours")->b&&!receipt.find("manufacturing_authoritative")->b,"DXF reports analytic preview boundary");
    check(receipt.find("arc_count")->num>=2&&receipt.find("circle_count")->num>=1,"slot arcs and round opening survive DXF extraction");
    const auto& contour=receipt.find("contour_bounds_mm")->arr;
    const auto& flatBounds=model.find("report")->find("flat_bounds_mm")->arr;
    check(std::abs(contour[0].num-flatBounds[0].num)<1e-3&&std::abs(contour[3].num-flatBounds[5].num)<1e-3,
          "DXF contour extents rebind to the exact flat B-rep");
    check(receipt.find("bend_line_count")->num==2&&bytes.find("BEND_DOWN")!=std::string::npos,"DXF carries both signed bend lines");
    check(bytes.find("$INSUNITS\n70\n4")!=std::string::npos&&bytes.find("0\nEOF\n")!=std::string::npos,"DXF states millimetres and terminates");
    Value altered=model;altered.set("flat_brep_sha256",Value::makeStr("changed"));exportRequest.set("package",altered);
    check(rejected(exportRequest),"DXF rejects stale flat B-rep digest");
    exportRequest.set("package",model);exportRequest.set("path",Value::makeStr((file.string()+".step")));
    check(rejected(exportRequest),"DXF cannot be mislabeled as STEP");
    fs::remove(file);
}
void authoredPattern(){
    ark::AgentSession session;
    Value created=accepted(session.call("doc_apply",json(R"({"commands":[
      {"op":"param.set","name":"pitch","expr":"30"},
      {"op":"sheet.create","name":"Floor","p":{"width":90,"thickness":2,"k_factor":0.5},"base":{"id":"floor","length":180}},
      {"op":"sheet.flange.add","id":1,"flange":{"parent":"floor","edge":"u_max","length":30,"inner_radius":4,"angle":-65}},
      {"op":"sheet.opening.pattern","id":"Floor","opening":{"id":"fasteners","panel":"floor","kind":"slot","x":35,"y":40,"length":18,"width":5},"count":3,"dx":"pitch","dy":0}
    ]})")));
    const auto& edits=created.find("sheet_edits")->arr;
    check(edits.size()==3&&edits[0].find("entity_id")->str=="floor","sheet create returns base identity");
    check(edits[1].find("entity_id")&&edits[2].find("entity_ids")->arr.size()==3,"flange and pattern return named identities");
    check(edits[2].find("entity_ids")->arr[0].str=="fasteners.1"&&edits[2].find("entity_ids")->arr[2].str=="fasteners.3","pattern names are deterministic");
    const Value inspected=accepted(session.call("sheet_inspect",json(R"({"id":1})")));
    check(inspected.find("opening_count")->num==3&&inspected.find("bends")->arr.size()==1,"pattern recomputed as exact panel cuts");
    const auto previous=session.revision;
    Value invalid=session.call("doc_apply",json(R"({"commands":[
      {"op":"sheet.opening.pattern","id":1,"opening":{"id":"overlap","panel":"floor","kind":"circle","x":40,"y":40,"radius":4},"count":3,"dx":2,"dy":0}
    ]})"));
    check(invalid.find("isError")->b&&session.revision==previous,"overlapping hole pattern rejects atomically");
    accepted(session.call("doc_apply",json(R"({"commands":[{"op":"param.set","name":"pitch","expr":"34"}]})")));
    const Value moved=accepted(session.call("sheet_inspect",json(R"({"id":1})")));
    const auto& holes=moved.find("openings")->arr;
    check(std::abs(holes[2].find("local_center")->arr[0].num-103)<1e-6,"pattern spacing remains expression-driven");
}
void hemsAndStamps(){
    auto fails=[](const Value& q){try{ark::occt::modelSheetNative(q);return false;}catch(const std::exception&){return true;}};
    for(const char* edge:{"u_min","u_max","v_min","v_max"})for(double direction:{-1.,1.}){
        Value q=json(R"({"width":70,"thickness":2,"k_factor":0.5,"base":{"id":"base","length":100},"hems":[{"id":"rim","parent":"base","edge":"u_max","length":12,"inner_radius":3,"direction":1,"width":40,"offset":10}]})");
        Value hems=*q.find("hems");hems.arr[0].set("edge",Value::makeStr(edge));hems.arr[0].set("direction",Value::makeNum(direction));q.set("hems",hems);
        const auto result=ark::occt::modelSheetNative(q);const auto& report=*result.find("report");
        check(report.find("hem_count")->num==1&&report.find("flat_pattern_qualified")->b,"open round hem keeps developed flat qualification");
        check(std::abs(report.find("volume_delta_mm3")->num)<1e-5&&report.find("refold_symmetric_difference_mm3")->num<report.find("volume_tolerance_mm3")->num,"signed 180 degree hem independently refolds");
        check(result.find("bends")->arr[0].find("kind")->str=="open_round_hem","hem kind retained in bend inventory");
        const auto preview=ark::buildSheetPreview(result,1);check(preview.poses.size()==2,"hem preview endpoint exists");
        hems.arr[0].set("kind",Value::makeStr("closed"));q.set("hems",hems);check(fails(q),"unsupported closed hem never silently becomes open round");hems.arr[0].set("kind",Value::makeStr("open_round"));hems.arr[0].set("inner_radius",Value::makeNum(0));q.set("hems",hems);check(fails(q),"zero-radius closed hem rejected");
    }
    for(double depth:{-5.,5.}){
        Value q=json(R"({"width":90,"thickness":2,"k_factor":0.5,"base":{"id":"base","length":120},"stamps":[{"id":"boss","panel":"base","kind":"circular_emboss","x":60,"y":45,"radius":15,"top_radius":8,"depth":5}]})");
        Value stamps=*q.find("stamps");stamps.arr[0].set("depth",Value::makeNum(depth));q.set("stamps",stamps);
        const auto result=ark::occt::modelSheetNative(q);const auto& report=*result.find("report");
        check(report.find("stamp_count")->num==1&&!report.find("flat_pattern_qualified")->b&&!report.find("geometric_material_conservation")->b,"stamp has unqualified nominal blank and no material conservation claim");
        check(report.find("refold_symmetric_difference_mm3")->isNull()&&report.find("rigid_panel_refold_verified")->b,"stamp does not claim whole-sheet unfolding/refolding");
        const auto shape=ark::occt::readBrep(result.find("brep_base64")->str);
        auto state=[&](gp_Pnt p){BRepClass3d_SolidClassifier c(shape,p,1e-7);return c.State();};
        check(state(gp_Pnt(60,depth+1,45))==TopAbs_IN&&state(gp_Pnt(60,1,45))==TopAbs_OUT,"emboss plateau is real displaced material with empty old location");
        const double m=-depth/7,n=std::sqrt(1+m*m),radial=11.5,mid=1+depth/2;
        for(double offset:{-.9,.9})check(state(gp_Pnt(60+radial-offset*m/n,mid+offset/n,45))==TopAbs_IN,"conical skins contain full normal wall thickness");
        for(double offset:{-1.1,1.1})check(state(gp_Pnt(60+radial-offset*m/n,mid+offset/n,45))==TopAbs_OUT,"conical wall normal thickness is bounded exactly");
        auto formedPreview=ark::buildSheetPreview(result,1);auto flatPreview=ark::buildSheetPreview(result,0);check(formedPreview.mesh.triCount()>0&&flatPreview.mesh.triCount()>0,"stamped preview exact endpoints");
        bool intermediate=false;try{ark::buildSheetPreview(result,.5);}catch(const std::exception&){intermediate=true;}check(intermediate,"stamping strain animation refused");
        Value request=Value::makeObj();request.set("package",result);request.set("path",Value::makeStr((fs::temp_directory_path()/"ark-unqualified-stamp.dxf").string()));check(rejected(request),"unqualified stamped blank cannot export DXF");
        const auto file=fs::temp_directory_path()/"ark-stamp-formed.step";ark::occt::writeStep(shape,file);check(ark::occt::symmetricDifference(shape,ark::occt::readStep(file))<report.find("volume_tolerance_mm3")->num,"stamped exact formed STEP roundtrip");fs::remove(file);
        stamps.arr[0].set("x",Value::makeNum(10));q.set("stamps",stamps);check(fails(q),"stamp at edge rejected");
    }
    Value flangeStamp=json(R"({"width":90,"thickness":2,"k_factor":0.42,"base":{"id":"base","length":120},"flanges":[{"id":"back","parent":"base","edge":"u_max","length":60,"inner_radius":4,"angle":90}],"hems":[{"id":"rim","parent":"back","edge":"u_max","length":12,"inner_radius":3,"direction":-1}],"stamps":[{"id":"boss","panel":"back","kind":"circular_emboss","x":30,"y":45,"radius":12,"top_radius":6,"depth":4}]})");
    check(ark::occt::modelSheetNative(flangeStamp).find("stamps")->arr.size()==1,"stamp on rotated flange and negative hem accepted");
    Value conflict=flangeStamp;conflict.set("openings",json(R"([{"id":"hole","panel":"back","kind":"circle","x":30,"y":45,"radius":3}])"));check(fails(conflict),"stamp footprint/opening conflict rejected");
    Value stamps=*flangeStamp.find("stamps");stamps.arr.push_back(stamps.arr[0]);stamps.arr[1].set("id",Value::makeStr("duplicate_footprint"));conflict=flangeStamp;conflict.set("stamps",stamps);check(fails(conflict),"touching stamp footprints rejected");
    ark::AgentSession cascade;
    accepted(cascade.call("doc_apply",json(R"({"commands":[{"op":"sheet.create","p":{"width":90,"thickness":2,"k_factor":0.5},"base":{"id":"base","length":120}},{"op":"sheet.flange.add","id":1,"flange":{"id":"back","parent":"base","edge":"u_max","length":60,"inner_radius":4,"angle":90}},{"op":"sheet.hem.add","id":1,"hem":{"id":"rim","parent":"back","edge":"u_max","length":10,"inner_radius":3}},{"op":"sheet.stamp.add","id":1,"stamp":{"id":"boss","panel":"back","kind":"circular_emboss","x":30,"y":45,"radius":12,"top_radius":6,"depth":4}}]})")));
    const auto cascadeRevision=cascade.revision;check(cascade.call("doc_apply",json(R"({"commands":[{"op":"sheet.flange.remove","id":1,"entity":"back"}]})")).find("isError")->b&&cascade.revision==cascadeRevision,"flange with hem and stamp requires explicit cascade");
    accepted(cascade.call("doc_apply",json(R"({"commands":[{"op":"sheet.flange.remove","id":1,"entity":"back","cascade":true}]})")));check(cascade.document.features[0].nativeSpec.find("hems")->arr.empty()&&cascade.document.features[0].nativeSpec.find("stamps")->arr.empty(),"cascade removes dependent hem and stamp intent");
    ark::AgentSession session;
    accepted(session.call("doc_apply",json(R"({"commands":[{"op":"sheet.create","name":"HemStamp","p":{"width":90,"thickness":2,"k_factor":0.5},"base":{"id":"base","length":120}},{"op":"sheet.hem.add","id":1,"hem":{"parent":"base","edge":"u_max","length":10,"inner_radius":3}},{"op":"sheet.stamp.add","id":1,"stamp":{"panel":"base","kind":"circular_emboss","x":"panel_length/2","y":"panel_width/2","radius":15,"top_radius":8,"depth":5}}]})")));
    auto saved=ark::documentToJson(session.document),savedAuthoring=ark::documentToJson(session.document,true,false);Value serialized;check(arkjson::parse(savedAuthoring,serialized)&&serialized.find("version")->num==21,"hem/stamp intent saves version 21");
    ark::Document restored;std::string error;check(ark::documentFromJson(saved,restored,&error),"hem/stamp persistence load");restored.recompute();check(restored.evalBodies.size()==1&&restored.evalBodies[0].nativeData.find("stamps")->arr.size()==1,"hem/stamp persistence reevaluates natively");
    check(std::abs(restored.evalBodies[0].nativeData.find("stamps")->arr[0].find("local_center")->arr[0].num-60)<1e-6,"loaded stamp expression retains panel environment");
    Value newBase=*restored.features[0].nativeSpec.find("base");newBase.set("length",Value::makeNum(140));restored.features[0].nativeSpec.set("base",newBase);restored.recompute();check(std::abs(restored.evalBodies[0].nativeData.find("stamps")->arr[0].find("local_center")->arr[0].num-70)<1e-6,"loaded stamp expression regenerates after panel dimension edit");
    const auto flatStep=fs::temp_directory_path()/"ark-stamped-refused.step";Value exportArgs=Value::makeObj();exportArgs.set("id",Value::makeNum(1));exportArgs.set("path",Value::makeStr(flatStep.string()));exportArgs.set("pose",Value::makeStr("flat"));check(session.call("export_step",exportArgs).find("isError")->b,"agent STEP export refuses unstamped nominal reference");
    const auto& native=session.document.features[0].nativeSpec;const auto hemId=native.find("hems")->arr[0].find("id")->str,stampId=native.find("stamps")->arr[0].find("id")->str;
    check(hemId!=stampId,"hem and stamp share collision-free identity allocator");
    Value bad=json(R"({"commands":[{"op":"sheet.stamp.edit","id":1,"entity":"placeholder","values":{"top_radius":1}}]})");Value cmds=*bad.find("commands");cmds.arr[0].set("entity",Value::makeStr(stampId));bad.set("commands",cmds);const auto revision=session.revision;
    check(session.call("doc_apply",bad).find("isError")->b&&session.revision==revision&&ark::documentToJson(session.document)==saved,"invalid stamp edit rolls back intent and revision");
    cmds.arr[0].set("values",json(R"({"depth":-6})"));bad.set("commands",cmds);accepted(session.call("doc_apply",bad));check(session.document.evalBodies[0].nativeData.find("stamps")->arr[0].find("id")->str==stampId,"stamp edit keeps identity");
    accepted(session.call("doc_undo",Value::makeObj()));check(ark::documentToJson(session.document,true,false)==savedAuthoring,"stamp undo restores exact intent");
    Value remove=json(R"({"commands":[{"op":"sheet.stamp.remove","id":1,"entity":"placeholder"},{"op":"sheet.hem.remove","id":1,"entity":"placeholder"}]})");cmds=*remove.find("commands");cmds.arr[0].set("entity",Value::makeStr(stampId));cmds.arr[1].set("entity",Value::makeStr(hemId));remove.set("commands",cmds);accepted(session.call("doc_apply",remove));check(session.document.evalBodies[0].nativeData.find("report")->find("stamp_count")->num==0,"hem/stamp removal recomputes plain panel");
}

}
int main(){try{exactFlatDxf();authoredPattern();hemsAndStamps();std::cout<<"sheet completion tests passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
