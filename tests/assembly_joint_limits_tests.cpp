#include "ark/agent.hpp"
#include "ark/assembly_io.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace ark;
using V=arkjson::Value;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void close(double actual,double expected,const char* message){if(!std::isfinite(actual)||std::fabs(actual-expected)>=1e-8)throw std::runtime_error(std::string(message)+": actual="+std::to_string(actual)+", expected="+std::to_string(expected));}
V json(const std::string& source){V value;check(arkjson::parse(source,value),"test JSON parses");return value;}
V accepted(const V& reply){if(reply.find("isError")->b)throw std::runtime_error(reply.find("structuredContent")->dump());return *reply.find("structuredContent");}
V apply(AgentSession& session,const char* source){auto args=json(source);args.set("expected_revision",V::makeNum((double)session.revision));return accepted(session.call("doc_apply",args));}
void rejected(AgentSession& session,const V& args,const char* code){const auto before=documentToJson(session.document);const auto revision=session.revision;
    const auto reply=session.call("doc_apply",args);check(reply.find("isError")->b,"invalid joint request rejects");
    check(reply.find("structuredContent")->dump().find(code)!=std::string::npos,"rejection reports its joint cause");
    const std::string expected=code;if(expected.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ_")==std::string::npos)
        check(reply.find("structuredContent")->find("error_code")->str==expected,"joint rejection exposes the exact structured error code");
    check(session.revision==revision&&documentToJson(session.document)==before,"joint failure preserves complete document/revision/journal");}
V driveArgs(const AgentSession& session,const char* driver,const char* mode,double delta){V args=V::makeObj(),commands=V::makeArr(),command=V::makeObj();
    command.set("op",V::makeStr("assembly.drive"));command.set("driver",V::makeStr(driver));command.set("mode",V::makeStr(mode));command.set("delta",V::makeNum(delta));commands.arr.push_back(command);
    args.set("expected_revision",V::makeNum((double)session.revision));args.set("commands",commands);return args;}
const Mat4& pose(const std::vector<AssemblyPlacement>& placements,u64 node){for(const auto& p:placements)if(p.node==node&&!p.copy)return p.world;throw std::runtime_error("placement missing");}
AssemblyManipulationProgram program(const Document& d){AssemblyManipulationProgram p;std::string error;check(buildAssemblyManipulation(d,p,error),error.c_str());return p;}
AssemblyManipulationPreview preview(const Document& d,u64 driver,const char* mode,double delta){auto p=program(d);AssemblyManipulationPreview out;std::string error;check(previewAssemblyManipulation(p,driver,0,mode,delta,out,error),error.c_str());return out;}
void cylinder(AgentSession& session){apply(session,R"({"commands":[
  {"op":"param.set","name":"stroke","expr":80},
  {"op":"feature.add","type":"box","name":"Test body","p":{"dx":10,"dy":2,"dz":2}},
  {"op":"assembly.add","name":"Barrel","feature":"Test body","fixed":true,"p":{"tx":10,"ty":20,"tz":30,"ry":90}},
  {"op":"assembly.add","name":"Rod","feature":"Test body","p":{"tx":10,"ty":20,"tz":30,"ry":90}},
  {"op":"assembly.add","name":"Eye","feature":"Test body","p":{"tx":10,"ty":20,"tz":30,"ry":90}},
  {"op":"mate.add","type":"slider","a":"Barrel","b":"Rod","reference_a":"Z","reference_b":"Z","limits":{"min":0,"max":"stroke"}},
  {"op":"mate.add","type":"rigid","a":"Rod","b":"Eye"}
]})");}
void testPrismaticStrokeAndPersistence(){
    AgentSession session;cylinder(session);check(session.document.mates[0].type=="prismatic","slider alias stores canonical prismatic type");
    auto p=program(session.document);check(p.diagnostics.freeDofs==1,"prismatic and rigid follower leave one linear coordinate");
    const auto baseline=documentToJson(session.document);const auto revision=session.revision;
    for(double delta:{0.,25.,80.}){auto candidate=preview(session.document,3,"translate",delta);
        check(candidate.joint.mode=="translate"&&candidate.joint.movingNodes==std::vector<u64>({2,3}),"eye selection drives rod and rigid eye");
        close(candidate.joint.position,0,"stroke coordinate begins at zero");close(candidate.joint.max,80,"stroke expression is evaluated");
        close(pose(candidate.placements,2).m[12],10+delta,"world slider axis follows rotated support");
        close(pose(candidate.placements,3).m[12],10+delta,"rigid eye follows full stroke");
        for(int i=0;i<12;++i)close(pose(candidate.placements,2).m[i],pose(p.baseline,2).m[i],"prismatic drive retains full orientation");
        for(int i=0;i<16;++i)close(pose(candidate.placements,1).m[i],pose(p.baseline,1).m[i],"barrel stays grounded");}
    check(documentToJson(session.document)==baseline&&session.revision==revision,"stroke previews never mutate history");
    rejected(session,driveArgs(session,"Rod","translate",-1),"MANIPULATION_LIMIT_VIOLATION");
    rejected(session,driveArgs(session,"Rod","translate",81),"MANIPULATION_LIMIT_VIOLATION");
    rejected(session,driveArgs(session,"Rod","rotate",1),"MANIPULATION_NO_GROUNDED_JOINT");
    auto noRevision=driveArgs(session,"Rod","translate",25);noRevision.obj.erase(std::remove_if(noRevision.obj.begin(),noRevision.obj.end(),[](const auto& item){return item.first=="expected_revision";}),noRevision.obj.end());rejected(session,noRevision,"requires expected_revision");
    auto stale=driveArgs(session,"Rod","translate",25);stale.set("expected_revision",V::makeNum(0));rejected(session,stale,"revision");
    accepted(session.call("doc_apply",driveArgs(session,"Eye","translate",50)));
    close(preview(session.document,2,"translate",0).joint.position,50,"committed stroke is measured from persistent frames");
    rejected(session,driveArgs(session,"Eye","translate",31),"MANIPULATION_LIMIT_VIOLATION");
    rejected(session,driveArgs(session,"Eye","translate",-51),"MANIPULATION_LIMIT_VIOLATION");
    apply(session,R"({"commands":[{"op":"mate.edit","id":1,"limits":{"min":10,"max":"stroke-5"}}]})");
    accepted(session.call("doc_undo",V::makeObj()));close(preview(session.document,2,"translate",0).joint.min,0,"undo restores limit edit");
    accepted(session.call("doc_redo",V::makeObj()));close(preview(session.document,2,"translate",0).joint.min,10,"redo restores authored limits");
    const auto saved=documentToJson(session.document);Document restored;std::string error;check(documentFromJson(saved,restored,&error),error.c_str());
    check(restored.mates[0].limits.max=="stroke-5","limit expression survives reload");close(preview(restored,3,"translate",0).joint.position,50,"reload retains stroke and limits");
    const auto path=std::filesystem::temp_directory_path()/("ark-joint-limits-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ark");
    check(saveDocument(session.document,path.string().c_str()),"binary joint document saves");check(loadDocument(path.string().c_str(),restored,&error),error.c_str());std::filesystem::remove(path);
    close(preview(restored,2,"translate",0).joint.max,75,"binary reload preserves evaluated limit expressions");
    V old=json(documentToJson(session.document,true,false));check(old.find("version")->num==31,"prismatic/limits require semantic document v31");old.set("version",V::makeNum(30));const auto kept=documentToJson(restored);
    check(!documentFromJson(old.dump(),restored,&error)&&documentToJson(restored)==kept,"v30 cannot silently discard v31 joints or replace destination");
    AgentSession reopened;reopened.document=std::move(restored);accepted(reopened.call("doc_undo",V::makeObj()));close(preview(reopened.document,2,"translate",0).joint.min,0,"persisted undo restores former limits");
    accepted(reopened.call("doc_undo",V::makeObj()));close(preview(reopened.document,2,"translate",0).joint.position,0,"persisted undo restores stroke");
    accepted(reopened.call("doc_redo",V::makeObj()));close(preview(reopened.document,2,"translate",0).joint.position,50,"persisted redo restores stroke");
    auto boundEdit=json(R"({"commands":[{"op":"param.set","name":"marker","expr":1},{"op":"param.set","name":"stroke","expr":30}]})");boundEdit.set("expected_revision",V::makeNum((double)session.revision));rejected(session,boundEdit,"ASSEMBLY_JOINT_LIMIT_VIOLATION");
    apply(session,R"({"commands":[{"op":"mate.edit","id":1,"limits":null}]})");check(!preview(session.document,2,"translate",100).joint.limited,"limits:null explicitly removes stops");
}
Document hinge(double coordinate,double minimum,double maximum,bool reverse=false){
    Document d;Feature f;f.type=FeatureType::Box;f.p={{"dx","10"},{"dy","2"},{"dz","2"}};d.addFeature(f);
    for(u64 id:{1,2,3}){AssemblyNode node;node.id=id;node.feature=1;node.name=id==1?"Support":id==2?"Arm":"Grip";node.fixed=reverse?id==2:id==1;
        node.p["rz"]=reverse?(id==1?std::to_string(-coordinate):"0"):(id==1?"0":std::to_string(coordinate));d.assembly.push_back(node);}
    AssemblyMate mate;mate.id=1;mate.a=1;mate.b=2;mate.type="revolute";mate.limits={true,std::to_string(minimum),std::to_string(maximum)};d.mates.push_back(mate);
    AssemblyMate rigid;rigid.id=2;rigid.a=reverse?1:2;rigid.b=3;rigid.type="rigid";d.mates.push_back(rigid);d.nextAssemblyId_=4;d.nextMateId_=3;d.recompute();return d;
}
void testAngularBranchesAndReverseGround(){
    for(bool reverse:{false,true}){auto d=hinge(170,120,240,reverse);const double sign=reverse?-1:1;
        for(double coordinate:{120.,180.,240.}){const auto candidate=preview(d,3,"rotate",sign*(coordinate-170));
            close(candidate.joint.position,170,"signed angular baseline is independent of grounded endpoint");close(candidate.joint.coordinateSign,sign,"reverse ground changes drag-to-coordinate sign");
            AgentSession session;session.document=d;accepted(session.call("doc_apply",driveArgs(session,"Grip","rotate",sign*(coordinate-170))));
            auto actual=preview(session.document,3,"rotate",0);close(actual.joint.position,coordinate,"continuous sub-turn branch survives pose commit");
            Document reloaded;std::string error;check(documentFromJson(documentToJson(session.document),reloaded,&error),error.c_str());close(preview(reloaded,3,"rotate",0).joint.position,coordinate,"180-degree branch persists across reload");
        }
        AgentSession session;session.document=d;
        rejected(session,driveArgs(session,"Grip","rotate",sign*(-51)),"MANIPULATION_LIMIT_VIOLATION");
        rejected(session,driveArgs(session,"Grip","rotate",sign*71),"MANIPULATION_LIMIT_VIOLATION");
        rejected(session,driveArgs(session,"Grip","rotate",360),"MANIPULATION_LIMIT_VIOLATION");
        rejected(session,driveArgs(session,"Grip","rotate",-360),"MANIPULATION_LIMIT_VIOLATION");
    }
    AgentSession centered;centered.document=hinge(0,-45,45);accepted(centered.call("doc_apply",driveArgs(centered,"Grip","rotate",45)));
    accepted(centered.call("doc_apply",driveArgs(centered,"Grip","rotate",-90)));close(preview(centered.document,3,"rotate",0).joint.position,-45,"both signed angular stops reached exactly");
    rejected(centered,driveArgs(centered,"Grip","rotate",-1),"MANIPULATION_LIMIT_VIOLATION");
    AgentSession positive;positive.document=hinge(0,0,120);accepted(positive.call("doc_apply",driveArgs(positive,"Grip","rotate",120)));rejected(positive,driveArgs(positive,"Grip","rotate",1),"MANIPULATION_LIMIT_VIOLATION");
}
void testValidationAndUnsupportedLoops(){
    AgentSession session;cylinder(session);
    for(const char* command:{R"({"op":"mate.edit","id":1,"limits":{"min":80,"max":0}})",R"({"op":"mate.edit","id":1,"limits":{"min":0}})",R"({"op":"mate.edit","id":1,"limits":{"min":0,"max":"missing"}})",R"({"op":"mate.edit","id":2,"limits":{"min":0,"max":10}})"}){
        V args=V::makeObj(),commands=V::makeArr();commands.arr.push_back(json(command));args.set("commands",commands);args.set("expected_revision",V::makeNum((double)session.revision));rejected(session,args,"ASSEMBLY_INVALID_JOINT_LIMITS");}
    auto outside=json(R"({"commands":[{"op":"assembly.edit","id":"Rod","p":{"tx":90.000001,"ty":20,"tz":30,"ry":90}}]})");outside.set("expected_revision",V::makeNum((double)session.revision));rejected(session,outside,"ASSEMBLY_JOINT_LIMIT_VIOLATION");
    apply(session,R"({"commands":[{"op":"assembly.edit","id":"Rod","p":{"tx":90,"ty":20,"tz":30,"ry":90}}]})");close(preview(session.document,2,"translate",0).joint.position,80,"exact static stop accepts then supports zero preview");
    accepted(session.call("doc_apply",driveArgs(session,"Rod","translate",-80)));
    AgentSession angular;angular.document=hinge(0,-45,45);auto bad=json(R"({"commands":[{"op":"mate.edit","id":1,"limits":{"min":-180,"max":180}}]})");bad.set("expected_revision",V::makeNum(0));rejected(angular,bad,"ASSEMBLY_INVALID_JOINT_LIMITS");
    auto opposed=session.document;opposed.assembly[1].p["rz"]="180";opposed.assembly[1].p["tx"]="35";opposed.assembly[2].p["tx"]="35";auto p=program(opposed);
    close(pose(p.baseline,2).m[12],35,"full-orientation solve retains authored free axial translation");check(pose(p.baseline,2).transformDir({1,0,0}).approx(pose(p.baseline,1).transformDir({1,0,0}),1e-8),"prismatic corrects a 180-degree twist pole instead of accepting axial spin");
    AssemblyMate loop;loop.id=3;loop.a=3;loop.b=1;loop.type="revolute";session.document.mates.push_back(loop);session.document.nextMateId_=4;
    rejected(session,driveArgs(session,"Rod","translate",5),"MANIPULATION_NO_GROUNDED_JOINT");
}
#ifdef ARK_JOINT_NATIVE_TESTS
void testNativeCylinderExample(){
    std::ifstream file(std::filesystem::path(ARK_SOURCE_DIR)/"examples/pneumatic-cylinder.arkops");check((bool)file,"native cylinder example exists");const std::string source((std::istreambuf_iterator<char>(file)),{});
    auto recipe=json(source);AgentSession session;accepted(session.call("doc_apply",*recipe.find("arguments")));
    accepted(session.call("doc_apply",driveArgs(session,"Rod eye","translate",80)));
    const auto candidate=preview(session.document,2,"translate",0);close(candidate.joint.position,80,"native barrel/rod exact concept reaches authored stroke");
    for(const auto& body:session.document.evalBodies){const auto* report=body.nativeData.find("report");check(report&&report->find("brep_valid")->b&&report->find("solids")->num==1,"synthetic pneumatic component preserves validated B-rep");}
    rejected(session,driveArgs(session,"Rod eye","translate",.1),"MANIPULATION_LIMIT_VIOLATION");
}
#endif
}
int main(){try{testPrismaticStrokeAndPersistence();testAngularBranchesAndReverseGround();testValidationAndUnsupportedLoops();
#ifdef ARK_JOINT_NATIVE_TESTS
    testNativeCylinderExample();
#endif
    std::cout<<"assembly prismatic and joint-limit contracts passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
