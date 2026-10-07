#include "ark/agent.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/suspension.hpp"
#include "ark/serialize.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using arkjson::Value;
namespace {
Value json(const char* text){Value result;if(!arkjson::parse(text,result))throw std::runtime_error("Bad suspension test JSON");return result;}
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
Value accepted(const Value& reply){
    if(reply.find("isError")&&reply.find("isError")->b)
        throw std::runtime_error(reply.find("structuredContent")?reply.find("structuredContent")->dump():"Suspension request failed");
    return *reply.find("structuredContent");
}
double number(const Value& object,const char* key){
    const Value* field=object.find(key);if(!field||field->t!=Value::Num)throw std::runtime_error(std::string("Missing ")+key);
    return field->num;
}
const ark::SuspensionPart& part(const ark::SuspensionPreview& preview,const char* name){
    for(const auto& item:preview.parts)if(item.name==name)return item;
    throw std::runtime_error(std::string("Missing display part: ")+name);
}
void closedDisplay(const ark::SuspensionPreview& preview){
    for(const auto& component:preview.parts){
        const auto audit=ark::analyzeMesh(component.mesh);
        if(!audit.closedOriented())throw std::runtime_error(
            "Open suspension display part "+component.name+": boundary="+std::to_string(audit.boundaryEdges)+
            " degenerate="+std::to_string(audit.degenerateTriangles)+
            " nonmanifold="+std::to_string(audit.nonManifoldEdges)+
            " inconsistent="+std::to_string(audit.inconsistentEdges)+
            " nonfinite="+std::to_string(audit.nonFiniteVertices));
    }
    const auto combined=ark::analyzeMesh(preview.mesh);
    check(combined.closedOriented(),"combined suspension display has no open, invalid or degenerate triangles");
}
void mechanism(){
    ark::AgentSession session;
    accepted(session.call("doc_apply",json(R"({"expected_revision":0,"commands":[
      {"op":"param.set","name":"arm_y","expr":"-650"},
      {"op":"feature.add","type":"suspension","name":"Front left double wishbone","native":{
        "side":"left",
        "hardpoints":{
          "lower_front":[-100,-300,400],"lower_rear":[100,-300,400],"lower_ball":[0,"arm_y",380],
          "upper_front":[-80,-280,650],"upper_rear":[80,-280,650],"upper_ball":[0,-620,650],
          "tie_inner":[-60,-250,520],"tie_outer":[-80,-635,510],
          "wheel_center":[0,-720,520],"wheel_axis":[0,-1,0],
          "shock_chassis":[0,-230,800],"shock_arm":[0,-500,400]
        },
        "wheel":{"radius":300,"width":180,"hub_radius":65},
        "spring":{"rate_n_per_mm":40,"free_length_mm":510,"preload_n":100,
                  "coil_diameter_mm":70,"turns":7,"min_length_mm":350,"max_length_mm":600},
        "limits":{"jounce_min_mm":-35,"jounce_max_mm":35,"rack_min_mm":-12,"rack_max_mm":12}
      }}
    ]})")));
    const auto& feature=session.document.features.front();
    const auto env=session.document.buildEnv();
    const auto nominal=ark::buildSuspensionPreview(feature,env,{});
    closedDisplay(nominal);
    check(nominal.mesh.triCount()>1000&&nominal.parts.size()>=12,"mechanism has detailed named display parts");
    check(part(nominal,"coil_spring").color.x>0.7&&part(nominal,"wheel_tyre").color.x<0.2,
          "spring and tyre have distinct display colors");
    check(number(nominal.report,"closure_residual_mm_max")<1e-5,"nominal rigid links close");
    const auto& baseLinks=*nominal.report.find("link_lengths");
    for(double jounce:{-30.,0.,30.})for(double rack:{-10.,0.,10.}){
        const auto state=ark::buildSuspensionPreview(feature,env,{jounce,rack,70});
        closedDisplay(state);
        check(number(state.report,"closure_residual_mm_max")<1e-5,"stroke/steering pose closes all links");
        const auto& links=*state.report.find("link_lengths");
        for(const char* link:{"lower_front_mm","lower_rear_mm","upper_front_mm","upper_rear_mm","upright_mm","tie_rod_mm"})
            check(std::abs(number(links,link)-number(baseLinks,link))<1e-5,"rigid-link length invariant across stroke");
        check(std::abs(number(state.report,"jounce_mm")-jounce)<1e-8,"reported jounce matches request");
    }
    const auto compressed=ark::buildSuspensionPreview(feature,env,{30,0,0});
    const auto extended=ark::buildSuspensionPreview(feature,env,{-30,0,0});
    check(std::abs(number(*compressed.report.find("spring"),"length_mm")-
                   number(*extended.report.find("spring"),"length_mm"))>5,
          "coilover length visibly changes with jounce");
    check(std::abs(number(compressed.report,"bump_steer_deg"))>1e-4,
          "fixed-rack jounce reports solved bump steer");
    const auto spin=ark::buildSuspensionPreview(feature,env,{0,0,90});
    const auto& marker0=part(nominal,"spin_marker").mesh.pos;
    const auto& marker90=part(spin,"spin_marker").mesh.pos;
    check(marker0.size()==marker90.size()&&std::abs(marker0[0]-marker90[0])+std::abs(marker0[2]-marker90[2])>5,
          "wheel spin rotates a visible marker");
    check(part(nominal,"wheel_tyre").mesh.pos==part(spin,"wheel_tyre").mesh.pos,
          "wheel spin leaves symmetric tyre envelope unchanged");
    const auto stableRevision=session.revision;
    const std::string stableDocument=ark::documentToJson(session.document);
    const Value queried=accepted(session.call("suspension_inspect",json(R"({
      "id":"Front left double wishbone","jounce_mm":20,"rack_mm":8,"wheel_spin_deg":135
    })")));
    check(std::abs(number(queried,"jounce_mm")-20)<1e-9&&number(queried,"closure_residual_mm_max")<1e-5,
          "read-only inspection solves requested suspension pose");
    check(session.revision==stableRevision&&ark::documentToJson(session.document)==stableDocument,
          "suspension inspection preserves document and revision");
    Value saved;check(arkjson::parse(ark::documentToJson(session.document),saved),"suspension saves");
    check(saved.find("version")->num>=18,"suspension selects version 18");
    ark::Document loaded;std::string error;
    check(ark::documentFromJson(saved.dump(),loaded,&error),"suspension reloads");
    loaded.recompute();check(loaded.lastResults.front().ok,"reloaded suspension closes nominal and envelope");

    ark::Feature mirrored=feature;mirrored.name="Front right double wishbone";
    Value spec=mirrored.nativeSpec;spec.set("side",Value::makeStr("right"));
    Value hp=*spec.find("hardpoints");
    for(auto& field:hp.obj)if(field.second.t==Value::Arr&&field.second.arr.size()==3){
        Value y=field.second.arr[1];if(y.t==Value::Num)y.num=-y.num;
        else if(y.t==Value::Str)y.str="-("+y.str+")";
        field.second.arr[1]=y;
    }
    spec.set("hardpoints",hp);mirrored.nativeSpec=spec;
    const auto opposite=ark::buildSuspensionPreview(mirrored,env,{25,-8,0});
    const auto left=ark::buildSuspensionPreview(feature,env,{25,8,0});
    check(std::abs(number(*left.report.find("spring"),"length_mm")-
                   number(*opposite.report.find("spring"),"length_mm"))<1e-5,
          "mirrored suspension keeps spring geometry");
    check(std::abs(left.report.find("wheel")->find("center_mm")->arr[1].num+
                   opposite.report.find("wheel")->find("center_mm")->arr[1].num)<1e-5,
          "mirrored corner wheel centers reflect across centerline");

    const auto revision=session.revision;
    Value badNative=feature.nativeSpec,badHardpoints=*badNative.find("hardpoints");
    badHardpoints.set("tie_outer",json("[-1,-635,510]"));
    badNative.set("hardpoints",badHardpoints);
    Value edit=Value::makeObj(),commands=Value::makeArr(),request=Value::makeObj();
    edit.set("op",Value::makeStr("feature.edit"));edit.set("id",Value::makeStr("Front left double wishbone"));
    edit.set("native",std::move(badNative));commands.arr.push_back(std::move(edit));request.set("commands",std::move(commands));
    const Value invalid=session.call("doc_apply",request);
    check(invalid.find("isError")&&invalid.find("isError")->b&&session.revision==revision,
          "invalid mechanism edit rolls back atomically");
}
}
int main(){try{mechanism();std::cout<<"suspension mechanism tests passed\n";return 0;}
           catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
