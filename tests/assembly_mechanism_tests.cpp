#include "ark/agent.hpp"
#include "ark/assembly_io.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace ark;
using V=arkjson::Value;
namespace {
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
void close(double a,double b,double epsilon,const char* why){check(std::isfinite(a)&&std::fabs(a-b)<epsilon,std::string(why)+": "+std::to_string(a)+" / "+std::to_string(b));}
V accepted(const V& reply){check(!reply.find("isError")->b,reply.find("structuredContent")->dump());return *reply.find("structuredContent");}
const Mat4& pose(const std::vector<AssemblyPlacement>& rows,u64 node){for(const auto& row:rows)if(row.node==node&&!row.copy)return row.world;throw std::runtime_error("missing mechanism pose");}
AssemblyManipulationProgram program(const Document& document){AssemblyManipulationProgram p;std::string error;check(buildAssemblyManipulation(document,p,error),error);return p;}
AssemblyManipulationPreview drive(const Document& document,u64 node,u64 mate,const char* mode,double delta){auto p=program(document);AssemblyManipulationPreview result;std::string error;check(previewAssemblyManipulation(p,node,mate,mode,delta,result,error),error);return result;}
V request(AgentSession& session,u64 driver,u64 mate,const char* mode,double delta){V args=V::makeObj(),commands=V::makeArr(),command=V::makeObj();
    args.set("expected_revision",V::makeNum((double)session.revision));command.set("op",V::makeStr("assembly.drive"));command.set("driver",V::makeNum((double)driver));
    command.set("mate",V::makeNum((double)mate));command.set("mode",V::makeStr(mode));command.set("delta",V::makeNum(delta));commands.arr.push_back(command);args.set("commands",commands);return args;}
void rejectAtomic(AgentSession& session,V args,const char* code){const auto bytes=documentToJson(session.document);const auto revision=session.revision;
    const auto reply=session.call("doc_apply",args);check(reply.find("isError")->b,"impossible mechanism movement rejects");check(reply.find("structuredContent")->dump().find(code)!=std::string::npos,reply.find("structuredContent")->dump());
    check(session.revision==revision&&documentToJson(session.document)==bytes,"mechanism rejection retains complete pose/revision/journal");}
void equal(const Mat4& a,const Mat4& b,double epsilon,const char* why){for(int i=0;i<16;++i)close(a.m[i],b.m[i],epsilon,why);}
u64 part(Document& d,const char* name,u64 parent,double x,double y=0,bool fixed=false){AssemblyNode n;n.id=d.nextAssemblyId_++;n.parent=parent;n.feature=1;n.name=name;n.fixed=fixed;
    n.p={{"tx",V::makeNum(x).dump()},{"ty",V::makeNum(y).dump()}};d.assembly.push_back(n);return n.id;}
u64 group(Document& d,const char* name,Vec3 p,double angle){AssemblyNode n;n.id=d.nextAssemblyId_++;n.name=name;n.p={{"tx",V::makeNum(p.x).dump()},{"ty",V::makeNum(p.y).dump()},{"tz",V::makeNum(p.z).dump()},{"rz",V::makeNum(angle).dump()}};d.assembly.push_back(n);return n.id;}
void revolute(Document& d,u64 a,u64 b,bool limited=false,double minimum=0,double maximum=0){AssemblyMate m;m.id=d.nextMateId_++;m.a=a;m.b=b;m.type="revolute";
    if(limited)m.limits={true,std::to_string(minimum),std::to_string(maximum)};d.mates.push_back(m);}
// Explicit link/pin origins in rigid subassembly groups exercise the production
// static and manipulation solvers without fabricated geometry references.
Document fourbar(bool parallelogram=true){Document d;Feature f;f.type=FeatureType::Box;f.p={{"dx","4"},{"dy","4"},{"dz","2"}};d.addFeature(f);
    const double a=parallelogram?60:70,b=parallelogram?100:60,c=parallelogram?60:40,theta=parallelogram?45:60;
    const Vec3 A{a*std::cos(deg2rad(theta)),a*std::sin(deg2rad(theta)),0},D{100,0,0};Vec3 B;
    if(parallelogram)B=A+D;else{const Vec3 v=D-A;const double distance=v.len(),along=(b*b-c*c+distance*distance)/(2*distance),height=std::sqrt(b*b-along*along);
        const Vec3 u=v/distance;B=A+u*along+Vec3{-u.y,u.x,0}*height;}
    const double beta=rad2deg(std::atan2(B.y-A.y,B.x-A.x)),gamma=rad2deg(std::atan2(B.y,B.x-100));
    const u64 ground=group(d,"Chassis",{},0),lower=group(d,"Lower arm",{},theta),upright=group(d,"Upright",A,beta),upper=group(d,"Upper arm",D,gamma);
    // IDs: groups1..4, grounded pins5/6, lower7/8/9, upright10/11/12, upper13/14/15.
    const u64 groundA=part(d,"Ground A",ground,0,0,true),groundD=part(d,"Ground D",ground,100);
    const u64 lowerA=part(d,"Lower pin A",lower,0),lowerB=part(d,"Lower pin B",lower,a);part(d,"Lower body",lower,a*.5);
    const u64 upA=part(d,"Upright pin A",upright,0),upB=part(d,"Upright pin B",upright,b);part(d,"Upright body",upright,b*.5);
    const u64 upperA=part(d,"Upper pin A",upper,0),upperB=part(d,"Upper pin B",upper,c);part(d,"Upper body",upper,c*.5);
    revolute(d,groundA,lowerA,parallelogram,10,140);revolute(d,lowerB,upA);revolute(d,upB,upperB);revolute(d,groundD,upperA);d.recompute();return d;}
void verify(const AssemblyManipulationPreview& p){check(p.joint.constrained&&p.continuationSteps>0,"loop uses actual constrained continuation");
    check(p.diagnostics.maxPositionMm<1e-5&&p.diagnostics.maxAngleDeg<1e-4,"every retained geometric mate satisfies unchanged tolerances");}
void testSuspension(){auto document=fourbar();const auto initial=program(document);check(initial.diagnostics.freeDofs==1,"closed fourbar has one measured geometric freedom");
    const auto before=documentToJson(document);for(int angle=15;angle<=125;angle+=5){const auto p=drive(document,9,1,"rotate",angle-45);verify(p);
        equal(pose(initial.baseline,5),pose(p.placements,5),0.5e-12,"ground A exact");equal(pose(initial.baseline,6),pose(p.placements,6),0.5e-12,"ground D exact");
        const Vec3 actual=pose(p.placements,8).transformPoint({});close(actual.x,60*std::cos(deg2rad(angle)),1e-7,"lower pin x follows real arm length");close(actual.y,60*std::sin(deg2rad(angle)),1e-7,"lower pin y follows real arm length");
        close(pose(p.placements,11).m[12]-actual.x,100,1e-7,"upright retains link length");close(pose(p.placements,11).m[13]-actual.y,0,1e-7,"parallelogram upright retains orientation");}
    check(documentToJson(document)==before,"all loop previews are read-only");
    auto tilted=document;const u64 tiltedRoot=group(tilted,"Repositioned linkage",{120,-75,33},30);
    tilted.assembly.back().p["rx"]="20";tilted.assembly.back().p["ry"]="15";for(auto& node:tilted.assembly)if(node.id<=4)node.parent=tiltedRoot;tilted.recompute();
    const Mat4 reposition=Mat4::translation({120,-75,33})*Mat4::rotZ(deg2rad(30))*Mat4::rotY(deg2rad(15))*Mat4::rotX(deg2rad(20));
    const auto ordinary=drive(document,9,1,"rotate",30),repositioned=drive(tilted,9,1,"rotate",30);verify(repositioned);
    for(u64 node:{5,6,7,8,9,10,11,12,13,14,15})equal(reposition*pose(ordinary.placements,node),pose(repositioned.placements,node),1e-7,"tilted planar linkage follows authored frames under rigid repositioning");
    auto ungrounded=document;for(auto& node:ungrounded.assembly)node.fixed=false;std::vector<AssemblyPlacement> ungroundedPlacements;std::string ungroundedError;
    check(!resolveAssembly(ungrounded,ungroundedPlacements,ungroundedError)&&ungroundedError.find("no fixed anchor")!=std::string::npos,"unanchored closed carriers still reject explicitly");
    for(double stop:{10.,140.}){const auto p=drive(document,9,1,"rotate",stop-45);verify(p);close(std::atan2(pose(p.placements,7).m[1],pose(p.placements,7).m[0])*180/K_PI,stop,1e-9,"closed linkage reaches exact authored angular stops");}
    auto patterned=document;const u64 unrelated=part(patterned,"Unrelated bolt pattern",0,250);patterned.assembly.back().pattern.active=true;patterned.assembly.back().pattern.type="linear";
    patterned.assembly.back().pattern.count="3";patterned.assembly.back().pattern.dy="30";patterned.recompute();const auto p0=program(patterned);const auto p1=drive(patterned,9,1,"rotate",20);
    for(const auto& row:p0.baseline)if(row.node==unrelated){const auto changed=std::find_if(p1.placements.begin(),p1.placements.end(),[&](const AssemblyPlacement& candidate){return candidate.node==row.node&&candidate.copy==row.copy;});
        check(changed!=p1.placements.end(),"unrelated pattern copy retained");equal(row.world,changed->world,1e-12,"constrained loop leaves unrelated patterned copies unchanged");}
    AgentSession session;session.document=document;double angle=45;for(double target:{15.,40.,75.,110.,125.,85.,45.}){
        const auto preview=drive(session.document,9,1,"rotate",target-angle);accepted(session.call("doc_apply",request(session,9,1,"rotate",target-angle)));
        const auto committed=program(session.document);for(u64 node:{5,6,7,8,9,10,11,12,13,14,15})equal(pose(preview.placements,node),pose(committed.baseline,node),1e-7,"committed closed loop matches preview");angle=target;}
    for(u64 node:{5,6,7,8,9,10,11,12,13,14,15})equal(pose(initial.baseline,node),pose(program(session.document).baseline,node),2e-7,"reverse continuation returns all suspension links");
    accepted(session.call("doc_apply",request(session,9,1,"rotate",20)));const auto saved=documentToJson(session.document);Document restored;std::string error;check(documentFromJson(saved,restored,&error),error);
    equal(pose(program(session.document).baseline,8),pose(program(restored).baseline,8),1e-8,"loop pose reload");
    const auto package=std::filesystem::temp_directory_path()/("ark-closed-mechanism-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ark");
    check(saveDocument(session.document,package.string().c_str()),"closed-loop binary package saves");const bool loaded=loadDocument(package.string().c_str(),restored,&error);std::filesystem::remove(package);check(loaded,error);
    equal(pose(program(session.document).baseline,8),pose(program(restored).baseline,8),1e-8,"binary loop pose reload");
    AgentSession reopened;reopened.document=restored;accepted(reopened.call("doc_undo",V::makeObj()));equal(pose(initial.baseline,8),pose(program(reopened.document).baseline,8),2e-7,"persisted undo restores suspension pose");
    accepted(reopened.call("doc_redo",V::makeObj()));equal(pose(program(session.document).baseline,8),pose(program(reopened.document).baseline,8),1e-8,"persisted redo restores suspension pose");
    rejectAtomic(session,request(session,9,1,"rotate",80),"MANIPULATION_LIMIT_VIOLATION");rejectAtomic(session,request(session,9,1,"rotate",360),"MANIPULATION_LIMIT_VIOLATION");
    auto stale=request(session,9,1,"rotate",2);stale.set("expected_revision",V::makeNum(0));rejectAtomic(session,stale,"revision");
    auto impossible=fourbar(false);AgentSession locked;locked.document=impossible;rejectAtomic(locked,request(locked,9,1,"rotate",80),"MANIPULATION_CONSTRAINT_VIOLATION");}
Document sliderCrank(){Document d;Feature f;f.type=FeatureType::Box;f.p={{"dx","4"},{"dy","4"},{"dz","2"}};d.addFeature(f);
    const double r=30,l=90,angle=40;const Vec3 A{r*std::cos(deg2rad(angle)),r*std::sin(deg2rad(angle)),0};const double x=A.x+std::sqrt(l*l-A.y*A.y),beta=rad2deg(std::atan2(-A.y,x-A.x));
    const u64 ground=group(d,"Ground",{},0),crank=group(d,"Crank",{},angle),rod=group(d,"Connecting rod",A,beta);
    const u64 groundPin=part(d,"Ground pivot",ground,0,0,true),guide=part(d,"Ground guide",ground,0);d.assembly.back().p["ry"]="90";
    const u64 crankA=part(d,"Crank pivot",crank,0),crankB=part(d,"Crank eye",crank,r);part(d,"Crank body",crank,r*.5);
    const u64 rodA=part(d,"Rod crank eye",rod,0),rodB=part(d,"Rod slider eye",rod,l);part(d,"Rod body",rod,l*.5);
    const u64 slider=part(d,"Slider",0,x),eye=part(d,"Slider eye",0,x);d.assembly[d.assembly.size()-2].p["ry"]="90";
    revolute(d,groundPin,crankA);revolute(d,crankB,rodA);revolute(d,rodB,eye);
    AssemblyMate rigid;rigid.id=d.nextMateId_++;rigid.a=slider;rigid.b=eye;rigid.type="rigid";rigid.reference_b="X";d.mates.push_back(rigid);
    AssemblyMate travel;travel.id=d.nextMateId_++;travel.a=guide;travel.b=slider;travel.type="prismatic";travel.limits={true,"60","120"};d.mates.push_back(travel);d.recompute();return d;}
void testSliderCrank(){auto d=sliderCrank();const auto baseline=program(d);check(baseline.diagnostics.freeDofs==1,"slider crank has one geometric freedom");
    const double initial=30*std::cos(deg2rad(40))+std::sqrt(90*90-std::pow(30*std::sin(deg2rad(40)),2));
    for(double angle:{10.,20.,40.,60.,90.,120.,150.,170.}){const auto p=drive(d,8,1,"rotate",angle-40);verify(p);
        const double expected=30*std::cos(deg2rad(angle))+std::sqrt(90*90-std::pow(30*std::sin(deg2rad(angle)),2));close(pose(p.placements,12).m[12],expected,1e-7,"closed slider crank travel solves linkage length");}
    const auto translated=drive(d,12,5,"translate",100-initial);verify(translated);close(pose(translated.placements,12).m[12],100,1e-7,"dragging slider solves passive linked arms");
    AgentSession session;session.document=d;accepted(session.call("doc_apply",request(session,12,5,"translate",100-initial)));accepted(session.call("doc_apply",request(session,12,5,"translate",initial-100)));
    for(u64 node:{4,5,6,7,8,9,10,11,12,13})equal(pose(baseline.baseline,node),pose(program(session.document).baseline,node),1e-7,"reversed slider input returns linked arm branch");
    rejectAtomic(session,request(session,12,5,"translate",121-initial),"MANIPULATION_LIMIT_VIOLATION");}
Document transmission(const char* type="chain",bool follower=false){Document d;Feature f;f.type=FeatureType::Box;f.p={{"dx","4"},{"dy","4"},{"dz","2"}};d.addFeature(f);
    const u64 supportA=part(d,"Support A",0,0,0,true),input=part(d,"Input",0,0),supportB=part(d,"Support B",0,100,0,true),output=part(d,"Output",0,100);
    revolute(d,supportA,input);if(follower){d.assembly[2].p["ry"]="90";d.assembly[3].p["ry"]="90";AssemblyMate slide;slide.id=d.nextMateId_++;slide.a=supportB;slide.b=output;slide.type="prismatic";slide.limits={true,"-80","80"};d.mates.push_back(slide);}else revolute(d,supportB,output);
    AssemblyMate coupling;coupling.id=d.nextMateId_++;coupling.a=input;coupling.b=output;coupling.type=type;
    if(std::string(type)=="chain"||std::string(type)=="gear")coupling.motion={{"teeth_a","48"},{"teeth_b","16"},{"phase_deg","17"}};
    if(std::string(type)=="rack_pinion")coupling.motion={{"pitch_radius","10"},{"direction","-1"}};
    if(std::string(type)=="cam")coupling.motion={{"lift","10"},{"phase_deg","25"}};
    d.mates.push_back(coupling);if(!follower){const u64 wheel=part(d,"Wheel",0,100);revolute(d,supportB,wheel);AssemblyMate shaft;shaft.id=d.nextMateId_++;shaft.a=output;shaft.b=wheel;shaft.type="shaft";d.mates.push_back(shaft);}
    d.recompute();return d;}
void testTransmissionDrag(){for(const char* type:{"chain","gear"}){auto d=transmission(type);const auto base=program(d);const double sign=std::string(type)=="gear"?-1:1;
        for(double delta:{-70.,-15.,0.,15.,70.,130.}){const auto p=drive(d,2,1,"rotate",delta);verify(p);check(p.couplings.arr.size()==2,"chain/gear and shaft coupling residuals exposed");
            const double expected=deg2rad(sign*3*delta);close(pose(p.placements,4).m[0],std::cos(expected),1e-8,"coupling actual output rotation");close(pose(p.placements,5).m[1],std::sin(expected),1e-8,"shaft actual wheel follows ratio");equal(pose(base.baseline,3),pose(p.placements,3),1e-12,"transmission ground stationary");}
        AgentSession session;session.document=d;accepted(session.call("doc_apply",request(session,2,1,"rotate",20)));accepted(session.call("doc_apply",request(session,2,1,"rotate",20)));
        const auto doubled=program(session.document);close(pose(doubled.baseline,4).m[1],std::sin(deg2rad(sign*120)),1e-8,"phase offset is not reapplied on relative commit");
        accepted(session.call("doc_apply",request(session,4,2,"rotate",-sign*120)));for(u64 node:{1,2,3,4,5})equal(pose(base.baseline,node),pose(program(session.document).baseline,node),1e-7,"reverse output drag propagates to input");}
    auto carrier=transmission();const u64 grip=part(carrier,"Rigid input grip",0,0);AssemblyMate gripAttachment;gripAttachment.id=carrier.nextMateId_++;gripAttachment.a=2;gripAttachment.b=grip;gripAttachment.type="rigid";carrier.mates.push_back(gripAttachment);
    carrier.mates[2].a=grip;carrier.mates[2].motion["teeth_a"]="960";carrier.recompute();const auto carried=drive(carrier,2,1,"rotate",4);verify(carried);
    close(pose(carried.placements,5).m[1],std::sin(deg2rad(240)),1e-8,"large coupling ratio steps follow selected pin's rigid carrier");
    auto rack=transmission("rack_pinion",true);const auto p=drive(rack,2,1,"rotate",90);verify(p);close(pose(p.placements,4).m[12],100-5*K_PI,1e-8,"rack travel from pinion angle");
    const auto reverse=drive(rack,4,2,"translate",-5*K_PI);verify(reverse);close(pose(reverse.placements,2).m[1],1,1e-8,"rack reverse drive rotates pinion");
    auto smallRack=transmission("rack_pinion",true);smallRack.mates[2].motion["pitch_radius"]="0.5";smallRack.recompute();
    const auto smallReverse=drive(smallRack,4,2,"translate",-K_PI*.25);verify(smallReverse);close(pose(smallReverse.placements,2).m[1],1,1e-8,"small-radius reverse rack input uses ratio-scaled continuation steps");
    auto cam=transmission("cam",true);AgentSession session;session.document=cam;accepted(session.call("doc_apply",request(session,2,1,"rotate",45)));accepted(session.call("doc_apply",request(session,2,1,"rotate",45)));
    close(pose(program(session.document).baseline,4).m[12],100+5*(std::cos(deg2rad(25))-std::cos(deg2rad(115))),1e-8,"cam phase survives consecutive commits");
    Document restored;std::string error;check(documentFromJson(documentToJson(session.document),restored,&error),error);session.document=restored;accepted(session.call("doc_apply",request(session,2,1,"rotate",-90)));
    equal(pose(program(cam).baseline,4),pose(program(session.document).baseline,4),1e-7,"cam reverse after reload returns original follower");
    AgentSession limits;limits.document=transmission("rack_pinion",true);rejectAtomic(limits,request(limits,2,1,"rotate",500),"MANIPULATION_LIMIT_VIOLATION");}
#ifdef ARK_MECHANISM_NATIVE_TESTS
void testNativeSuspensionExample(){std::ifstream file(std::filesystem::path(ARK_SOURCE_DIR)/"examples/fourbar-suspension.arkops");check((bool)file,"native closed-loop suspension example exists");
    const std::string source((std::istreambuf_iterator<char>(file)),{});V recipe;check(arkjson::parse(source,recipe),"native suspension recipe parses");AgentSession session;accepted(session.call("doc_apply",*recipe.find("arguments")));
    for(const auto& body:session.document.evalBodies){const auto* report=body.nativeData.find("report");check(report&&report->find("brep_valid")->b&&report->find("solids")->num==1,"authored suspension component has a valid exact solid");}
    // Node8 is the visible lower arm; its carrier shares node9/10 pin origins.
    const auto baseline=program(session.document);const auto p=drive(session.document,8,1,"rotate",20);verify(p);accepted(session.call("doc_apply",request(session,8,1,"rotate",20)));
    for(u64 node:{5,6,7,8,9,10,11,12,13,14,15,16})equal(pose(p.placements,node),pose(program(session.document).baseline,node),1e-7,"native suspension commit matches solved preview");
    accepted(session.call("doc_apply",request(session,8,1,"rotate",-20)));for(u64 node:{5,6,7,8,9,10,11,12,13,14,15,16})equal(pose(baseline.baseline,node),pose(program(session.document).baseline,node),1e-7,"exact suspension reverse keeps original rigid carriers");}
#endif
}
int main(){try{auto run=[](const char* name,auto test){try{test();}catch(const std::exception& error){throw std::runtime_error(std::string(name)+": "+error.what());}};
    run("suspension",testSuspension);run("slider-crank",testSliderCrank);run("transmission",testTransmissionDrag);
#ifdef ARK_MECHANISM_NATIVE_TESTS
    run("native suspension example",testNativeSuspensionExample);
#endif
    std::cout<<"closed assembly mechanisms and transmission drag passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
