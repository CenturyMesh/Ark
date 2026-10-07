#include "ark/agent.hpp"
#include "ark/occt_sheet.hpp"
#include "ark/sheet_preview.hpp"
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
}
int main(){try{exactFlatDxf();authoredPattern();std::cout<<"sheet completion tests passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
