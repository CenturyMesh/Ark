#include "ark/electronics.hpp"
#include "ark/agent.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using arkjson::Value;
Value parse(const char* text){Value value;if(!arkjson::parse(text,value))throw std::runtime_error("Bad test JSON");return value;}
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool fails(const ark::Feature& feature){try{(void)ark::electronicsRequest(feature,{});return false;}catch(const std::exception&){return true;}}
const Value& accepted(const Value& reply){const Value* error=reply.find("isError");if(error&&error->b){const Value* result=reply.find("structuredContent");throw std::runtime_error(result&&result->find("error")?result->find("error")->str:"MCP request failed");}const Value* result=reply.find("structuredContent");if(!result)throw std::runtime_error("Missing result");return *result;}
}

int main(){
    try{
        ark::Feature pcb;pcb.type=ark::FeatureType::PCB;
        pcb.nativeSpec=parse(R"({"outline":[[0,0],[40,0],[40,30],[0,30]],"thickness":1.6,
          "layers":[{"name":"top","z":1.6},{"name":"bottom","z":0}],
          "pads":[{"name":"J1.1","net":"PWR","layer":"top","x":10,"y":10,"width":2,"height":2},{"name":"U1.1","net":"PWR","layer":"top","x":25,"y":10,"width":2,"height":2},{"name":"J1.2","net":"GND","layer":"top","x":10,"y":20,"width":2,"height":2}],
          "traces":[{"net":"PWR","layer":"top","width":0.5,"points":[[10,10],[25,10]]}],
          "holes":[{"x":35,"y":25,"diameter":2}],
          "components":[{"ref":"U1","value":"controller","package":"QFN","x":25,"y":10,"width":4,"height":4,"height_mm":1}]
        })");
        Value board=ark::electronicsRequest(pcb,{});check(board.find("report")->find("drc_checked")->b,"PCB DRC checked");check(board.find("report")->find("connectivity_complete")->b,"routed net connectivity checked");check(board.find("report")->find("bom")->arr.size()==1&&board.find("report")->find("bom")->arr[0].find("quantity")->num==1,"component BOM retained");check(board.find("report")->find("nets")->arr.size()==2,"netlist retained");
        ark::Feature shorted=pcb;shorted.nativeSpec=parse(R"({"outline":[[0,0],[40,0],[40,30],[0,30]],"thickness":1.6,"pads":[{"name":"A","net":"A","layer":"top","x":10,"y":10,"width":2,"height":2},{"name":"B","net":"B","layer":"top","x":10.5,"y":10,"width":2,"height":2}]})");check(fails(shorted),"cross-net copper overlap rejects");
        ark::Feature escaped=pcb;escaped.nativeSpec=parse(R"({"outline":[[0,0],[40,0],[40,30],[0,30]],"thickness":1.6,"pads":[{"name":"A","net":"A","layer":"top","x":0.3,"y":10,"width":2,"height":2}]})");check(fails(escaped),"pad outside board rejects");
        ark::Feature open=pcb;open.nativeSpec=parse(R"({"outline":[[0,0],[40,0],[40,30],[0,30]],"thickness":1.6,"pads":[{"name":"A","net":"PWR","layer":"top","x":10,"y":10,"width":2,"height":2},{"name":"B","net":"PWR","layer":"top","x":25,"y":10,"width":2,"height":2}]})");check(!ark::electronicsRequest(open,{}).find("report")->find("connectivity_complete")->b,"incomplete net is reported without blocking editable PCB");

        ark::Feature cable;cable.type=ark::FeatureType::CableRoute;cable.nativeSpec=parse(R"({"start":{"name":"battery","point":[0,0,0]},"end":{"name":"motor","point":[20,0,0]},"radius":0.5,"bend_radius":2,"auto_route":true,"obstacles":[{"name":"frame","min":[8,-1,-1],"max":[12,1,1]}]})");
        Value route=ark::electronicsRequest(cable,{});check(route.find("path")->arr.size()>2&&route.find("report")->find("auto_routed")->b,"bounded auto route avoids obstacle");check(route.find("report")->find("route_length_mm")->num>20,"route length includes detour and bends");
        cable.nativeSpec=parse(R"({"start":[0,0,0],"end":[20,0,0],"radius":0.5,"bend_radius":2,"obstacles":[{"name":"frame","min":[8,-1,-1],"max":[12,1,1]}]})");check(fails(cable),"colliding cable route rejects");

        const auto prefix=(std::filesystem::temp_directory_path()/"ark-electronics-test").string();Value exportResult=ark::exportPcbFabrication(pcb,{},prefix);check(!exportResult.find("fabrication_qualified")->b,"fabrication export stays preview");for(const auto& file:exportResult.find("files")->arr){const auto path=std::filesystem::u8path(file.str);std::ifstream stream(path,std::ios::binary);check(stream.good(),"fabrication preview file opens");const std::string contents((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());stream.close();if(path.extension()==".gbr")check(contents.find("%FSLAX45Y45*%")!=std::string::npos&&contents.find("M02*")!=std::string::npos,"Gerber has coordinate format and footer");else if(path.extension()==".drl")check(contents.find("M48")!=std::string::npos&&contents.find("M30")!=std::string::npos,"Excellon has drill header and footer");else{Value netlist;check(arkjson::parse(contents,netlist)&&!netlist.find("fabrication_qualified")->b,"netlist preview parses and stays unqualified");}std::filesystem::remove(path);}

#ifdef ARK_ELECTRONICS_EXACT_TESTS
        ark::AgentSession session;
        accepted(session.call("doc_apply",parse(R"({"commands":[{"op":"feature.add","type":"pcb","name":"Board","native":{"outline":[[0,0],[40,0],[40,30],[0,30]],"thickness":1.6,"pads":[{"name":"J1","net":"PWR","layer":"top","x":10,"y":10,"width":2,"height":2}]}}]})")));
        Value inspected=accepted(session.call("electronics_inspect",parse(R"({"id":1})")));check(inspected.find("drc_checked")&&inspected.find("drc_checked")->b,"MCP PCB inspection reports DRC");
        const std::string before=ark::documentToJson(session.document);const auto revision=session.revision;
        Value bad=session.call("doc_apply",parse(R"({"commands":[{"op":"feature.edit","id":1,"native":{"outline":[[0,0],[40,0],[40,30],[0,30]],"thickness":1.6,"pads":[{"name":"A","net":"A","layer":"top","x":10,"y":10,"width":2,"height":2},{"name":"B","net":"B","layer":"top","x":10.5,"y":10,"width":2,"height":2}]}}]})"));check(bad.find("isError")->b&&session.revision==revision&&ark::documentToJson(session.document)==before,"invalid PCB edit rolls back atomically");
        accepted(session.call("doc_apply",parse(R"({"commands":[{"op":"feature.add","type":"cable_route","name":"Cable","native":{"start":[0,0,0],"end":[20,0,0],"radius":0.5,"bend_radius":2,"auto_route":true,"obstacles":[{"name":"frame","min":[8,-1,-1],"max":[12,1,1]}]}}]})")));
        inspected=accepted(session.call("electronics_inspect",parse(R"({"id":2})")));check(inspected.find("exact_obstacle_checked")&&inspected.find("exact_obstacle_checked")->b,"exact cable sweep checked against obstacle B-rep");check(inspected.find("geometry")->find("solids")->num==1,"cable route evaluates to one exact solid");
#endif
        std::cout<<"electronics tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
