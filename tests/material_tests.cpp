#include "ark/agent.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using arkjson::Value;
Value json(const char* source){Value value;if(!arkjson::parse(source,value))throw std::runtime_error("Bad test JSON");return value;}
const Value& accepted(const Value& reply){
    if(const Value* error=reply.find("isError");error&&error->b){const Value* body=reply.find("structuredContent");throw std::runtime_error(body&&body->find("error")?body->find("error")->str:"MCP request failed");}
    const Value* body=reply.find("structuredContent");if(!body)throw std::runtime_error("Missing result");return *body;
}
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool close(double a,double b,double eps=1e-6){return std::abs(a-b)<=eps;}
}

int main(){
    try{
        ark::AgentSession session;
        accepted(session.call("doc_apply",json(R"({"expected_revision":0,"commands":[
          {"op":"material.set","name":"Aluminium","density":2.7,"appearance":{"base_color":[0.25,0.42,0.7],"metallic":0.8,"roughness":0.3,"texture_path":"textures/brushed.png","texture_scale":[2,3]}},
          {"op":"feature.add","type":"box","name":"Cube","p":{"dx":10,"dy":10,"dz":10},"material":"Aluminium","appearance":{"opacity":0.6}},
          {"op":"assembly.add","name":"Row","feature":"Cube","pattern":{"type":"linear","count":2,"dx":10}}
        ]})")));
        check(session.revision==1,"material batch revision");
        const auto& feature=session.document.features.front();
        const ark::Appearance appearance=ark::resolvedAppearance(session.document,feature);
        check(close(appearance.baseColor.x,0.25)&&close(appearance.metallic,0.8)&&close(appearance.opacity,0.6),"material and feature appearance merge");
        check(appearance.texturePath=="textures/brushed.png"&&close(appearance.textureScaleV,3),"texture metadata preserved");

        Value package=json(ark::documentToJson(session.document,true,false).c_str());
        check(package.find("version")->num==15,"material document version");
        ark::Document loaded;std::string error;
        check(ark::documentFromJson(ark::documentToJson(session.document),loaded,&error),"material round trip");
        check(ark::documentToJson(loaded)==ark::documentToJson(session.document),"material serialization stable");
        check(ark::resolvedAppearance(loaded,loaded.features.front()).texturePath=="textures/brushed.png","appearance restored");
        const std::string loadedBefore=ark::documentToJson(loaded);
        check(!ark::documentFromJson(R"({"format":"ark","version":15,"units":"mm","features":[],"materials":[{"name":"Invalid","density":-2}]})",loaded,&error)&&ark::documentToJson(loaded)==loadedBefore,"invalid material package leaves document intact");

        const auto priorResults=session.document.lastResults.size();
        const auto priorBody=session.document.evalBodies.front().id;
        Value paint=accepted(session.call("doc_apply",json(R"({"commands":[{"op":"material.set","name":"Aluminium","appearance":{"roughness":0.4}}]})")));
        check(paint.find("geometry_recomputed")&&!paint.find("geometry_recomputed")->b,"appearance only batch skips geometry recompute");
        check(session.document.lastResults.size()==priorResults&&session.document.evalBodies.front().id==priorBody,"evaluated geometry remains available");

        const std::string before=ark::documentToJson(session.document);const auto revision=session.revision;
        auto rejected=session.call("doc_apply",json(R"({"commands":[{"op":"material.remove","name":"Aluminium"}]})"));
        check(rejected.find("isError")->b&&session.revision==revision&&ark::documentToJson(session.document)==before,"referenced material removal rolls back");
        rejected=session.call("doc_apply",json(R"({"commands":[{"op":"material.set","name":"Aluminium","appearance":{"roughness":1.2}}]})"));
        check(rejected.find("isError")->b&&session.revision==revision&&ark::documentToJson(session.document)==before,"invalid appearance rolls back");
        rejected=session.call("doc_apply",json(R"({"commands":[{"op":"material.set","name":"Aluminium","density":-1}]})"));
        check(rejected.find("isError")->b&&session.revision==revision&&ark::documentToJson(session.document)==before,"invalid material density rolls back");
        rejected=session.call("doc_apply",json(R"({"commands":[{"op":"feature.edit","id":"Cube","material":"Missing"}]})"));
        check(rejected.find("isError")->b&&session.revision==revision&&ark::documentToJson(session.document)==before,"missing material reference rolls back");
        Value design=accepted(session.call("doc_info",json(R"({"detail":"design"})")));
        check(design.find("materials")&&design.find("materials")->arr.size()==1,"design inventory includes named material catalog");
        check(design.find("features")->arr[0].find("appearance")&&close(design.find("features")->arr[0].find("appearance")->find("roughness")->num,0.4),"design inventory resolves feature appearance");
        check(close(design.find("features")->arr[0].find("density_g_cm3")->num,2.7),"design inventory resolves material density");

#ifdef ARK_MATERIAL_EXACT_TESTS
        Value native=accepted(session.call("native_inspect",json(R"({"id":1,"gravity_m_s2":10})")));
        const std::string exactBefore=session.document.evalBodies.front().nativeData.dump();
        paint=accepted(session.call("doc_apply",json(R"({"commands":[{"op":"feature.edit","id":"Cube","appearance":{"opacity":0.7}}]})")));
        check(!paint.find("geometry_recomputed")->b&&session.document.evalBodies.front().nativeData.dump()==exactBefore,"material edit retains exact B-rep cache");
        check(native.find("mass_checked")->b&&close(native.find("mass_g")->num,2.7),"exact native mass from material density");
        check(close(native.find("weight_N")->num,0.027)&&close(native.find("gravity_m_s2")->num,10),"explicit gravity and weight");
        check(close(native.find("center_mm")->arr[0].num,5),"exact native center");

        Value assembly=accepted(session.call("assembly_info",json(R"({"gravity_m_s2":9.80665})")));
        check(assembly.find("mass_checked")->b&&close(assembly.find("mass_g")->num,5.4),"pattern mass includes copies");
        check(close(assembly.find("center_mm")->arr[0].num,10),"pattern center uses world transforms");
        check(close(assembly.find("weight_N")->num,0.0054*9.80665),"assembly weight uses specified gravity");

        accepted(session.call("doc_apply",json(R"({"commands":[{"op":"assembly.edit","id":"Row","visible":false}]})")));
        assembly=accepted(session.call("assembly_info",Value::makeObj()));
        check(assembly.find("mass_scope")->str=="visible_placed_parts","assembly reports visibility scope");
        check(!assembly.find("mass_checked")->b&&!assembly.find("mass_g"),"hidden parts do not produce a partial mass");
        accepted(session.call("doc_apply",json(R"({"commands":[{"op":"assembly.edit","id":"Row","visible":true}]})")));

        accepted(session.call("doc_apply",json(R"({"commands":[{"op":"feature.edit","id":"Cube","p":{"density":1}}]})")));
        native=accepted(session.call("native_inspect",json(R"({"id":1})")));
        check(close(native.find("mass_g")->num,1),"feature density overrides material density");

        ark::AgentSession rotated;
        accepted(rotated.call("doc_apply",json(R"({"commands":[
          {"op":"material.set","name":"Test","density":2},
          {"op":"feature.add","type":"box","name":"Asymmetric","p":{"dx":10,"dy":20,"dz":10},"material":"Test"},
          {"op":"assembly.add","name":"Rotated","feature":"Asymmetric","p":{"tx":100,"rz":90}},
          {"op":"assembly.add","name":"Unrotated","feature":"Asymmetric"}
        ]})")));
        assembly=accepted(rotated.call("assembly_info",Value::makeObj()));
        check(assembly.find("mass_checked")->b&&close(assembly.find("mass_g")->num,8),"rotated assembly exact total mass");
        const auto& center=assembly.find("center_mm")->arr;
        check(close(center[0].num,47.5)&&close(center[1].num,7.5)&&close(center[2].num,5),"rotated asymmetric centroid uses world frames");
#endif

        accepted(session.call("doc_apply",json(R"({"commands":[{"op":"feature.edit","id":"Cube","material":null},{"op":"material.remove","name":"Aluminium"}]})")));
        check(session.document.materials.empty()&&session.document.features.front().material.empty(),"material unassign and removal succeed in one batch");

#ifdef ARK_MATERIAL_EXACT_TESTS
        ark::AgentSession bare;
        accepted(bare.call("doc_apply",json(R"({"commands":[{"op":"feature.add","type":"box","name":"Bare","p":{"dx":10,"dy":10,"dz":10}}]})")));
        native=accepted(bare.call("native_inspect",json(R"({"id":1})")));
        check(!native.find("mass_checked")->b&&!native.find("mass_g"),"missing density reports no mass");
#endif

        ark::Document legacy;check(ark::documentFromJson(R"({"format":"ark","version":1,"units":"mm","next_feature_id":1,"params":[],"sketches":[],"features":[]})",legacy,&error),"legacy document loads without materials");
        check(legacy.materials.empty(),"legacy material default");
        std::cout<<"material and physical properties tests passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
