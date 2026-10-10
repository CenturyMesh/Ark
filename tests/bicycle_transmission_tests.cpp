#include "ark/agent.hpp"
#include "ark/assembly.hpp"
#include "../src/occt/common.hpp"
#include <BRepClass3d_SolidClassifier.hxx>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using V=arkjson::Value;
namespace fs=std::filesystem;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void checkClose(double measured,double expected,const char* message){check(std::abs(measured-expected)<1e-7,message);}
V json(const char* text){V value;check(arkjson::parse(text,value),"test JSON parses");return value;}
V accepted(const V& response){if(response.find("isError")->b)throw std::runtime_error(response.find("structuredContent")->dump());return *response.find("structuredContent");}
const V& instance(const V& motion,const char* name){for(const auto& row:motion.find("instances")->arr)if(row.find("name")->str==name)return row;throw std::runtime_error("Motion instance missing");}
const ark::Body& body(const ark::Document& document,const char* name){ark::u64 id=0;for(const auto& feature:document.features)if(feature.name==name)id=feature.id;for(const auto& body:document.evalBodies)if(body.id==id)return body;throw std::runtime_error("Native bicycle geometry missing");}
void teeth(const ark::Document& document,const char* pattern,const char* rotor,double radius,int count){
    check(body(document,pattern).nativeData.find("report")->find("solids")->num==count,"visible tooth copy count equals authored teeth");
    const auto shape=ark::occt::readBrep(body(document,rotor).nativeData.find("brep_base64")->str);
    for(int index=0;index<count;++index){
        const double pitch=2*ark::K_PI/count,angle=index*pitch;
        BRepClass3d_SolidClassifier tooth(shape,gp_Pnt((radius+.5)*std::cos(angle),(radius+.5)*std::sin(angle),3),1e-7);
        BRepClass3d_SolidClassifier gap(shape,gp_Pnt((radius+.5)*std::cos(angle+pitch/2),(radius+.5)*std::sin(angle+pitch/2),3),1e-7);
        check(tooth.State()==TopAbs_IN&&gap.State()==TopAbs_OUT,"each visible radial tooth has actual material and a clear adjacent gap");
    }
}
V motion(ark::AgentSession& session,double time=.125){V args=V::makeObj();args.set("driver",V::makeStr("Front crank"));args.set("speed_rpm",V::makeStr("cadence_rpm"));args.set("time_s",V::makeNum(time));return accepted(session.call("assembly_motion",args));}
void speeds(const V& result,double cadence,double ratio){
    checkClose(instance(result,"Front crank").find("speed_rpm")->num,cadence,"crank cadence preserved");
    checkClose(instance(result,"Rear sprocket").find("speed_rpm")->num,cadence*ratio,"rear sprocket speed follows tooth ratio");
    checkClose(instance(result,"Rear wheel").find("speed_rpm")->num,cadence*ratio,"shaft drives actual rear wheel speed");
    checkClose(instance(result,"Front crank").find("angle_deg")->num,cadence*.75,"crank angle advances over one eighth second");
    checkClose(instance(result,"Rear wheel").find("angle_deg")->num,cadence*ratio*.75,"rear wheel angle advances by ratio");
    check(result.find("static_constraints_verified")->b,"animated poses retain declared static mounts");
    check(!result.find("tooth_contact_checked")->b&&!result.find("dynamics_checked")->b,"ideal motion does not claim tooth contact or dynamics qualification");
    int active=0;for(const auto& relation:result.find("relations")->arr)if(relation.find("active")->b){++active;check(std::abs(relation.find("residual_rad")->num)<1e-8,"active coupling residual closes");}
    check(active==2,"chain and rigid shaft both participate in drivetrain propagation");
}
}
int main(){try{
    const auto replayPath=fs::path(ARK_SOURCE_DIR)/"examples/bicycle-transmission.arkops";
    std::ifstream file(replayPath);check(bool(file),"bicycle replay fixture exists");std::string bytes((std::istreambuf_iterator<char>(file)),{});V replay;check(arkjson::parse(bytes,replay),"bicycle replay parses");
    ark::AgentSession session;accepted(session.call("doc_apply",*replay.find("arguments")));
    check(session.document.features.size()==31&&session.document.assembly.size()==13,"complete compact bicycle fixture instantiated");
    for(const auto& evaluated:session.document.lastResults)check(evaluated.ok,"every native bicycle feature evaluates");
    teeth(session.document,"Front chainring visible teeth","Front chainring toothed rotor",12.7*48/(2*ark::K_PI),48);
    teeth(session.document,"Rear cog visible teeth","Rear cog toothed rotor",12.7*16/(2*ark::K_PI),16);
    check(body(session.document,"Chain pitch envelope").nativeData.find("report")->find("brep_valid")->b,"closed tangent chain envelope is actual valid native geometry");
    const double frontRadius=12.7*48/(2*ark::K_PI),rearRadius=12.7*16/(2*ark::K_PI),distance=std::sqrt(420.*420+40.*40),beta=std::acos((frontRadius-rearRadius)/distance);
    const double envelopeLength=2*std::sqrt(distance*distance-std::pow(frontRadius-rearRadius,2))+2*beta*rearRadius+(2*ark::K_PI-2*beta)*frontRadius;
    const double envelopeVolume=body(session.document,"Chain pitch envelope").nativeData.find("report")->find("volume_mm3")->num;
    check(std::abs(envelopeVolume-envelopeLength*4*ark::K_PI)<envelopeVolume*1e-7,"exact chain sweep volume agrees with independent external-tangent length");
    const auto saved=ark::documentToJson(session.document);const auto revision=session.revision;const auto first=motion(session);speeds(first,60,3);
    const auto& wheelMatrix=instance(first,"Rear wheel").find("world_column_major")->arr;
    checkClose(wheelMatrix[12].num,-420,"motion preserves rear axle X");checkClose(wheelMatrix[13].num,320,"motion preserves rear axle height");checkClose(wheelMatrix[14].num,0,"motion preserves rear axle plane");
    checkClose(wheelMatrix[0].num,std::cos(135*ark::K_PI/180),"wheel actual pose rotates with derived angle");
    check(session.revision==revision&&ark::documentToJson(session.document)==saved,"motion inspection changes neither intent nor revision");
    accepted(session.call("doc_apply",json(R"({"commands":[{"op":"param.set","name":"cadence_rpm","expr":"90"}]})")));speeds(motion(session),90,3);
    accepted(session.call("doc_apply",json(R"({"commands":[{"op":"param.set","name":"rear_teeth","expr":"24"}]})")));speeds(motion(session),90,2);
    teeth(session.document,"Rear cog visible teeth","Rear cog toothed rotor",12.7*24/(2*ark::K_PI),24);
    const auto changed=ark::documentToJson(session.document);const auto changedRevision=session.revision;
    const auto invalid=session.call("doc_apply",json(R"({"commands":[{"op":"param.set","name":"rear_teeth","expr":"2.5"}]})"));check(invalid.find("isError")->b&&session.revision==changedRevision&&ark::documentToJson(session.document)==changed,"noninteger teeth edit rolls back geometry and intent");
    ark::Document loaded;std::string error;check(ark::documentFromJson(changed,loaded,&error),"mechanical bicycle intent loads");loaded.recompute();for(const auto& evaluated:loaded.lastResults)check(evaluated.ok,"loaded mechanical bicycle reevaluates");
    ark::AssemblyMotionProgram program;check(ark::buildAssemblyMotion(loaded,program,error),"loaded relations rebuild motion program");ark::u64 driver=0;for(const auto& node:loaded.assembly)if(node.name=="Front crank")driver=node.id;
    speeds(ark::inspectAssemblyMotion(program,driver,90,.125),90,2);
    accepted(session.call("doc_undo",V::makeObj()));speeds(motion(session),90,3);
    accepted(session.call("doc_undo",V::makeObj()));speeds(motion(session),60,3);
    std::cout<<"Native visible teeth, tangent chain, ratio/cadence motion, rear wheel transforms, no-mutation, rollback, persistence and undo passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
