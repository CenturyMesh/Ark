#include "ark/agent.hpp"
#include "ark/assembly_io.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace ark;
using V=arkjson::Value;
namespace {
void check(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
void close(double actual,double expected,double tolerance,const char* reason){check(std::isfinite(actual)&&std::fabs(actual-expected)<=tolerance,reason);}
V json(const char* text){V value;check(arkjson::parse(text,value),"test JSON parses");return value;}
V accepted(const V& result){if(result.find("isError")->b)throw std::runtime_error(result.find("structuredContent")->dump());return *result.find("structuredContent");}
Document transmission(const char* type="chain"){
    Document document;document.setParam("front_teeth","48");document.setParam("rear_teeth","16");document.setParam("cadence","60");
    Feature body;body.type=FeatureType::Box;body.name="Display body";body.p={{"dx","10"},{"dy","10"},{"dz","10"}};const auto feature=document.addFeature(body);
    for(u64 id=1;id<=3;++id){AssemblyNode node;node.id=id;node.feature=feature;node.name=id==1?"Front crank":id==2?"Rear sprocket":"Rear wheel";node.p["tx"]=id==1?"0":"100";document.assembly.push_back(node);}document.nextAssemblyId_=4;
    AssemblyMate chain;chain.id=1;chain.a=1;chain.b=2;chain.type=type;chain.reference_a=chain.reference_b="Z";chain.motion={{"teeth_a","front_teeth"},{"teeth_b","rear_teeth"}};document.mates.push_back(chain);
    AssemblyMate shaft;shaft.id=2;shaft.a=2;shaft.b=3;shaft.type="shaft";shaft.reference_a=shaft.reference_b="Z";document.mates.push_back(shaft);document.nextMateId_=3;
    document.recompute();return document;
}
AssemblyMotionProgram program(const Document& document){AssemblyMotionProgram result;std::string error;check(buildAssemblyMotion(document,result,error),error.c_str());return result;}
const V& instance(const V& report,u64 node){for(const auto& row:report.find("instances")->arr)if(row.find("node")->num==node)return row;throw std::runtime_error("motion instance missing");}
void reject(const Document& document,const char* expected){AssemblyMotionProgram result;std::string error;check(!buildAssemblyMotion(document,result,error),"unsupported mechanism rejects");check(error.find(expected)!=std::string::npos,error.c_str());}
void testBicycle(){
    auto document=transmission();const auto before=documentToJson(document);auto prepared=program(document);const auto report=inspectAssemblyMotion(prepared,1,60,.25);
    close(instance(report,1).find("angle_deg")->num,90,1e-9,"crank quarter-turn");close(instance(report,2).find("angle_deg")->num,270,1e-9,"48/16 teeth drive three quarter-turns");
    close(instance(report,2).find("speed_rpm")->num,180,1e-9,"rear speed derives from teeth counts");close(instance(report,3).find("speed_rpm")->num,180,1e-9,"shaft carries rear wheel speed");
    close(report.find("relations")->arr[0].find("teeth_a")->num,48,0,"evaluated teeth exposed");close(report.find("relations")->arr[0].find("residual_rad")->num,0,1e-12,"chain relation verified");
    const auto& matrix=instance(report,1).find("world_column_major")->arr;close(matrix[0].num,0,1e-9,"matrix rotates crank x");close(matrix[1].num,1,1e-9,"matrix quarter turn is actual solved rotation");
    check(documentToJson(document)==before,"motion does not alter authored/static document");
    document.setParam("rear_teeth","24");document.recompute();auto changed=inspectAssemblyMotion(program(document),1,60,.25);close(instance(changed,3).find("speed_rpm")->num,120,1e-9,"teeth edit changes rear wheel speed");
    auto reverse=inspectAssemblyMotion(prepared,3,180,.25);close(instance(reverse,1).find("speed_rpm")->num,60,1e-9,"angular coupling can be solved from downstream driver");
    Document restored;std::string error;check(documentFromJson(before,restored,&error),error.c_str());check(restored.mates[0].motion.at("teeth_a")=="front_teeth","motion expressions persist");V saved;check(arkjson::parse(before,saved)&&saved.find("version")->num==22,"mechanical persistence version22");saved.set("version",V::makeNum(21));check(!documentFromJson(saved.dump(),restored,&error),"old schema cannot hide mechanical mates");
}
void testDirectionsAndFollowerLaws(){
    auto gear=transmission("gear");auto geared=inspectAssemblyMotion(program(gear),1,60,.25);close(instance(geared,2).find("speed_rpm")->num,-180,1e-9,"external gear counter-rotates");
    auto opposed=transmission();opposed.assembly[1].p["rx"]="180";opposed.assembly[2].p["rx"]="180";auto flipped=inspectAssemblyMotion(program(opposed),1,60,.25);close(instance(flipped,2).find("speed_rpm")->num,-180,1e-9,"opposite semantic axes correct signed coordinates");
    auto follower=transmission();follower.mates.resize(1);follower.mates[0].type="cam";follower.mates[0].reference_b="X";follower.mates[0].motion={{"lift","10"},{"phase_deg","0"}};
    auto cam=inspectAssemblyMotion(program(follower),1,60,.25);close(instance(cam,2).find("displacement_mm")->num,5,1e-9,"harmonic cam quarter-turn lift");close(instance(cam,2).find("velocity_mm_s")->num,10*K_PI,1e-9,"cam velocity is derivative of lift law");close(instance(cam,2).find("world_column_major")->arr[12].num,105,1e-9,"cam follower matrix translates along semantic axis");check(!cam.find("cam_contact_checked")->b,"analytic cam law does not claim geometric contact");
    follower.mates[0].type="rack_pinion";follower.mates[0].motion={{"pitch_radius","10"},{"direction","-1"}};auto rack=inspectAssemblyMotion(program(follower),1,60,.25);close(instance(rack,2).find("displacement_mm")->num,-5*K_PI,1e-9,"rack length derives from angle times pitch radius");
}
void testFailures(){
    auto fixed=transmission();fixed.assembly[0].fixed=true;reject(fixed,"MECHANICAL_FIXED_ENDPOINT");
    auto bad=transmission();bad.mates[0].motion["teeth_b"]="0";reject(bad,"MECHANICAL_INVALID_TEETH");
    auto tilted=transmission();tilted.assembly[1].p["rx"]="90";reject(tilted,"MECHANICAL_AXES_NOT_PARALLEL");
    auto cyclic=transmission();AssemblyMate cycle;cycle.id=3;cycle.a=3;cycle.b=1;cycle.type="shaft";cyclic.mates.push_back(cycle);reject(cyclic,"MECHANICAL_CYCLE_UNSUPPORTED");
    auto redundant=transmission();AssemblyMate extra=redundant.mates[0];extra.id=3;extra.a=3;redundant.mates.push_back(extra);reject(redundant,"MECHANICAL_MULTIPLE_DRIVERS");
    auto groups=transmission();groups.assembly[1].feature=0;reject(groups,"MECHANICAL_GROUP_ENDPOINT_UNSUPPORTED");
    auto extreme=transmission();extreme.mates[0].motion={{"teeth_a","10000"},{"teeth_b","1"}};extreme.mates[1].type="chain";extreme.mates[1].motion={{"teeth_a","10000"},{"teeth_b","1"}};bool rejected=false;try{inspectAssemblyMotion(program(extreme),1,60,10);}catch(const std::exception& error){rejected=std::string(error.what()).find("MECHANICAL_RESOURCE_LIMIT")!=std::string::npos;}check(rejected,"multi-stage extreme ratio rejects before nonfinite matrices");
    auto overconstrained=transmission();AssemblyNode anchor;anchor.id=4;anchor.feature=1;anchor.name="Grounded support";anchor.fixed=true;anchor.p["tx"]="100";overconstrained.assembly.push_back(anchor);AssemblyMate distance;distance.id=3;distance.a=1;distance.b=4;distance.type="distance";distance.reference_a="X";distance.value="100";overconstrained.mates.push_back(distance);rejected=false;try{inspectAssemblyMotion(program(overconstrained),1,60,.25);}catch(const std::exception& error){rejected=std::string(error.what()).find("MECHANICAL_STATIC_CONSTRAINT_VIOLATION")!=std::string::npos;}check(rejected,"motion rejects a geometric constraint it would violate");
}
void testAgentAtomicity(){
    AgentSession session;session.document=transmission();const auto original=documentToJson(session.document);auto report=accepted(session.call("assembly_motion",json(R"({"driver":"Front crank","speed_rpm":"cadence","time_s":0.25})")));close(instance(report,3).find("speed_rpm")->num,180,1e-9,"agent expression-driven motion");check(session.revision==0&&documentToJson(session.document)==original,"agent inspection keeps revision/static pose");
    auto batch=accepted(session.call("query_batch",json(R"({"queries":[{"tool":"assembly_motion","args":{"driver":"Front crank","speed_rpm":"cadence","time_s":0.25},"fields":["relations"]}]})")));check(!batch.find("results")->arr[0].find("isError")->b,"motion is available in read-only batch/postcondition queries");
    auto edited=accepted(session.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"rear_teeth","expr":24}]})")));check(edited.find("revision")->num==1,"valid param edit commits");report=accepted(session.call("assembly_motion",json(R"({"driver":"Front crank","speed_rpm":"cadence","time_s":0.25})")));close(instance(report,3).find("speed_rpm")->num,120,1e-9,"motion cache refreshes after revision change");
    const auto valid=documentToJson(session.document);const auto revision=session.revision;auto invalid=session.call("doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"rear_teeth","expr":0}]})"));check(invalid.find("isError")->b&&session.revision==revision&&documentToJson(session.document)==valid,"invalid teeth edit rolls back the whole batch");check(invalid.find("structuredContent")->find("error_code")->str=="MECHANICAL_INVALID_TEETH","mechanical failures expose typed tool error code");
    check(!accepted(session.call("agent_onboard",json(R"({"topic":"motion"})"))).find("rules")->arr.empty(),"motion onboarding discoverable");
}
}
int main(){try{testBicycle();testDirectionsAndFollowerLaws();testFailures();testAgentAtomicity();std::cout<<"Mechanical motion tests passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
