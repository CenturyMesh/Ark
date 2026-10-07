#include "ark/agent.hpp"
#include "ark/serialize.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using arkjson::Value;
namespace {
Value json(const char* text){Value out;if(!arkjson::parse(text,out))throw std::runtime_error("Bad frame test JSON");return out;}
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
Value accepted(const Value& result){
    if(result.find("isError")&&result.find("isError")->b){
        const Value* payload=result.find("structuredContent");
        throw std::runtime_error(payload?payload->dump():"Frame tool failed");
    }
    const Value* payload=result.find("structuredContent");
    if(!payload)throw std::runtime_error("Frame tool omitted structured content");
    return *payload;
}
double number(const Value& object,const char* key){const Value* found=object.find(key);if(!found||found->t!=Value::Num)throw std::runtime_error(std::string("Missing number ")+key);return found->num;}

void chassisProgram(){
    ark::AgentSession session;
    accepted(session.call("doc_apply",json(R"({"expected_revision":0,"commands":[
      {"op":"param.set","name":"span","expr":"400"},
      {"op":"material.set","name":"Steel","density":7.85},
      {"op":"feature.add","type":"construction_frame","name":"Chassis","native":{
        "profiles":[
          {"name":"round30","section":"round","od":"30","wall":"2","material":"Steel"},
          {"name":"rect40x20","section":"rect","width":"40","height":"20","wall":"2","material":"Steel"}
        ],
        "nodes":[
          {"name":"A","xyz":[0,0,0]},{"name":"B","xyz":["span",0,0]},
          {"name":"C","xyz":["span/2",0,0]},{"name":"D","xyz":["span/2",0,200]},
          {"name":"E","xyz":[0,100,0]},{"name":"F","xyz":["span",100,0]},
          {"name":"G","xyz":["span+160",0,160]},
          {"name":"H","xyz":[110,100,110]}
        ],
        "members":[
          {"name":"rail","profile":"round30","start":"A","end":"B"},
          {"name":"brace","profile":"round30","start":"D","end":"C"},
          {"name":"sill","profile":"rect40x20","start":"E","end":"F","normal":[0,0,1]},
          {"name":"hoop","profile":"round30","start":"B","end":"G","direction":[1,0,0],"normal":[0,0,1],
           "segments":[{"kind":"line","length":"80"},{"kind":"arc","radius":"80","angle":"90"},{"kind":"line","length":"80"}]},
          {"name":"rect_bend","profile":"rect40x20","start":"E","end":"H","direction":[1,0,0],"normal":[0,0,1],
           "segments":[{"kind":"line","length":50},{"kind":"arc","radius":60,"angle":90},{"kind":"line","length":50}]}
        ],
        "joints":[{"kind":"cope","member":"brace","end":"end","against":"rail"}]
      }},
      {"op":"plane.set","name":"RailEnd","support":{"kind":"face","feature":"Chassis","reference":"member.rail.cap.start"}}
    ]})")));
    const Value inspected=accepted(session.call("frame_inspect",json(R"({"id":1})")));
    check(number(inspected,"member_count")==5,"frame inspect lists five members");
    check(inspected.find("mass_checked")&&inspected.find("mass_checked")->b,"frame mass covers every profile");
    check(number(inspected,"mass_g")>0,"frame mass is positive");
    check(inspected.find("joints")&&inspected.find("joints")->arr.size()==1,"one cope joint is reported");
    check(number(inspected.find("joints")->arr[0],"removed_volume_mm3")>0,"cope removed exact material");
    const Value& members=*inspected.find("members");
    check(members.arr.size()==5,"cut list has five rows");
    check(std::abs(number(members.arr[0],"developed_length_mm")-400)<1e-6,"straight cut length follows expression");
    check(number(members.arr[1],"exact_volume_mm3")<number(members.arr[1],"nominal_blank_volume_mm3"),"cope reduces brace volume");
    check(std::abs(number(members.arr[3],"developed_length_mm")-(160+40*std::acos(-1.0)))<1e-5,"bend developed length includes arc");
    check(std::abs(number(members.arr[4],"developed_length_mm")-(100+30*std::acos(-1.0)))<1e-5,"rectangular bend cut length includes arc");
    const Value cap=accepted(session.call("reference_inspect",json(R"({"id":1,"reference":"member.rail.cap.start"})")));
    check(cap.find("world_column_major"),"frame member face is a semantic reference");
    const Value datum=accepted(session.call("plane_inspect",json(R"({"name":"RailEnd"})")));
    check(datum.find("world_column_major"),"member face supports a construction datum");
    const Value native=accepted(session.call("native_inspect",json(R"({"id":1})")));
    check(native.find("report")&&native.find("report")->find("brep_valid")&&native.find("report")->find("brep_valid")->b,
          "frame has a validated exact B-rep");
    check(native.find("mass_checked")&&native.find("mass_checked")->b&&native.find("center_mm"),
          "native inspection exposes member-weighted frame mass and center");
    const auto revision=session.revision;
    const Value rejected=session.call("doc_apply",json(R"({"commands":[
      {"op":"feature.edit","id":"Chassis","native":{"joints":[{"kind":"cope","member":"brace","end":"end","against":"missing"}]}}
    ]})"));
    check(rejected.find("isError")&&rejected.find("isError")->b&&session.revision==revision,"bad frame edit rolls back atomically");
    const double beforeMass=number(inspected,"mass_g");
    accepted(session.call("doc_apply",json(R"({"commands":[{"op":"material.set","name":"Steel","density":8.0}]})")));
    const Value updated=accepted(session.call("frame_inspect",json(R"({"id":1})")));
    check(std::abs(number(updated,"mass_g")-beforeMass*8.0/7.85)<1e-5,
          "profile material density edit refreshes exact member mass");
    check(std::abs(updated.find("center_mm")->arr[0].num-inspected.find("center_mm")->arr[0].num)<1e-6,
          "uniform density edit preserves the mass center");
    accepted(session.call("doc_apply",json(R"({"commands":[
      {"op":"assembly.add","name":"Placed chassis","feature":"Chassis","p":{"tx":10,"ty":20,"tz":30}}
    ]})")));
    const Value assembly=accepted(session.call("assembly_info",Value::makeObj()));
    check(assembly.find("mass_checked")&&assembly.find("mass_checked")->b,
          "placed frame has complete assembly mass");
    check(std::abs(number(assembly,"mass_g")-number(updated,"mass_g"))<1e-5,
          "one placed frame keeps its exact member mass");
    const auto& localCenter=updated.find("center_mm")->arr;
    const auto& worldCenter=assembly.find("center_mm")->arr;
    for(int axis=0;axis<3;++axis)
        check(std::abs(worldCenter[axis].num-localCenter[axis].num-(axis==0?10:axis==1?20:30))<1e-5,
              "assembly center applies the authored frame placement");
    std::string encoded=ark::documentToJson(session.document),error;
    ark::Document restored;
    check(ark::documentFromJson(encoded,restored,&error),"frame document saves and reloads");
    restored.recompute();
    check(restored.lastResults.front().ok,"reloaded frame recomputes");
}
}
int main(){try{chassisProgram();std::cout<<"construction frame tests passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
