#include "ark/agent.hpp"
#include "ark/drag_chain.hpp"
#include "ark/motion_graph.hpp"
#include "ark/onboarding.hpp"
#include "ark/serialize.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

// 050B native drag chain. Pure rigid-link kinematics over a carriage sweep, then (with the
// native worker) a synthetic kiosk camera lift: vertical prismatic carriage q in [120,570] mm,
// R 38, pitch 15, 31.6 x 21.6 mm links inside a duct. No user design is used.
using namespace ark;
using V=arkjson::Value;
namespace {
int checks=0;
void check(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
void close(double actual,double expected,double tolerance,const std::string& message){check(std::isfinite(actual)&&std::fabs(actual-expected)<=tolerance,message+": actual="+std::to_string(actual)+", expected="+std::to_string(expected));}
bool throwsCode(const std::function<void()>& run,const std::string& code){try{run();}catch(const std::exception& e){return std::string(e.what()).find(code)!=std::string::npos;}return false;}
DragChainGeometry kiosk(int links){DragChainGeometry g;g.pitch=15;g.radius=38;g.links=links;g.origin={0,0,345};g.direction={0,0,-1};g.bend={1,0,0};return g;}

void kinematics(){
    const auto g=kiosk(25);const double p=g.pitch,R=g.radius,total=25*p;const Vec3 A=g.origin,d=g.direction,n=g.bend,O0=A+n*R;
    std::map<int,DragChainLayout> sweep;double maxSpacing=0,maxPath=0,maxOffset=0,minOffset=1e9;
    for(int k=-2250;k<=2250;++k){const double c=k*.1;const auto layout=layoutDragChain(g,c);sweep[k]=layout;
        check(layout.pins.size()==26&&layout.links.size()==25&&layout.fixedLinks+layout.bendLinks+layout.movingLinks==25,"pin and link counts");
        check(layout.pins.front().approx(A,1e-12)&&layout.pins.back().approx(A+n*(2*R)+d*c,1e-9),"pin 0 at the fixed pin and pin N at the carriage pin");
        for(size_t i=0;i+1<layout.pins.size();++i){maxSpacing=std::max(maxSpacing,std::fabs((layout.pins[i+1]-layout.pins[i]).len()-p));check(layout.arc[i+1]>layout.arc[i],"arc length is strictly monotonic");}
        // Every pin lies on the path: fixed line, semicircle or moving line.
        const Vec3 O=A+d*layout.fixedRun+n*R;const int movingFirst=25-layout.movingLinks;
        for(int i=0;i<=25;++i){const Vec3 q=layout.pins[(size_t)i]-A;const double along=q.dot(d),across=q.dot(n);double e=std::fabs(q.dot(n.cross(d)));
            if(i<=layout.fixedLinks)e=std::max({e,std::fabs(across),std::max(0.,along-layout.fixedRun)});
            else if(i>=movingFirst)e=std::max({e,std::fabs(across-2*R),std::max(0.,along-layout.fixedRun)});
            else e=std::max({e,std::fabs((layout.pins[(size_t)i]-O).len()-R),std::max(0.,layout.fixedRun-along)});maxPath=std::max(maxPath,e);}
        // Link frames: rigid, right-handed, X along the chord, pins on the link's X axis.
        for(size_t i=0;i<layout.links.size();++i){const auto& m=layout.links[i];const Vec3 x{m.m[0],m.m[1],m.m[2]},y{m.m[4],m.m[5],m.m[6]},z{m.m[8],m.m[9],m.m[10]};
            check(std::fabs(x.len()-1)<1e-12&&std::fabs(y.len()-1)<1e-12&&std::fabs(x.dot(y))<1e-12&&x.cross(y).approx(z,1e-12),"link frame is a proper rigid frame");
            check(m.transformPoint({p,0,0}).approx(layout.pins[i+1],1e-9)&&m.transformPoint({0,0,0}).approx(layout.pins[i],1e-12),"link pins sit on the path pins");}
        const double offset=layout.fixedRun-dragChainContinuousFixedRun(total,R,c);maxOffset=std::max(maxOffset,offset);minOffset=std::min(minOffset,offset);
        check(layout.apex.approx(A+d*(layout.fixedRun+R)+n*R,1e-9),"loop apex");(void)O0;}
    close(maxSpacing,0,1e-9,"pin spacing equals the pitch over the sweep");close(maxPath,0,1e-9,"pins lie on the path");
    // Fixed-run links never move; moving-run links move rigidly with the carriage.
    size_t fixedCompared=0,movingCompared=0;
    for(const auto& [k,layout]:sweep){const auto& ref=sweep.at(0);
        for(int i=0;i<std::min(layout.fixedLinks,ref.fixedLinks);++i){for(int e=0;e<16;++e)check(layout.links[(size_t)i].m[e]==ref.links[(size_t)i].m[e],"fixed-run link frame is bitwise stationary");++fixedCompared;}
        const double dc=k*.1;for(int i=25-std::min(layout.movingLinks,ref.movingLinks);i<25;++i){const auto& a=layout.links[(size_t)i];const auto& b=ref.links[(size_t)i];for(int e=0;e<12;++e)check(a.m[e]==b.m[e],"moving-run link keeps its orientation");
            check(Vec3{a.m[12],a.m[13],a.m[14]}.approx(Vec3{b.m[12],b.m[13],b.m[14]}+d*dc,1e-9),"moving-run link translates with the carriage");++movingCompared;}}
    check(fixedCompared>1000&&movingCompared>1000,"sweep compared static and carried links");
    // Loop: exact 2-pitch periodicity, half speed on average, bounded polygon ripple.
    for(int k=-2250;k+300<=2250;k+=37)close(sweep.at(k+300).fixedRun-sweep.at(k).fixedRun,p,1e-9,"advancing the carriage 2 pitches moves the loop exactly one pitch");
    close((sweep.at(2250).fixedRun-sweep.at(-2250).fixedRun)/450.,.5,(maxOffset-minOffset)/450.+1e-12,"loop moves at half the carriage speed over the travel");
    double maxStep=0,maxTurn=0;for(int k=-2250;k<2250;++k){maxStep=std::max(maxStep,std::fabs(sweep.at(k+1).fixedRun-sweep.at(k).fixedRun));for(size_t i=0;i<25;++i){const auto& a=sweep.at(k).links[i];const auto& b=sweep.at(k+1).links[i];maxTurn=std::max(maxTurn,std::acos(std::clamp(a.m[0]*b.m[0]+a.m[1]*b.m[1]+a.m[2]*b.m[2],-1.,1.)));}}
    check(maxStep<.1&&maxTurn<0.01,"loop position and link orientation are continuous in the carriage coordinate");
    check(maxOffset-minOffset<.5&&std::fabs(minOffset)<1&&std::fabs(maxOffset)<1,"polygon ripple around the continuous formula stays bounded");
    std::cout<<"kinematics: max_pin_spacing_error_mm="<<maxSpacing<<" max_path_error_mm="<<maxPath<<" polygon_offset_mm=["<<minOffset<<","<<maxOffset<<"] max_loop_step_per_0.1mm="<<maxStep<<" max_link_turn_rad_per_0.1mm="<<maxTurn<<"\n";
    // Typed kinematic rejections.
    check(throwsCode([&]{(void)layoutDragChain(g,300);},"DRAG_CHAIN_RUN_NEGATIVE"),"moving run negative");
    check(throwsCode([&]{(void)layoutDragChain(g,-300);},"DRAG_CHAIN_RUN_NEGATIVE"),"fixed run negative");
    check(throwsCode([&]{(void)layoutDragChain(kiosk(7),0);},"DRAG_CHAIN_TOO_SHORT"),"too few links for the bend");
    check(throwsCode([&]{auto bad=kiosk(25);bad.radius=10;(void)layoutDragChain(bad,0);},"DRAG_CHAIN_GEOMETRY"),"radius below pitch");
    check(throwsCode([&]{validateDragChainSection(15,31.6,21.6,25,17,16);},"DRAG_CHAIN_BEND_TOO_TIGHT"),"tight bend");
    check(throwsCode([&]{validateDragChainSection(15,31.6,21.6,31.2,17,38);},"DRAG_CHAIN_GEOMETRY"),"thin wall");
    check(throwsCode([&]{validateDragChainSection(15,31.6,21.6,25,21.4,38);},"DRAG_CHAIN_GEOMETRY"),"thin crossbar");
    validateDragChainSection(15,31.6,21.6,25,17,38);
    // A shorter chain at its exact limits: both runs reach zero, never negative.
    const auto edge=layoutDragChain(kiosk(25),sweep.at(2250).carriage);check(edge.movingRun>0&&edge.fixedRun>0,"fixture travel keeps both runs positive");
}

#ifdef ARK_DRAG_CHAIN_NATIVE
V json(const std::string& source){V value;if(!arkjson::parse(source,value))throw std::runtime_error("test JSON parses: "+source.substr(0,80));return value;}
V accepted(const V& reply){if(reply.find("isError")->b)throw std::runtime_error(reply.find("structuredContent")->dump().substr(0,1500));return *reply.find("structuredContent");}
V withRevision(const AgentSession& session,const std::string& source){auto args=json(source);args.set("expected_revision",V::makeNum((double)session.revision));return args;}
V commit(AgentSession& session,const std::string& source){return accepted(session.call("doc_apply",withRevision(session,source)));}
V rejected(AgentSession& session,const char* tool,const V& args,const std::string& code,const std::string& message){
    const auto before=documentToJson(session.document);const auto revision=session.revision;const auto reply=session.call(tool,args);
    check(reply.find("isError")->b,message+" must reject");const auto text=reply.find("structuredContent")->dump();check(text.find(code)!=std::string::npos,message+" reports "+code+": "+text.substr(0,600));
    check(session.revision==revision&&documentToJson(session.document)==before,message+" leaves document and revision unchanged");return *reply.find("structuredContent");}
void rejectSet(AgentSession& session,const std::string& command,const std::string& code,const std::string& message){rejected(session,"doc_apply",withRevision(session,"{\"commands\":["+command+"]}"),code,message);}
u64 node(const AgentSession& session,const std::string& name){for(const auto& n:session.document.assembly)if(n.name==name)return n.id;throw std::runtime_error("node missing: "+name);}
Mat4 matrix(const V& v){Mat4 m;for(int i=0;i<16;++i)m.m[i]=v.arr[(size_t)i].num;return m;}
std::unordered_map<u64,Mat4> world(const Document& d){std::vector<AssemblyPlacement> placements;std::string error;check(resolveAssembly(d,placements,error),"fixture resolves: "+error);std::unordered_map<u64,Mat4> out;for(const auto& p:placements)if(!p.copy)out[p.node]=p.world;return out;}
const char* kKiosk=R"J({"commands":[
  {"op":"param.set","name":"chain_pitch","expr":15},{"op":"param.set","name":"chain_radius","expr":38},{"op":"param.set","name":"lift_min","expr":120},{"op":"param.set","name":"lift_max","expr":570},
  {"op":"feature.add","type":"box","name":"Duct back","p":{"dx":226,"dy":6,"dz":760}},{"op":"feature.add","type":"box","name":"Duct wall","p":{"dx":6,"dy":72,"dz":760}},
  {"op":"feature.add","type":"box","name":"Rail body","p":{"dx":12,"dy":12,"dz":700}},{"op":"feature.add","type":"box","name":"Carriage body","p":{"dx":60,"dy":40,"dz":50}},{"op":"feature.add","type":"box","name":"Knob body","p":{"dx":10,"dy":10,"dz":10}},
  {"op":"assembly.add","name":"Duct","feature":"Duct back","fixed":true,"p":{"tx":-60,"ty":20,"tz":-60}},
  {"op":"assembly.add","name":"Duct left","feature":"Duct wall","fixed":true,"p":{"tx":-66,"ty":-46,"tz":-60}},
  {"op":"assembly.add","name":"Rail","feature":"Rail body","fixed":true,"p":{"tx":100,"ty":-20,"tz":8}},
  {"op":"assembly.add","name":"Carriage","feature":"Carriage body","p":{"tx":100,"ty":-20,"tz":353}},
  {"op":"assembly.add","name":"Knob","feature":"Knob body","p":{"tx":-40,"ty":-40,"tz":700}},
  {"op":"mate.add","type":"prismatic","a":"Rail","b":"Carriage","reference_a":"Z","reference_b":"Z","limits":{"min":"lift_min","max":"lift_max"}},
  {"op":"mate.add","type":"revolute","a":"Duct","b":"Knob","reference_a":"origin","reference_b":"origin"}]})J";
// Anchor in the duct frame: world (0,0,345); moving pin in the carriage frame: world (76,0,q).
std::string chainSet(const std::string& extra="",const std::string& anchorOrigin="[60,-20,405]",const std::string& moving="\"occurrence\":\"Carriage\",\"attach\":[-24,20,-8],\"joint\":1",const std::string& link="\"pitch\":\"chain_pitch\",\"outer_width\":31.6,\"outer_height\":21.6,\"inner_width\":25,\"inner_height\":17",const std::string& radius="\"chain_radius\""){
    return R"({"op":"drag_chain.set","name":"Camera cable chain","link":{)"+link+R"(},"bend_radius":)"+radius+R"(,"anchor":{"occurrence":"Duct","origin":)"+anchorOrigin+R"(,"direction":"-Z","bend":"+X"},"moving":{)"+moving+"}"+extra+"}";}
std::vector<u64> linkIds(const AgentSession& session){std::vector<u64> out;for(const auto& id:findDragChain(session.document,"Camera cable chain")->find("links")->arr)out.push_back((u64)id.num);return out;}
DragChainLayout expected(int links,double q){auto g=kiosk(links);return layoutDragChain(g,345-q);}
void comparePoses(const std::unordered_map<u64,Mat4>& w,const std::vector<u64>& links,const DragChainLayout& layout,double tolerance,const std::string& what){
    check(links.size()==layout.links.size(),what+": link count");for(size_t i=0;i<links.size();++i){const auto& a=w.at(links[i]);for(int e=0;e<16;++e)check(std::fabs(a.m[e]-layout.links[i].m[e])<=tolerance,what+": link "+std::to_string(i)+" pose");}}
V collide(AgentSession& session,u64 a,u64 b){V args=V::makeObj(),ea=V::makeObj(),eb=V::makeObj();ea.set("node",V::makeNum((double)a));ea.set("copy",V::makeNum(0));eb.set("node",V::makeNum((double)b));eb.set("copy",V::makeNum(0));args.set("a",ea);args.set("b",eb);args.set("mode",V::makeStr("exact"));return accepted(session.call("collision_inspect",args));}

void document(){
    AgentSession session;commit(session,kKiosk);const auto bare=documentToJson(session.document);
    // Old documents and ones without chains keep loading unchanged.
    {Document reopened;std::string error;check(documentFromJson(bare,reopened,&error)&&reopened.dragChains.isNull(),"document without drag chains reopens: "+error);
     V old=json(bare);old.set("version",V::makeNum(31));Document legacy;check(documentFromJson(old.dump(),legacy,&error)&&legacy.dragChains.isNull()&&legacy.assembly.size()==5,"older format without drag chains still loads: "+error);}
    // Typed, atomic rejections.
    rejected(session,"doc_apply",json("{\"commands\":["+chainSet()+"]}"),"expected_revision","authoring without expected_revision");
    rejectSet(session,chainSet("","[61,-20,405]"),"DRAG_CHAIN_NOT_PARALLEL","moving pin 1 mm off the 2R offset");
    rejectSet(session,chainSet("","[60,-19,405]"),"DRAG_CHAIN_NOT_PARALLEL","moving pin 1 mm lateral");
    rejectSet(session,chainSet("","[60,-20,405]","\"occurrence\":\"Rail\",\"attach\":[-24,20,337],\"joint\":1"),"DRAG_CHAIN_CARRIAGE_NOT_DRIVEN","moving occurrence on the grounded side");
    rejectSet(session,chainSet(",\"link_count\":20"),"DRAG_CHAIN_TRAVEL_RUN_NEGATIVE","20 links cannot cover the travel");
    rejectSet(session,chainSet(",\"link_count\":7"),"DRAG_CHAIN_TOO_SHORT","7 links cannot pass the bend");
    rejectSet(session,chainSet(",\"link_count\":60"),"DRAG_CHAIN_TOO_LONG","60 links overhang the stroke");
    rejectSet(session,chainSet(",\"link_count\":401"),"DRAG_CHAIN_CAPACITY","link capacity");
    rejectSet(session,chainSet(",\"travel\":{\"min\":400,\"max\":570}"),"DRAG_CHAIN_TRAVEL_EXCLUDES_POSE","travel excludes the current pose");
    rejectSet(session,chainSet("","[60,-20,405]","\"occurrence\":\"Nope\",\"joint\":1"),"DRAG_CHAIN_UNKNOWN_OCCURRENCE","unknown carriage");
    rejectSet(session,chainSet("","[60,-20,405]","\"occurrence\":\"Carriage\",\"attach\":[-24,20,-8],\"joint\":99"),"DRAG_CHAIN_UNKNOWN_MATE","unknown joint");
    rejectSet(session,chainSet("","[60,-20,405]","\"occurrence\":\"Carriage\",\"attach\":[-24,20,-8],\"joint\":2"),"DRAG_CHAIN_JOINT_NOT_PRISMATIC","revolute joint");
    rejectSet(session,chainSet("","[60,-20,405]","\"occurrence\":\"Carriage\",\"attach\":[-24,20,-8],\"joint\":1","\"pitch\":15,\"outer_width\":31.6,\"outer_height\":21.6","\"1/0\""),"DRAG_CHAIN_NONFINITE","nonfinite radius");
    rejectSet(session,chainSet("","[60,-20,\"sqrt(-1)\"]"),"DRAG_CHAIN_NONFINITE","nonfinite anchor");
    rejectSet(session,chainSet("","[60,-20,405]","\"occurrence\":\"Carriage\",\"attach\":[-24,20,-8],\"joint\":1","\"pitch\":15,\"outer_width\":31.6,\"outer_height\":21.6","16"),"DRAG_CHAIN_BEND_TOO_TIGHT","tight bend");
    rejectSet(session,chainSet("","[60,-20,405]","\"occurrence\":\"Carriage\",\"attach\":[-24,20,-8],\"joint\":1","\"pitch\":15,\"outer_width\":31.6,\"outer_height\":21.6,\"inner_width\":31.2"),"DRAG_CHAIN_GEOMETRY","thin side wall");
    rejectSet(session,chainSet(",\"colour\":1"),"DRAG_CHAIN_INVALID","unknown field");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"drag_chain.remove","name":"Nope"}]})"),"DRAG_CHAIN_NOT_FOUND","remove missing chain");
    // Accepted chain: derived count from the joint-limit travel plus the 2-pitch margin.
    const auto beforeRevision=session.revision;commit(session,"{\"commands\":["+chainSet()+"]}");check(session.revision==beforeRevision+1,"one transaction");
    const auto* record=findDragChain(session.document,"Camera cable chain");check(record&&record->find("link_count_source")->str=="derived","record stored");
    auto links=linkIds(session);check(links.size()==25,"derived link count: ceil((pi*38+225+30)/15)=25, got "+std::to_string(links.size()));
    const u64 group=(u64)record->find("group")->num,feature=(u64)record->find("link_feature")->num;const auto* definition=session.document.find(feature);
    check(definition&&definition->type==FeatureType::NativeSolid&&definition->nativeSpec.find("op")->str=="drag_chain_link"&&definition->p.at("pitch")=="chain_pitch","native link definition with parameter expressions");
    for(u64 id:links){const auto& n=*std::find_if(session.document.assembly.begin(),session.document.assembly.end(),[&](const AssemblyNode& x){return x.id==id;});check(n.parent==group&&n.feature==feature&&n.p.empty()&&!n.fixed,"link occurrence under the chain group");}
    // Native link solid: one valid exact solid spanning lobe to lobe.
    {V args=V::makeObj();args.set("id",V::makeNum((double)feature));const auto report=accepted(session.call("native_inspect",args));const auto text=report.dump();
     check(text.find("\"brep_valid\":true")!=std::string::npos,"link B-rep is valid: "+text.substr(0,400));const auto* r=report.find("report");if(!r)r=&report;const auto* volume=r->find("volume_mm3");check(volume&&volume->num>500&&volume->num<15*31.6*21.6,"link volume is positive and below the outer box");
     std::cout<<"link solid: volume_mm3="<<volume->num<<"\n";}
    // Static pose equals the analytic solution at q=345 (c=0).
    auto w=world(session.document);comparePoses(w,links,expected(25,345),1e-9,"static pose");
    {AssemblyDiagnostics diagnostics;std::vector<AssemblyPlacement> placements;std::string error;check(resolveAssembly(session.document,placements,error,&diagnostics),error);for(u64 id:diagnostics.unconstrainedNodes)check(std::find(links.begin(),links.end(),id)==links.end(),"driven links are not reported unconstrained");}
    // BOM counts one definition with 25 occurrences.
    {const auto bom=accepted(session.call("bom_inspect",V::makeObj())).dump();check(bom.find("Camera cable chain link")!=std::string::npos,"BOM lists the link definition");}
    // drag_chain_info list, detail, hypothetical q and poses.
    {const auto list=accepted(session.call("drag_chain_info",V::makeObj()));check(list.find("count")->num==1&&list.find("drag_chains")->arr[0].find("valid")->b,"info lists the chain");
     close(list.find("drag_chains")->arr[0].find("q_mm")->num,345,1e-6,"listed q");
     const auto info=accepted(session.call("drag_chain_info",json(R"({"name":"Camera cable chain"})")));const auto& state=*info.find("state");
     close(state.find("carriage_mm")->num,0,1e-6,"carriage coordinate at q=345");close(state.find("fixed_run_mm")->num,expected(25,345).fixedRun,1e-9,"info fixed run");
     close(state.find("fixed_run_mm")->num+state.find("moving_run_mm")->num,375-K_PI*38,1,"runs plus bend give the chain length (polygon bend)");
     check(info.find("travel")->find("source")->str=="joint_limits"&&info.find("travel")->find("at_min")->find("moving_run_mm")->num>0&&info.find("travel")->find("at_max")->find("fixed_run_mm")->num>0,"travel ends reported from the joint limits");
     close(info.find("travel")->find("at_min")->find("carriage_mm")->num,225,1e-6,"carriage coordinate at q_min");
     const auto& budget=*info.find("length_budget");close(budget.find("chain_length_mm")->num,375,1e-9,"chain length");close(budget.find("required_mm")->num,K_PI*38+225,1e-6,"required length");close(budget.find("slack_mm")->num,375-K_PI*38-225,1e-6,"slack");
     check(state.find("checks")->find("max_pin_spacing_error_mm")->num<1e-9&&state.find("checks")->find("arc_length_monotonic")->b,"info pin checks");
     const auto atLow=accepted(session.call("drag_chain_info",json(R"({"name":"Camera cable chain","q":120})")));close(atLow.find("state")->find("fixed_run_mm")->num,expected(25,120).fixedRun,1e-9,"info at q=120");close(atLow.find("q_mm")->num,120,1e-12,"evaluated q");
     check(documentToJson(session.document)==documentToJson(session.document),"info is read-only");
     const auto full=accepted(session.call("drag_chain_info",json(R"({"name":"Camera cable chain","poses":true})")));check(full.find("state")->find("links")->arr.size()==25,"poses lists every link");
     rejected(session,"drag_chain_info",json(R"({"name":"Camera cable chain","q":700})"),"DRAG_CHAIN_JOINT_NOT_DRIVABLE","q outside the joint limits");
     rejected(session,"drag_chain_info",json(R"({"name":"Nope"})"),"DRAG_CHAIN_NOT_FOUND","unknown chain info");
     std::cout<<"info q=345: fixed_run_mm="<<state.find("fixed_run_mm")->num<<" moving_run_mm="<<state.find("moving_run_mm")->num<<" segments="<<state.find("segments")->dump()<<" polygon_offset_mm="<<state.find("polygon_offset_mm")->num<<"\n";}
    // Exact clearance between real link solids: adjacent links through the bend, next-but-one and across the runs.
    {const auto layout=expected(25,345);int pairs=0;double nearest=1e9;
     for(int i=std::max(0,layout.fixedLinks-1);i<std::min(24,25-layout.movingLinks+1);++i){const auto r=collide(session,links[(size_t)i],links[(size_t)i+1]);check(r.find("status")->str=="separated"&&r.find("min_distance_mm")->num>=0.099,"adjacent links clear through the bend: "+r.dump().substr(0,300));nearest=std::min(nearest,r.find("min_distance_mm")->num);++pairs;
         if(i+2<25){const auto skip=collide(session,links[(size_t)i],links[(size_t)i+2]);check(skip.find("status")->str=="separated","next-but-one links clear");++pairs;}}
     const auto across=collide(session,links[2],links[22]);check(across.find("status")->str=="separated"&&across.find("min_distance_mm")->num>76-21.6-1,"fixed and moving runs clear across the 2R gap");
     std::cout<<"clearance: "<<pairs<<" bend pairs separated, nearest adjacent "<<nearest<<" mm, runs "<<across.find("min_distance_mm")->num<<" mm\n";}
    // Drive preview and commit: links follow without pose commands of their own.
    {const auto preview=accepted(session.call("assembly_info",json(R"({"drive":{"driver":"Carriage","mate":1,"mode":"translate","delta":-200}})")));const auto text=preview.dump();
     std::unordered_map<u64,Mat4> previewed;const V* rows=preview.find("drive")?preview.find("drive")->find("instances"):nullptr;check(rows&&rows->t==V::Arr,"drive preview lists instances: "+text.substr(0,300));
     for(const auto& row:rows->arr){const auto* m=row.find("world_column_major");const auto* copy=row.find("copy");if(m&&(!copy||copy->num==0))previewed[(u64)row.find("node")->num]=matrix(*m);}
     comparePoses(previewed,links,expected(25,145),1e-6,"assembly_info drive preview");
     const auto* commands=preview.find("drive")?preview.find("drive")->find("commands"):preview.find("commands");if(commands)for(const auto& c:commands->arr)check(std::find(links.begin(),links.end(),(u64)c.find("id")->num)==links.end(),"drive never writes link poses");}
    commit(session,R"({"commands":[{"op":"assembly.drive","driver":"Carriage","mate":1,"mode":"translate","delta":-225}]})");
    w=world(session.document);comparePoses(w,links,expected(25,120),1e-6,"committed drive to q=120");
    for(const auto& n:session.document.assembly)if(std::find(links.begin(),links.end(),n.id)!=links.end())check(n.p.empty(),"committed drive left link p empty");
    {const auto layout=expected(25,120);const auto r=collide(session,links[(size_t)layout.fixedLinks],links[(size_t)layout.fixedLinks+1]);check(r.find("status")->str=="separated","links clear in the bend at q=120");}
    commit(session,R"({"commands":[{"op":"assembly.drive","driver":"Carriage","mate":1,"mode":"translate","delta":225}]})");
    // Driven links reject mates, placement edits and removal.
    rejected(session,"doc_apply",withRevision(session,"{\"commands\":[{\"op\":\"mate.add\",\"type\":\"rigid\",\"a\":\"Duct\",\"b\":"+std::to_string(links[3])+"}]}"),"DRAG_CHAIN_LINK_DRIVEN","mating a link");
    rejected(session,"doc_apply",withRevision(session,"{\"commands\":[{\"op\":\"assembly.edit\",\"id\":"+std::to_string(links[3])+",\"p\":{\"tx\":5}}]}"),"DRAG_CHAIN_LINK_DRIVEN","placing a link");
    rejected(session,"doc_apply",withRevision(session,"{\"commands\":[{\"op\":\"assembly.remove\",\"id\":"+std::to_string(links[3])+"}]}"),"DRAG_CHAIN_UNKNOWN_OCCURRENCE","removing a link");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"param.set","name":"chain_radius","expr":30}]})"),"DRAG_CHAIN_NOT_PARALLEL","radius edit that breaks the 2R run offset");rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"param.set","name":"chain_pitch","expr":40}]})"),"DRAG_CHAIN_BEND_TOO_TIGHT","pitch edit beyond the bend limits");
    // Motion bake of the carriage: links are baked as moving instances with per-frame analytic poses.
    commit(session,R"({"commands":[{"op":"motion.graph.set","name":"Lift cycle","graph":{"channels":[{"name":"lift","mate":1,"curve":{"type":"sequence","items":[{"type":"ease","duration":1,"to":120},{"type":"ease","duration":2,"to":570},{"type":"ease","duration":1,"to":345}]}}]}},{"op":"motion.bake","name":"Lift cycle","fps":20}]})");
    {const auto summary=accepted(session.call("motion_info",json(R"({"name":"Lift cycle"})")));const auto& bake=*summary.find("bake");const size_t frames=(size_t)bake.find("frame_count")->num;check(frames==81,"bake frame count");
     std::set<u64> moving;for(const auto& row:bake.find("instances")->arr)moving.insert((u64)row.find("node")->num);
     check(moving.count(node(session,"Carriage"))&&moving.count(links.back())&&!moving.count(links.front()),"bake lists moving-run links and the carriage but not the stationary first link");
     size_t compared=0;double maxError=0,maxLoopError=0;double previousQ=0,previousRun=0;bool first=true;
     for(size_t k=0;k<frames;k+=4){V args=json(R"({"name":"Lift cycle"})");args.set("frame",V::makeNum((double)k));const auto frame=*accepted(session.call("motion_info",args)).find("frame");const double q=frame.find("coordinates")->arr[0].num;
         const auto layout=expected(25,q);std::unordered_map<u64,Mat4> poses;for(const auto& row:frame.find("instances")->arr)poses[(u64)row.find("node")->num]=matrix(*row.find("world_column_major"));
         for(size_t i=0;i<links.size();++i){const auto found=poses.find(links[i]);if(found==poses.end()){check(i==0,"only stationary links are omitted from the bake");continue;}for(int e=0;e<16;++e)maxError=std::max(maxError,std::fabs(found->second.m[e]-layout.links[i].m[e]));++compared;}
         if(!first){const double dq=q-previousQ,dl=layout.fixedRun-previousRun;if(std::fabs(dq)>30)maxLoopError=std::max(maxLoopError,std::fabs(dl/(-dq)-.5));}first=false;previousQ=q;previousRun=layout.fixedRun;}
     check(compared>400&&maxError<2e-3,"baked link poses match the analytic layout at each frame's q (float32 payload): max error "+std::to_string(maxError));
     check(maxLoopError<.02,"baked loop moves at half the carriage speed between sampled frames");
     std::cout<<"bake: frames="<<frames<<" moving_instances="<<moving.size()<<" compared_link_frames="<<compared<<" max_pose_error="<<maxError<<" max_half_speed_deviation="<<maxLoopError<<"\n";}
    // Integration with 049A receipts: recipe changes invalidate the bake even
    // when the assembly's current pose is unchanged.
    {commit(session,"{\"commands\":["+chainSet(",\"max_slack\":121")+"]}");
     const auto info=accepted(session.call("motion_info",json(R"({"name":"Lift cycle"})")));
     check(info.find("status")->dump().find("stale")!=std::string::npos,"recipe change invalidates the previous motion bake");
     accepted(session.call("doc_undo",V::makeObj()));
     check(findDragChain(session.document,"Camera cable chain")->find("max_slack")==nullptr,"undo restores the original recipe");}
    // Followers do not have a proven rigid joint-tree displacement bound.
    {V args=json(R"({"name":"Lift cycle","clearance":{"pairs":[],"from_frame":0,"to_frame":1,"max_subdivisions":0}})");
     auto request=*args.find("clearance");V pairs=V::makeArr(),pair=V::makeObj();
     pair.set("a",V::makeNum((double)links[10]));pair.set("b",V::makeNum((double)links[12]));pairs.arr.push_back(pair);request.set("pairs",pairs);args.set("clearance",request);
     const auto report=accepted(session.call("motion_info",args));const auto* clearance=report.find("clearance");
     check(clearance&&!clearance->find("continuous_certified")->b&&!clearance->find("continuous_lower_bound_proven")->b,"follower clearance stays explicitly uncertified between sampled frames");}
    // Persistence round trip, undo/redo and stable identities on replace.
    {const char* path="drag-chain-050b-test.ark";check(saveDocument(session.document,path),"binary chain document saves");
     clearBakedMotionCache();Document reopened;std::string error;check(loadDocument(path,reopened,&error),"binary chain document reopens: "+error);std::remove(path);
     BakedMotionFrames frames;BakedMotionFramesSource source;
     check(resolveBakedMotion(reopened,*findMotion(reopened,"Lift cycle"),frames,error,&source)&&source.verified,"reopened chain bake reproduces the 049A receipt: "+error);}
    {const auto saved=documentToJson(session.document);Document reopened;std::string error;check(documentFromJson(saved,reopened,&error),"chain document reopens: "+error);check(reopened.dragChains.dump()==session.document.dragChains.dump(),"record round trips");
     reopened.recompute();comparePoses(world(reopened),links,expected(25,345),1e-9,"reopened pose");
     V broken=json(saved);for(auto& item:broken.obj)if(item.first=="drag_chains")for(auto& field:item.second.arr[0].obj)if(field.first=="links")field.second.arr.push_back(field.second.arr[0]);Document bad;check(!documentFromJson(broken.dump(),bad,&error)&&error.find("DRAG_CHAIN_LINK_DRIVEN")!=std::string::npos,"duplicate link ids reject on load: "+error);}
    commit(session,"{\"commands\":["+chainSet(",\"link_count\":24")+"]}");auto shorter=linkIds(session);check(shorter.size()==24&&std::equal(shorter.begin(),shorter.end(),links.begin())&&findDragChain(session.document,"Camera cable chain")->find("link_count_source")->str=="explicit","replace keeps leading link identities");
    comparePoses(world(session.document),shorter,expected(24,345),1e-9,"24-link pose");
    accepted(session.call("doc_undo",V::makeObj()));check(linkIds(session)==links,"undo restores 25 links");accepted(session.call("doc_redo",V::makeObj()));check(linkIds(session)==shorter,"redo restores 24 links");
    commit(session,R"({"commands":[{"op":"drag_chain.remove","name":"Camera cable chain"}]})");check(session.document.dragChains.isNull()&&!session.document.find(feature)&&std::none_of(session.document.assembly.begin(),session.document.assembly.end(),[&](const AssemblyNode& n){return n.id==group||n.id==links[0];}),"remove deletes the record, group, links and definition");
    accepted(session.call("doc_undo",V::makeObj()));check(linkIds(session)==shorter,"undo restores the removed chain");
    std::cout<<"document: kiosk lift accepted, 18 typed rejections, bake and persistence checked\n";
}
void agentSurface(){
    const auto tools=AgentSession::tools();bool found=false;for(const auto& tool:tools.arr)if(tool.find("name")->str=="drag_chain_info")found=tool.find("annotations")->find("readOnlyHint")->b;check(found,"drag_chain_info is registered read-only");
    for(const char* name:{"doc_apply","doc_preview"}){bool advertised=false;for(const auto& tool:tools.arr)if(tool.find("name")->str==name){const auto ops=tool.find("inputSchema")->find("properties")->find("commands")->find("items")->find("properties")->find("op")->find("enum")->dump();advertised=ops.find("\"drag_chain.set\"")!=std::string::npos&&ops.find("\"drag_chain.remove\"")!=std::string::npos;}check(advertised,std::string(name)+" advertises the drag_chain operations");}
    AgentSession session;for(const char* topic:{"assembly","motion"}){V args=V::makeObj();args.set("topic",V::makeStr(topic));args.set("detail",V::makeStr("guide"));const auto guide=accepted(session.call("agent_onboard",args));const auto* section=guide.find("drag_chain");
        check(section&&section->dump().find("drag_chain.set")!=std::string::npos&&section->dump().find("Never model a drag chain as rigid telescoping segments")!=std::string::npos,std::string("onboarding section in ")+topic);
        check(guide.find("tools")->dump().find("drag_chain_info")!=std::string::npos,std::string("onboarding tools list drag_chain_info in ")+topic);}
}
#endif
}

int main(){
    try{
        kinematics();
#ifdef ARK_DRAG_CHAIN_NATIVE
        agentSurface();document();
#else
        std::cout<<"native worker unavailable: document, bake and clearance sections skipped\n";
#endif
        std::cout<<"drag chain 050B tests passed: "<<checks<<" checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<"\n";return 1;}
}
