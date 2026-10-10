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
namespace {
Document steering(){
    Document document;Feature body;body.type=FeatureType::Box;body.p={{"dx","10"},{"dy","10"},{"dz","10"}};document.addFeature(body);
    for(u64 id=1;id<=4;++id){AssemblyNode node;node.id=id;node.feature=1;node.name=id==1?"Frame":id==2?"Fork":id==3?"Handlebar":"Wheel";
        node.fixed=id==1;node.p={{"tx","10"},{"ty","20"},{"tz","30"},{"rx","12"},{"ry","25"},{"rz","40"}};document.assembly.push_back(node);}
    AssemblyMate head;head.id=1;head.a=1;head.b=2;head.type="revolute";head.reference_a=head.reference_b="Z";document.mates.push_back(head);
    AssemblyMate clamp;clamp.id=2;clamp.a=2;clamp.b=3;clamp.type="rigid";document.mates.push_back(clamp);
    AssemblyMate axle;axle.id=3;axle.a=2;axle.b=4;axle.type="revolute";axle.reference_a=axle.reference_b="X";document.mates.push_back(axle);
    document.nextAssemblyId_=5;document.nextMateId_=4;document.recompute();return document;
}
const Mat4& placement(const std::vector<AssemblyPlacement>& placements,u64 node){for(const auto& row:placements)if(row.node==node&&!row.copy)return row.world;throw std::runtime_error("placement missing");}
void equalPose(const Mat4& a,const Mat4& b,const char* reason){for(int i=0;i<16;++i)close(a.m[i],b.m[i],1e-8,reason);}
void testSteeringManipulation(){
    auto document=steering();const auto original=documentToJson(document);std::string error;AssemblyManipulationProgram prepared;
    check(buildAssemblyManipulation(document,prepared,error),error.c_str());AssemblyManipulationJoint joint;
    check(selectAssemblyManipulationJoint(prepared,3,0,"rotate",joint,error),error.c_str());check(joint.mate==1&&joint.movingNodes==std::vector<u64>({2,3,4}),"handlebar steers fork and passive wheel together");
    check(joint.origin.approx({10,20,30},1e-9)&&joint.axis.approx(placement(prepared.baseline,1).transformDir({0,0,1}),1e-9),"semantic steering axis and pivot are world oriented");
    for(double angle:{90.,-90.,450.,-810.,1080.25,-1080.25}){
        AssemblyManipulationPreview preview;check(previewAssemblyManipulation(prepared,3,0,"rotate",angle,preview,error),error.c_str());
        const Mat4 delta=Mat4::translation(joint.origin)*Mat4::axisAngle(joint.axis,deg2rad(std::remainder(angle,360.)))*Mat4::translation(-joint.origin);
        equalPose(placement(preview.placements,1),placement(prepared.baseline,1),"grounded frame stays exact");
        for(u64 node:{2,3,4})equalPose(placement(preview.placements,node),delta*placement(prepared.baseline,node),"fork, handlebar and wheel carry the solved steering transform");
        check(preview.diagnostics.maxPositionMm<1e-5&&preview.diagnostics.maxAngleDeg<1e-4,"all actual mate residuals validate");
        AgentSession session;session.document=document;V request=V::makeObj();request.set("expected_revision",V::makeNum(0));request.set("commands",preview.commands);
        accepted(session.call("doc_apply",request));std::vector<AssemblyPlacement> committed;check(resolveAssembly(session.document,committed,error),error.c_str());
        for(u64 node:{1,2,3,4})equalPose(placement(committed,node),placement(preview.placements,node),"persistent pose recipe agrees with preview");
        Document restored;check(documentFromJson(documentToJson(session.document),restored,&error),error.c_str());check(resolveAssembly(restored,committed,error),error.c_str());
        for(u64 node:{1,2,3,4})equalPose(placement(committed,node),placement(preview.placements,node),"persistent steering survives reopen");
    }
    for(int i=-100;i<=100;++i){AssemblyManipulationPreview preview;check(previewAssemblyManipulation(prepared,3,0,"rotate",i*720.25,preview,error),error.c_str());}
    AssemblyManipulationPreview reset;check(previewAssemblyManipulation(prepared,3,0,"rotate",0,reset,error),error.c_str());
    for(u64 node:{1,2,3,4})equalPose(placement(reset.placements,node),placement(prepared.baseline,node),"repeated absolute previews accumulate no drift");
    check(documentToJson(document)==original,"manipulation preview leaves source document byte-identical");
    check(selectAssemblyManipulationJoint(prepared,4,0,"rotate",joint,error)&&joint.mate==3&&joint.movingNodes==std::vector<u64>({4}),"wheel selection drives nearest axle independently");
    check(!previewAssemblyManipulation(prepared,1,0,"rotate",20,reset,error)&&error.find("MANIPULATION_FIXED_DRIVER")!=std::string::npos,"fixed frame selection rejects");
    check(!previewAssemblyManipulation(prepared,3,3,"rotate",20,reset,error),"explicit wheel joint cannot drive stationary handlebar");
    check(!previewAssemblyManipulation(prepared,3,0,"rotate",INFINITY,reset,error),"nonfinite motion rejects");
    AssemblyMate stop;stop.id=4;stop.a=1;stop.b=2;stop.type="angle";stop.reference_a=stop.reference_b="X";document.mates.push_back(stop);
    check(buildAssemblyManipulation(document,prepared,error),error.c_str());
    check(!previewAssemblyManipulation(prepared,3,1,"rotate",20,reset,error)&&error.find("MANIPULATION_CONSTRAINT_VIOLATION")!=std::string::npos,"locked steering rejects on real angle residual");
    document=steering();document.assembly[3].fixed=true;check(buildAssemblyManipulation(document,prepared,error),error.c_str());
    check(!previewAssemblyManipulation(prepared,3,1,"rotate",20,reset,error),"drive cannot carry a fixed descendant through passive hinge");
    document=steering();document.assembly[2].pattern.active=true;document.assembly[2].pattern.type="linear";document.assembly[2].pattern.count="2";document.assembly[2].pattern.dx="20";
    check(buildAssemblyManipulation(document,prepared,error),error.c_str());check(!previewAssemblyManipulation(prepared,3,0,"rotate",20,reset,error),"patterned instances reject unsupported drive");
}
void testManipulationSliderAndHierarchy(){
    auto document=steering();document.mates.resize(1);document.mates[0].type="concentric";std::string error;AssemblyManipulationProgram prepared;AssemblyManipulationPreview preview;
    check(buildAssemblyManipulation(document,prepared,error),error.c_str());check(previewAssemblyManipulation(prepared,2,1,"translate",25,preview,error),error.c_str());
    const Vec3 direction=placement(prepared.baseline,1).transformDir({0,0,1});
    check(placement(preview.placements,2).transformPoint({}).approx(Vec3{10,20,30}+direction*25,1e-8),"concentric free axial travel is exact");
    AssemblyMate lock;lock.id=2;lock.a=1;lock.b=2;lock.type="distance";document.mates.push_back(lock);
    check(buildAssemblyManipulation(document,prepared,error),error.c_str());check(!previewAssemblyManipulation(prepared,2,1,"translate",25,preview,error)&&error.find("MANIPULATION_CONSTRAINT_VIOLATION")!=std::string::npos,"distance lock prevents axial translation");
    document=steering();AssemblyNode group;group.id=5;group.name="Steering assembly";group.p={{"tx","7"},{"ry","90"}};document.assembly.push_back(group);
    for(size_t i=1;i<4;++i){document.assembly[i].parent=5;document.assembly[i].p.clear();}document.nextAssemblyId_=6;
    check(buildAssemblyManipulation(document,prepared,error),error.c_str());check(previewAssemblyManipulation(prepared,3,1,"rotate",40,preview,error),error.c_str());
    AgentSession session;session.document=document;V request=V::makeObj();request.set("expected_revision",V::makeNum(0));request.set("commands",preview.commands);accepted(session.call("doc_apply",request));
    std::vector<AssemblyPlacement> committed;check(resolveAssembly(session.document,committed,error),error.c_str());for(u64 node:{1,2,3,4})equalPose(placement(committed,node),placement(preview.placements,node),"hierarchical materialized poses preserve preview after commit");
}
}
int main(){try{testBicycle();testDirectionsAndFollowerLaws();testFailures();testAgentAtomicity();testSteeringManipulation();testManipulationSliderAndHierarchy();std::cout<<"Mechanical motion tests passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
