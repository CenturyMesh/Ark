#include "ark/agent.hpp"
#include "ark/motion_graph.hpp"
#include "ark/onboarding.hpp"
#include "ark/serialize.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace ark;
using V=arkjson::Value;
namespace {
int checks=0;
void check(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
void close(double actual,double expected,double tolerance,const std::string& message){check(std::isfinite(actual)&&std::fabs(actual-expected)<=tolerance,message+": actual="+std::to_string(actual)+", expected="+std::to_string(expected));}
V json(const std::string& source){V value;if(!arkjson::parse(source,value))throw std::runtime_error("test JSON parses: "+source.substr(0,80));return value;}
V accepted(const V& reply){if(reply.find("isError")->b)throw std::runtime_error(reply.find("structuredContent")->dump());return *reply.find("structuredContent");}
V withRevision(const AgentSession& session,const char* source){auto args=json(source);args.set("expected_revision",V::makeNum((double)session.revision));return args;}
V apply(AgentSession& session,const char* source){return accepted(session.call("doc_apply",withRevision(session,source)));}
void rejected(AgentSession& session,const char* tool,const V& args,const char* code,const std::string& message){
    const auto before=documentToJson(session.document);const auto revision=session.revision;const auto reply=session.call(tool,args);
    check(reply.find("isError")->b,message+" must reject");check(reply.find("structuredContent")->dump().find(code)!=std::string::npos,message+" reports "+code+": "+reply.find("structuredContent")->dump().substr(0,400));
    check(session.revision==revision&&documentToJson(session.document)==before,message+" leaves document and revision unchanged");}
CompiledMotion compile(const char* graph,std::map<std::string,double> starts={},ExprEnv env={}){CompiledMotion out;std::string error;check(compileMotionGraph(json(graph),env,starts,out,error),error);return out;}
std::string compileError(const char* graph){CompiledMotion out;std::string error;check(!compileMotionGraph(json(graph),ExprEnv{},{},out,error),std::string("graph must reject: ")+graph);return error;}
double at(const CompiledMotion& m,double t,size_t channel=0,double* slope=nullptr,int side=1){std::vector<double> slopes;const auto values=evaluateMotion(m,t,&slopes,side);if(slope)*slope=slopes[channel];return values[channel];}
const V& channelDiagnostics(const V& diagnostics,const char* name){for(const auto& row:diagnostics.find("channels")->arr)if(row.find("name")->str==name)return row;throw std::runtime_error(std::string("diagnostics channel missing: ")+name);}

void testSegmentsAndComposition(){
    auto glass=compile(R"({"channels":[{"name":"glass","from":0,"curve":{"type":"sequence","items":[{"type":"ease","duration":2,"to":-420},{"type":"hold","duration":0.5},{"type":"ease","duration":2,"to":0}]}}]})");
    close(glass.duration,4.5,1e-12,"sequence duration sums segments");close(at(glass,1),-210,1e-9,"ease midpoint");close(at(glass,2),-420,1e-9,"first ease reaches its target");close(at(glass,2.25),-420,1e-12,"hold keeps chained value");close(at(glass,4.5),0,1e-9,"return segment ends at zero");
    double slope=0;at(glass,0,0,&slope);close(slope,0,1e-12,"ease in_out starts at rest");at(glass,1,0,&slope);close(slope,-315,1e-9,"ease in_out peak velocity 1.5*delta/duration");
    auto diagnostics=motionDiagnostics(glass);const auto& row=channelDiagnostics(diagnostics,"glass");check(row.find("continuity")->str=="C1","eased glass motion is C1 continuous");check(row.find("loop_continuity")->str=="C1","down/up glass loops continuously");close(row.find("min")->num,-420,1e-9,"sampled range minimum");close(row.find("max")->num,0,1e-9,"sampled range maximum");
    auto linearBezier=compile(R"({"channels":[{"name":"x","from":10,"curve":{"type":"bezier","duration":3,"to":40,"c1":[1,20],"c2":[2,30]}}]})");
    for(double t:{0.,.37,1.1,2.5,3.})close(at(linearBezier,t),10+10*t,1e-9,"collinear Bezier control points reproduce linear time");
    auto easeBezier=compile(R"({"channels":[{"name":"x","from":0,"curve":{"type":"bezier","duration":1,"to":100,"c1":[0.42,0],"c2":[0.58,100]}}]})");
    double previous=-1;for(int i=0;i<=100;++i){const double v=at(easeBezier,i/100.);check(v>=previous-1e-12,"Bezier timing stays monotonic");previous=v;}close(at(easeBezier,.5),50,1e-9,"symmetric Bezier passes its midpoint");at(easeBezier,0,0,&slope);close(slope,0,1e-9,"flat Bezier start tangent");
    check(compileError(R"({"channels":[{"name":"x","from":0,"curve":{"type":"bezier","duration":1,"to":1,"c1":[0,0],"c2":[0.5,1]}}]})").find("MOTION_BEZIER_NOT_MONOTONIC")!=std::string::npos,"Bezier control time on the boundary rejects");
    auto wrist=compile(R"({"channels":[{"name":"w","from":0,"curve":{"type":"ping_pong","count":2,"item":{"type":"linear","duration":1,"to":40}}}]})");
    close(wrist.duration,4,1e-12,"ping-pong doubles each repetition");for(auto [t,v]:std::initializer_list<std::pair<double,double>>{{.5,20},{1,40},{1.5,20},{2,0},{2.5,20},{3,40},{4,0}})close(at(wrist,t),v,1e-9,"ping-pong value at "+std::to_string(t));
    at(wrist,1.5,0,&slope);close(slope,-40,1e-9,"mirrored half reverses velocity");const auto wristDiagnostics=motionDiagnostics(wrist);const auto& pp=channelDiagnostics(wristDiagnostics,"w");check(pp.find("continuity")->str=="C0"&&pp.find("slope_breaks")->num==3,"linear ping-pong reports C0 with three velocity reversals");
    auto delayed=compile(R"({"channels":[{"name":"x","from":5,"curve":{"type":"delay","seconds":0.5,"item":{"type":"linear","duration":1,"to":15}}}]})");close(at(delayed,.25),5,1e-12,"delay holds start");close(at(delayed,1),10,1e-9,"delay shifts the item");close(delayed.duration,1.5,1e-12,"delay adds time");
    auto fast=compile(R"({"channels":[{"name":"x","from":0,"curve":{"type":"time_scale","factor":2,"item":{"type":"linear","duration":2,"to":10}}}]})");close(fast.duration,1,1e-12,"time scale shortens duration");at(fast,.5,0,&slope);close(slope,10,1e-9,"time scale multiplies velocity");close(at(fast,.5),5,1e-9,"time scale value");
    auto fitted=compile(R"({"channels":[{"name":"x","from":0,"curve":{"type":"time_scale","duration":4,"item":{"type":"linear","duration":2,"to":10}}}]})");close(fitted.duration,4,1e-12,"time scale fits a target duration");
    auto backwards=compile(R"({"channels":[{"name":"x","from":0,"curve":{"type":"reverse","item":{"type":"linear","duration":2,"to":10}}}]})");close(at(backwards,0),10,1e-12,"reverse starts at item end");close(at(backwards,1.5),2.5,1e-9,"reverse mirrors time");
    auto layered=compile(R"({"channels":[{"name":"x","from":0,"curve":{"type":"parallel","items":[{"type":"linear","duration":2,"to":20},{"type":"sine","duration":1,"amplitude":2,"period_s":1}]}}]})");close(at(layered,.25),2.5+2,1e-9,"parallel add layers a zero-centred oscillation");close(at(layered,1.5),15,1e-9,"finished layer holds its end");
    auto scaled=compile(R"({"channels":[{"name":"x","from":1,"curve":{"type":"offset","value":10,"gain":2,"item":{"type":"linear","duration":1,"to":3}}}]})");close(at(scaled,.5),2*2+10,1e-9,"offset applies gain and value");
    auto looped=compile(R"({"channels":[{"name":"x","from":0,"curve":{"type":"repeat","count":3,"item":{"type":"linear","duration":1,"to":1}}}]})");close(at(looped,2.5),.5,1e-9,"repeat replays its item");check(channelDiagnostics(motionDiagnostics(looped),"x").find("continuity")->str=="discontinuous","repeated ramp reports value jumps");
    auto spring=compile(R"({"channels":[{"name":"x","from":0,"curve":{"type":"spring","duration":3,"to":100,"frequency_hz":1,"damping":0.2}}]})");close(at(spring,0),0,1e-12,"spring starts at from");at(spring,0,0,&slope);close(slope,0,1e-12,"spring starts at rest");check(channelDiagnostics(motionDiagnostics(spring),"x").find("max")->num>100,"underdamped spring overshoots");
    auto expression=compile(R"J({"channels":[{"name":"x","from":2,"curve":{"type":"function","duration":2,"expr":"from+3*u+amp*sin(pi*t)"}}]})J",{},[]{ExprEnv env;env.vars["amp"]=0;return env;}());close(at(expression,1),3.5,1e-9,"function segment evaluates t, u, from and parameters");
    auto sequenced=compile(R"({"channels":[{"name":"x","from":0,"curve":{"type":"sequence","items":[{"type":"linear","duration":1,"to":10},{"type":"hold","duration":1}]}}]})");const auto sequencedDiagnostics=motionDiagnostics(sequenced);const auto& c0=channelDiagnostics(sequencedDiagnostics,"x");check(c0.find("continuity")->str=="C0"&&c0.find("breaks")->arr[0].find("kind")->str=="C1","velocity step is reported as a C1 break");
    auto jump=compile(R"({"channels":[{"name":"x","from":0,"curve":{"type":"sequence","items":[{"type":"linear","duration":1,"to":10},{"type":"linear","duration":1,"from":20,"to":30}]}}]})");auto jumpDiagnostics=motionDiagnostics(jump);check(jumpDiagnostics.find("discontinuity")&&std::fabs(jumpDiagnostics.find("discontinuity")->find("value_jump")->num-10)<1e-9,"explicit from creates a reported C0 jump");close(at(jump,1,0,nullptr,-1),10,1e-12,"left limit at a seam");close(at(jump,1,0,nullptr,1),20,1e-12,"right limit at a seam");
    auto handles=motionBezierHandles(compile(R"({"channels":[{"name":"x","from":0,"curve":{"type":"sequence","items":[{"type":"hold","duration":1},{"type":"bezier","duration":2,"to":8,"c1":[0.5,0],"c2":[1.5,8]}]}}]})"),0);
    check(handles.size()==1,"Bezier handle extracted from a sequence");close(handles[0].t0,1,1e-12,"handle start time is global");close(handles[0].t1,1.5,1e-12,"control time mapped");close(handles[0].t3,3,1e-12,"handle end time");close(handles[0].v3,8,1e-12,"handle end value");
    auto limits=motionDiagnostics(spring,{{true,0,100}});check(limits.find("limit_violation")!=nullptr,"spring overshoot violates a [0,100] limit");
}
void testCouplingsAndRejections(){
    auto coupled=compile(R"({"channels":[{"name":"elbow","expr":"0.5*shoulder+10"},{"name":"shoulder","from":0,"curve":{"type":"linear","duration":2,"to":40}}]})");
    double slope=0;close(at(coupled,1,0,&slope),20,1e-9,"coupling evaluates after its dependency");close(slope,10,1e-6,"coupling velocity follows the chain rule");check(coupled.order.size()==2&&coupled.order[0]==1,"dependency order evaluates shoulder first");
    check(compileError(R"({"channels":[{"name":"a","expr":"b+1"},{"name":"b","expr":"a+1"},{"name":"c","from":0,"curve":{"type":"hold","duration":1}}]})").find("MOTION_COUPLING_CYCLE")!=std::string::npos,"coupling cycles reject");
    check(compileError(R"({"channels":[{"name":"a","expr":"missing_value+1"},{"name":"c","from":0,"curve":{"type":"hold","duration":1}}]})").find("MOTION_INVALID_EXPRESSION")!=std::string::npos,"unknown coupling identifiers reject");
    check(compileError(R"J({"channels":[{"name":"x","from":0,"curve":{"type":"function","duration":1,"expr":"1/(t-t)"}}]})J").find("MOTION_")!=std::string::npos,"nonfinite function rejects");
    check(compileError(R"({"channels":[{"name":"x","from":0,"curve":{"type":"keyframes","duration":1}}]})").find("unknown node type")!=std::string::npos,"keyframe lists are not a node type");
    check(compileError(R"({"channels":[{"name":"x","from":0,"curve":{"type":"linear","duration":1,"to":1,"tension":2}}]})").find("unknown linear field")!=std::string::npos,"unknown segment fields reject");
    check(compileError(R"({"channels":[{"name":"x","from":0,"curve":{"type":"linear","duration":0,"to":1}}]})").find("duration")!=std::string::npos,"zero duration rejects");
    check(compileError(R"({"channels":[{"name":"x y","from":0,"curve":{"type":"hold","duration":1}}]})").find("identifier")!=std::string::npos,"channel names must be identifiers");
    check(compileError(R"({"channels":[{"name":"x","curve":{"type":"hold","duration":1},"mate":3}]})").find("MOTION_START_UNKNOWN")!=std::string::npos,"bound channel without a measured start rejects");
    check(compileError(R"({"channels":[{"name":"x","from":0,"curve":{"type":"sequence","items":[]}}]})").find("1..256 items")!=std::string::npos,"empty sequence rejects");
}
void glassFixture(AgentSession& session){apply(session,R"({"commands":[
  {"op":"param.set","name":"glass_travel","expr":420},
  {"op":"feature.add","type":"box","name":"Rail body","p":{"dx":20,"dy":20,"dz":600}},
  {"op":"feature.add","type":"box","name":"Glass pane","p":{"dx":800,"dy":5,"dz":450}},
  {"op":"feature.add","type":"box","name":"Carriage body","p":{"dx":40,"dy":20,"dz":30}},
  {"op":"assembly.add","name":"Rails","feature":"Rail body","fixed":true},
  {"op":"assembly.add","name":"Glass","feature":"Glass pane"},
  {"op":"assembly.add","name":"Carriage","feature":"Carriage body"},
  {"op":"mate.add","type":"prismatic","a":"Rails","b":"Glass","reference_a":"Z","reference_b":"Z","limits":{"min":"-glass_travel","max":0}},
  {"op":"mate.add","type":"rigid","a":"Glass","b":"Carriage"}]})");}
const char* kGlassMotion=R"({"commands":[{"op":"motion.graph.set","name":"Window down and up","graph":{"channels":[{"name":"glass","mate":1,"from":0,"curve":{"type":"sequence","items":[{"type":"ease","duration":2,"to":"-glass_travel"},{"type":"hold","duration":0.5},{"type":"ease","duration":2,"to":0}]}}]}},{"op":"motion.bake","name":"Window down and up","fps":30}]})";
int version(const Document& d){return (int)json(documentToJson(d,true,false)).find("version")->num;}
const V& motionRow(const V& list,const char* name){for(const auto& row:list.find("motions")->arr)if(row.find("name")->str==name)return row;throw std::runtime_error(std::string("motion row missing: ")+name);}
void testGlassBakeAndStale(){
    AgentSession session;glassFixture(session);const int baseVersion=version(session.document);check(baseVersion==31,"prismatic glass fixture keeps document version 31");
    const auto assemblyBefore=documentToJson(session.document);
    rejected(session,"doc_apply",json(kGlassMotion),"requires expected_revision","motion authoring without expected_revision");
    auto stale=json(kGlassMotion);stale.set("expected_revision",V::makeNum((double)session.revision+5));rejected(session,"doc_apply",stale,"Revision conflict","stale expected_revision");
    auto preview=accepted(session.call("doc_preview",withRevision(session,kGlassMotion)));check(preview.find("candidate_id")&&session.document.motions.isNull(),"doc_preview evaluates a bake without committing it");
    const u64 bakedAt=session.revision;apply(session,kGlassMotion);check(session.revision==bakedAt+1,"graph and bake commit as one revision");
    const auto* motion=findMotion(session.document,"Window down and up");check(motion&&motion->find("bake"),"bake persisted with its graph");
    const auto& bake=*motion->find("bake");check(bake.find("fps")->num==30&&bake.find("frame_count")->num==136&&bake.find("source_revision")->num==(double)bakedAt,"bake records rate, 136 frames for 4.5 s and its source revision");
    check(bake.find("solver")->find("rigid_side_steps")->num==120&&bake.find("solver")->find("constrained_steps")->num==0,"glass bake drives 120 rigid-side steps; held frames need none");
    check(bake.find("instances")->arr.size()==2,"only the glass and its rigid carriage are stored as moving instances");check(!bake.find("clearance_checked")->b&&!bake.find("dynamics_checked")->b,"bake does not claim clearance or dynamics");
    for(auto* doc:{&session.document})for(const auto& node:doc->assembly)if(node.name=="Glass")check(node.p.count("tz")==0||node.p.at("tz")=="0","baking leaves authored glass placement unchanged");
    BakedMotionFrames frames;std::string error;check(decodeBakedMotion(*motion,frames,error),error);check(frames.frameCount==136&&frames.nodes.size()==2&&frames.channels.size()==1,"decoded frame inventory");
    size_t glassIndex=frames.names[0]=="Glass"?0:1;close(frames.poses[60*2+glassIndex].m[14],-420,1e-3,"frame 60 (t=2 s) holds the glass fully down");close(frames.poses[30*2+glassIndex].m[14],-210,1e-3,"frame 30 eases through the midpoint");close(frames.poses[135*2+glassIndex].m[14],0,1e-3,"last frame returns to the top");
    close(frames.coordinates[60],-420,1e-3,"measured prismatic coordinate at the bottom");close(frames.poses[60*2+(1-glassIndex)].m[14],-420,1e-3,"rigid carriage follows the glass");
    const auto list=accepted(session.call("motion_info",V::makeObj()));const auto& row=motionRow(list,"Window down and up");check(row.find("baked")->b&&!row.find("stale")->b&&row.find("frame_count")->num==136,"motion_info lists a fresh bake");
    const auto revision=session.revision;const auto before=documentToJson(session.document);
    const auto solved=accepted(session.call("motion_info",json(R"({"name":"Window down and up","time_s":1,"solve":true})")));const auto& evaluation=*solved.find("evaluation");close(evaluation.find("channels")->arr[0].find("value")->num,-210,1e-9,"time evaluation");
    bool glassSolved=false;for(const auto& instance:evaluation.find("solved_instances")->arr)if(instance.find("name")->str=="Glass"){glassSolved=true;close(instance.find("world_column_major")->arr[14].num,-210,1e-6,"read-only solve returns the glass pose at t=1 s");}check(glassSolved,"solved instances include the glass");
    const auto detail=accepted(session.call("motion_info",json(R"({"name":"Window down and up","frame":60})")));close(detail.find("frame")->find("coordinates")->arr[0].num,-420,1e-3,"frame decode through motion_info");check(detail.find("diagnostics")->find("channels")->arr[0].find("continuity")->str=="C1","motion_info reports C1 continuity");
    check(session.revision==revision&&documentToJson(session.document)==before,"inspection and solve never change revision or placements");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Too far","graph":{"channels":[{"name":"glass","mate":1,"from":0,"curve":{"type":"linear","duration":1,"to":-500}}]}}]})"),"MOTION_LIMIT_VIOLATION","travel past the lower stop");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Bounce","graph":{"channels":[{"name":"glass","mate":1,"from":-400,"curve":{"type":"spring","duration":2,"to":0,"frequency_hz":2,"damping":0.1}}]}}]})"),"MOTION_LIMIT_VIOLATION","spring overshoot above the upper stop");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Ghost","graph":{"channels":[{"name":"glass","mate":9,"from":0,"curve":{"type":"hold","duration":1}}]}}]})"),"MOTION_UNKNOWN_JOINT","unknown mate");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Rigid","graph":{"channels":[{"name":"carriage","mate":2,"from":0,"curve":{"type":"hold","duration":1}}]}}]})"),"MOTION_UNSUPPORTED_JOINT","rigid mate is not a skeleton joint");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Twice","graph":{"channels":[{"name":"a","mate":1,"from":0,"curve":{"type":"hold","duration":1}},{"name":"b","mate":1,"from":0,"curve":{"type":"hold","duration":1}}]}}]})"),"MOTION_DUPLICATE_JOINT","two channels on one joint");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"assembly.edit","id":"Glass","fixed":true},{"op":"motion.graph.set","name":"Locked","graph":{"channels":[{"name":"glass","mate":1,"from":0,"curve":{"type":"hold","duration":1}}]}}]})"),"MOTION_JOINT_NOT_DRIVABLE","joint with both sides grounded");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Jump","graph":{"channels":[{"name":"glass","mate":1,"from":0,"curve":{"type":"sequence","items":[{"type":"linear","duration":1,"to":-100},{"type":"linear","duration":1,"from":-200,"to":-300}]}}]}}]})"),"MOTION_DISCONTINUITY","value jump between segments");
    apply(session,R"({"commands":[{"op":"motion.graph.set","name":"Allowed jump","graph":{"allow_jumps":true,"channels":[{"name":"glass","mate":1,"from":0,"curve":{"type":"sequence","items":[{"type":"linear","duration":1,"to":-100},{"type":"linear","duration":1,"from":-200,"to":-300}]}}]}},{"op":"motion.remove","name":"Allowed jump"}]})");
    rejected(session,"motion_info",json(R"({"graph":{"channels":[{"name":"glass","mate":1,"from":0,"curve":{"type":"linear","duration":1,"to":-600}}]}})"),"MOTION_LIMIT_VIOLATION","dry-run graph validation");
    const auto dry=accepted(session.call("motion_info",json(R"({"graph":{"channels":[{"name":"glass","mate":1,"curve":{"type":"ease","duration":1,"to":-100}}]}})")));check(dry.find("valid")->b&&dry.find("diagnostics")->find("channels")->arr[0].find("start")->num==0,"curve without from starts at the measured joint coordinate");
    // A parameter change moves the stop and the target: the bake stays playable but stale.
    apply(session,R"({"commands":[{"op":"param.set","name":"glass_travel","expr":400}]})");
    auto status=motionRow(accepted(session.call("motion_info",V::makeObj())),"Window down and up");check(status.find("stale")->b&&status.find("stale_reasons")->dump().find("assembly_changed")!=std::string::npos,"parameter edit marks the bake stale");
    check(decodeBakedMotion(*findMotion(session.document,"Window down and up"),frames,error),"stale bake remains playable");
    apply(session,R"({"commands":[{"op":"motion.bake","name":"Window down and up"}]})");status=motionRow(accepted(session.call("motion_info",V::makeObj())),"Window down and up");check(!status.find("stale")->b,"re-bake clears stale state");
    check(decodeBakedMotion(*findMotion(session.document,"Window down and up"),frames,error),error);close(frames.coordinates[60],-400,1e-3,"re-bake follows the new parameter");
    apply(session,R"({"commands":[{"op":"assembly.drive","driver":"Glass","mode":"translate","delta":-50}]})");status=motionRow(accepted(session.call("motion_info",V::makeObj())),"Window down and up");check(status.find("stale")->b,"committed placement change marks the bake stale");
    apply(session,R"({"commands":[{"op":"motion.bake","name":"Window down and up"},{"op":"motion.graph.set","name":"Window down and up","graph":{"channels":[{"name":"glass","mate":1,"from":0,"curve":{"type":"ease","duration":3,"to":-300}}]}}]})");
    status=motionRow(accepted(session.call("motion_info",V::makeObj())),"Window down and up");check(status.find("stale")->b&&status.find("stale_reasons")->dump().find("graph_changed")!=std::string::npos,"graph replacement keeps the bake flagged graph_changed");
    // Rename, conflict, persistence, version gate, undo/redo.
    apply(session,R"({"commands":[{"op":"motion.bake","name":"Window down and up","fps":60},{"op":"motion.graph.set","name":"Second","graph":{"channels":[{"name":"glass","mate":1,"curve":{"type":"hold","duration":1}}]}}]})");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.rename","name":"Second","new_name":"Window down and up"}]})"),"MOTION_NAME_CONFLICT","rename onto an existing motion");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.remove","name":"Absent"}]})"),"MOTION_NOT_FOUND","remove of a missing motion");
    apply(session,R"({"commands":[{"op":"motion.rename","name":"Window down and up","new_name":"Window cycle"}]})");check(findMotion(session.document,"Window cycle")&&!findMotion(session.document,"Window down and up"),"rename keeps the bake under its new name");
    check(version(session.document)==49,"049A baked motion receipts conditionally require document version 49");
    check(documentToJson(session.document).find("frames_f32le_base64")==std::string::npos&&bakeIsReceipt(*findMotion(session.document,"Window cycle")->find("bake")),"049A: authoring and journal keep a receipt, never frame payloads");
    const auto saved=documentToJson(session.document);Document restored;check(documentFromJson(saved,restored,&error),error);check(restored.motions.dump()==session.document.motions.dump(),"JSON round trip preserves graphs and bakes");
    auto older=json(saved);older.set("version",V::makeNum(46));Document rejectedDoc;check(!documentFromJson(older.dump(),rejectedDoc,&error)&&error.find("version 47")!=std::string::npos,"version 46 readers of motions reject explicitly");
    older.set("version",V::makeNum(48));check(!documentFromJson(older.dump(),rejectedDoc,&error)&&error.find("version 49")!=std::string::npos,"a receipt labelled version 48 rejects explicitly: "+error);
    const auto path=std::filesystem::temp_directory_path()/("ark-motion-044b-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ark");
    check(saveDocument(session.document,path.string().c_str()),"binary save");Document loaded;check(loadDocument(path.string().c_str(),loaded,&error),error);std::filesystem::remove(path);check(loaded.motions.dump()==session.document.motions.dump(),"binary .ark round trip preserves motions");
    const auto beforeUndo=session.document.motions.dump();accepted(session.call("doc_undo",V::makeObj()));check(findMotion(session.document,"Window down and up")&&!findMotion(session.document,"Window cycle"),"undo restores the previous motion name");
    accepted(session.call("doc_redo",V::makeObj()));check(session.document.motions.dump()==beforeUndo,"redo restores motions exactly");
    apply(session,R"({"commands":[{"op":"motion.remove","name":"Window cycle"},{"op":"motion.remove","name":"Second"}]})");check(session.document.motions.isNull()&&version(session.document)==baseVersion,"removing every motion returns to the earlier required version");
    (void)assemblyBefore;
}
void robotFixture(AgentSession& session){apply(session,R"({"commands":[
  {"op":"param.set","name":"L1","expr":120},{"op":"param.set","name":"L2","expr":90},
  {"op":"feature.add","type":"box","name":"Link body","p":{"dx":20,"dy":10,"dz":10}},
  {"op":"feature.add","type":"box","name":"Pin","p":{"dx":4,"dy":4,"dz":4}},
  {"op":"assembly.add","name":"Base carrier","feature":0},
  {"op":"assembly.add","name":"Upper carrier","feature":0},
  {"op":"assembly.add","name":"Fore carrier","feature":0,"p":{"tx":"L1"}},
  {"op":"assembly.add","name":"Hand carrier","feature":0,"p":{"tx":"L1+L2"}},
  {"op":"assembly.add","name":"Base","parent":"Base carrier","feature":"Link body","fixed":true},
  {"op":"assembly.add","name":"Shoulder A","parent":"Base carrier","feature":"Pin"},
  {"op":"assembly.add","name":"Upper arm","parent":"Upper carrier","feature":"Link body"},
  {"op":"assembly.add","name":"Shoulder B","parent":"Upper carrier","feature":"Pin"},
  {"op":"assembly.add","name":"Elbow A","parent":"Upper carrier","feature":"Pin","p":{"tx":"L1"}},
  {"op":"assembly.add","name":"Forearm","parent":"Fore carrier","feature":"Link body"},
  {"op":"assembly.add","name":"Elbow B","parent":"Fore carrier","feature":"Pin"},
  {"op":"assembly.add","name":"Wrist A","parent":"Fore carrier","feature":"Pin","p":{"tx":"L2"}},
  {"op":"assembly.add","name":"Hand","parent":"Hand carrier","feature":"Link body"},
  {"op":"assembly.add","name":"Wrist B","parent":"Hand carrier","feature":"Pin"},
  {"op":"mate.add","type":"revolute","a":"Shoulder A","b":"Shoulder B","reference_a":"origin","reference_b":"origin","limits":{"min":-90,"max":90}},
  {"op":"mate.add","type":"revolute","a":"Elbow A","b":"Elbow B","reference_a":"origin","reference_b":"origin","limits":{"min":-120,"max":120}},
  {"op":"mate.add","type":"revolute","a":"Wrist A","b":"Wrist B","reference_a":"origin","reference_b":"origin"}]})");}
const char* kWave=R"({"commands":[{"op":"motion.graph.set","name":"Robot wave","graph":{"channels":[
  {"name":"shoulder","mate":1,"from":0,"curve":{"type":"sequence","items":[{"type":"ease","duration":1,"to":30},{"type":"sine","duration":2,"amplitude":15,"period_s":1},{"type":"ease","duration":1,"to":0}]}},
  {"name":"elbow","mate":2,"expr":"0.5*shoulder+10"},
  {"name":"wrist","mate":3,"from":0,"curve":{"type":"ping_pong","count":2,"item":{"type":"bezier","duration":1,"to":40,"c1":[0.3,0],"c2":[0.7,40]}}}]}},
  {"op":"motion.bake","name":"Robot wave","fps":30}]})";
void testRobotWave(){
    AgentSession session;robotFixture(session);const int baseVersion=version(session.document);apply(session,kWave);
    const auto* motion=findMotion(session.document,"Robot wave");BakedMotionFrames frames;std::string error;check(decodeBakedMotion(*motion,frames,error),error);
    check(frames.frameCount==121&&frames.channels.size()==3,"three-joint wave bakes 121 frames at 30 fps");check(frames.nodes.size()==8,"all eight distal link and pin instances move; the grounded base and shoulder pin do not");
    CompiledMotion graph;std::map<std::string,double> starts{{"shoulder",0},{"wrist",0}};check(compileMotionGraph(*motion->find("graph"),session.document.buildEnv(),starts,graph,error),error);
    size_t hand=frames.nodes.size();for(size_t i=0;i<frames.nodes.size();++i)if(frames.names[i]=="Hand")hand=i;check(hand<frames.nodes.size(),"hand is baked");
    for(size_t k:{0u,17u,45u,73u,120u}){const double t=std::min(4.,k/30.);const auto values=evaluateMotion(graph,t);
        for(size_t c=0;c<3;++c)close(frames.coordinates[k*3+c],values[c],2e-3,"measured joint "+frames.channels[c]+" at frame "+std::to_string(k));
        const double s=deg2rad(values[0]),e=deg2rad(values[1]);const auto& pose=frames.poses[k*frames.nodes.size()+hand];
        close(pose.m[12],120*std::cos(s)+90*std::cos(s+e),2e-3,"hand x follows serial forward kinematics");close(pose.m[13],120*std::sin(s)+90*std::sin(s+e),2e-3,"hand y follows serial forward kinematics");
        const double w=deg2rad(values[2]);close(std::atan2(pose.m[1],pose.m[0]),std::remainder(s+e+w,2*K_PI),2e-4,"hand orientation sums the three joint angles");}
    const auto detail=accepted(session.call("motion_info",json(R"({"name":"Robot wave"})")));const auto& diagnostics=*detail.find("diagnostics");
    check(channelDiagnostics(diagnostics,"shoulder").find("continuity")->str=="C0","ease into a sine reports a velocity break");check(channelDiagnostics(diagnostics,"wrist").find("continuity")->str=="C1","flat-tangent Bezier ping-pong is C1");check(channelDiagnostics(diagnostics,"elbow").find("kind")->str=="coupling","coupled elbow is reported as a coupling");
    check(detail.find("bake")->find("solver")->find("rigid_side_steps")->num>0&&!detail.find("bake")->find("frames_f32le_base64"),"bake summary reports solver steps without the payload");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Spin","graph":{"channels":[{"name":"wrist","mate":3,"from":0,"curve":{"type":"linear","duration":1,"to":720}}]}},{"op":"motion.bake","name":"Spin","fps":2}]})"),"MOTION_FRAME_STEP_TOO_LARGE","unlimited revolute step of 360 degrees per frame");
    apply(session,R"({"commands":[{"op":"motion.graph.set","name":"Spin","graph":{"channels":[{"name":"wrist","mate":3,"from":0,"curve":{"type":"linear","duration":1,"to":720}}]}},{"op":"motion.bake","name":"Spin","fps":30}]})");
    check(decodeBakedMotion(*findMotion(session.document,"Spin"),frames,error),error);close(frames.coordinates[30],720,2e-3,"unlimited wrist accumulates two full turns");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Overreach","graph":{"channels":[{"name":"shoulder","mate":1,"from":0,"curve":{"type":"linear","duration":1,"to":20}},{"name":"elbow","mate":2,"expr":"7*shoulder"}]}}]})"),"MOTION_LIMIT_VIOLATION","coupled channel past its stop");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"assembly.edit","id":"Base","fixed":false},{"op":"motion.bake","name":"Robot wave"}]})"),"no fixed anchor","ungrounded skeleton is rejected by the static solve before baking");
    check(!motionRow(accepted(session.call("motion_info",V::makeObj())),"Robot wave").find("stale")->b,"robot wave is fresh before a mate edit");
    apply(session,R"({"commands":[{"op":"mate.edit","id":2,"limits":{"min":-110,"max":110}}]})");
    const auto afterMate=accepted(session.call("motion_info",V::makeObj()));check(motionRow(afterMate,"Robot wave").find("stale")->b&&motionRow(afterMate,"Robot wave").find("stale_reasons")->dump().find("assembly_changed")!=std::string::npos,"mate limit edit marks the robot bake stale");
    apply(session,R"({"commands":[{"op":"motion.remove","name":"Robot wave"},{"op":"motion.remove","name":"Spin"}]})");check(version(session.document)==baseVersion,"robot document returns to its revolute version");
}
void fourBarFixture(AgentSession& session){apply(session,R"J({"commands":[
  {"op":"param.set","name":"arm_length","expr":60},{"op":"param.set","name":"pivot_spacing","expr":100},{"op":"param.set","name":"arm_angle","expr":25},
  {"op":"feature.add","type":"box","name":"Rail","p":{"dx":12,"dy":112,"dz":8}},{"op":"feature.add","type":"box","name":"Beam","p":{"dx":60,"dy":8,"dz":8}},{"op":"feature.add","type":"box","name":"Pin","p":{"dx":5,"dy":5,"dz":12}},
  {"op":"assembly.add","name":"Ground carrier","feature":0},{"op":"assembly.add","name":"Lower carrier","feature":0,"p":{"rz":"arm_angle"}},
  {"op":"assembly.add","name":"Upright carrier","feature":0,"p":{"tx":"arm_length*cos(arm_angle*pi/180)","ty":"arm_length*sin(arm_angle*pi/180)","rz":90}},{"op":"assembly.add","name":"Upper carrier","feature":0,"p":{"ty":"pivot_spacing","rz":"arm_angle"}},
  {"op":"assembly.add","name":"Chassis","parent":"Ground carrier","feature":"Rail","fixed":true},{"op":"assembly.add","name":"Lower chassis pin","parent":"Ground carrier","feature":"Pin"},{"op":"assembly.add","name":"Upper chassis pin","parent":"Ground carrier","feature":"Pin","p":{"ty":"pivot_spacing"}},
  {"op":"assembly.add","name":"Lower arm","parent":"Lower carrier","feature":"Beam"},{"op":"assembly.add","name":"Lower inner pin","parent":"Lower carrier","feature":"Pin"},{"op":"assembly.add","name":"Lower outer pin","parent":"Lower carrier","feature":"Pin","p":{"tx":"arm_length"}},
  {"op":"assembly.add","name":"Upright","parent":"Upright carrier","feature":"Beam"},{"op":"assembly.add","name":"Upright lower pin","parent":"Upright carrier","feature":"Pin"},{"op":"assembly.add","name":"Upright upper pin","parent":"Upright carrier","feature":"Pin","p":{"tx":"pivot_spacing"}},
  {"op":"assembly.add","name":"Upper arm","parent":"Upper carrier","feature":"Beam"},{"op":"assembly.add","name":"Upper inner pin","parent":"Upper carrier","feature":"Pin"},{"op":"assembly.add","name":"Upper outer pin","parent":"Upper carrier","feature":"Pin","p":{"tx":"arm_length"}},
  {"op":"mate.add","type":"revolute","a":"Lower chassis pin","b":"Lower inner pin","reference_a":"origin","reference_b":"origin","limits":{"min":-35,"max":70}},
  {"op":"mate.add","type":"revolute","a":"Lower outer pin","b":"Upright lower pin","reference_a":"origin","reference_b":"origin"},
  {"op":"mate.add","type":"revolute","a":"Upright upper pin","b":"Upper outer pin","reference_a":"origin","reference_b":"origin"},
  {"op":"mate.add","type":"revolute","a":"Upper chassis pin","b":"Upper inner pin","reference_a":"origin","reference_b":"origin"}]})J");}
void testConstrainedFollowersAndConflicts(){
    AgentSession session;fourBarFixture(session);
    apply(session,R"({"commands":[{"op":"motion.graph.set","name":"Jounce","graph":{"channels":[{"name":"lower","mate":1,"curve":{"type":"ease","duration":1,"to":45}}]}},{"op":"motion.bake","name":"Jounce","fps":30}]})");
    const auto* motion=findMotion(session.document,"Jounce");BakedMotionFrames frames;std::string error;check(decodeBakedMotion(*motion,frames,error),error);
    check(motion->find("bake")->find("solver")->find("constrained_steps")->num>0,"closed loop bakes through constrained continuation");check(frames.nodes.size()>=9,"upright and upper arm follow as driven links");close(frames.coordinates[30],45,1e-3,"driven lower arm reaches 45 degrees");close(frames.starts[0],25,1e-6,"curve starts at the committed 25 degree pose");
    size_t upper=frames.nodes.size();for(size_t i=0;i<frames.nodes.size();++i)if(frames.names[i]=="Upper arm")upper=i;check(upper<frames.nodes.size(),"upper arm is a baked follower");
    close(std::atan2(frames.poses[30*frames.nodes.size()+upper].m[1],frames.poses[30*frames.nodes.size()+upper].m[0]),deg2rad(45),1e-4,"parallelogram follower turns with the driven arm");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Fight","graph":{"channels":[{"name":"lower","mate":1,"curve":{"type":"ease","duration":1,"to":45}},{"name":"upper","mate":4,"from":25,"curve":{"type":"hold","duration":1}}]}},{"op":"motion.bake","name":"Fight"}]})"),"MOTION_","two channels fighting over one closed loop");
    const auto reply=session.call("doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Fight","graph":{"channels":[{"name":"lower","mate":1,"curve":{"type":"ease","duration":1,"to":45}},{"name":"upper","mate":4,"from":25,"curve":{"type":"hold","duration":1}}]}},{"op":"motion.bake","name":"Fight"}]})"));
    const auto text=reply.find("structuredContent")->dump();check(text.find("MOTION_CHANNEL_CONFLICT")!=std::string::npos||text.find("MOTION_BAKE_REJECTED")!=std::string::npos,"loop conflict reports the channel conflict or solver rejection: "+text.substr(0,300));
}
void measureLargerAssemblyBake(){
    // Timing only: the glass fixture plus 120 extra occurrences and 30 rigid attachments.
    AgentSession session;glassFixture(session);V commands=V::makeArr();
    for(int i=0;i<60;++i){V a=V::makeObj(),p=V::makeObj();p.set("tx",V::makeNum(100.*(i%10)));p.set("ty",V::makeNum(100.*(i/10)+1000));a.set("op",V::makeStr("assembly.add"));a.set("name",V::makeStr("Fixed "+std::to_string(i)));a.set("feature",V::makeStr("Carriage body"));a.set("fixed",V::makeBool(true));a.set("p",p);commands.arr.push_back(a);
        V b=a;b.set("name",V::makeStr("Attached "+std::to_string(i)));b.set("fixed",V::makeBool(false));commands.arr.push_back(b);
        if(i<30){V m=V::makeObj();m.set("op",V::makeStr("mate.add"));m.set("type",V::makeStr("rigid"));m.set("a",V::makeStr("Fixed "+std::to_string(i)));m.set("b",V::makeStr("Attached "+std::to_string(i)));commands.arr.push_back(m);}else{commands.arr.back().set("fixed",V::makeBool(true));}}
    for(size_t start=0;start<commands.arr.size();start+=100){V args=V::makeObj(),chunk=V::makeArr();for(size_t i=start;i<std::min(commands.arr.size(),start+100);++i)chunk.arr.push_back(commands.arr[i]);args.set("expected_revision",V::makeNum((double)session.revision));args.set("commands",chunk);accepted(session.call("doc_apply",args));}
    check(session.document.assembly.size()==123&&session.document.mates.size()==32,"larger timing fixture");
    const auto started=std::chrono::steady_clock::now();apply(session,kGlassMotion);const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    BakedMotionFrames frames;std::string error;check(decodeBakedMotion(*findMotion(session.document,"Window down and up"),frames,error)&&frames.nodes.size()==2,"larger assembly still stores only moving instances");
    std::cout<<"timing: 123-occurrence/32-mate graph.set+bake of 136 frames took "<<ms<<" ms (local Release, includes doc_apply validation)\n";
}
void testContracts(){
    const auto tools=AgentSession::tools();bool info=false,bakeOp=false;for(const auto& tool:tools.arr){if(tool.find("name")->str=="motion_info"){info=true;check(tool.find("annotations")->find("readOnlyHint")->b,"motion_info is read-only");}if(tool.find("name")->str=="doc_apply")bakeOp=tool.dump().find("motion.bake")!=std::string::npos&&tool.dump().find("motion.graph.set")!=std::string::npos;}
    check(info&&bakeOp,"motion_info and motion ops are discoverable");
    AgentSession session;const auto guide=agentOnboard(session,json(R"({"topic":"motion","detail":"guide"})"));check(guide.find("skeleton_motion")&&guide.dump().find("motion_info")!=std::string::npos&&guide.find("skeleton_motion")->find("door_glass_example"),"motion onboarding documents the vector graph contract");
    rejected(session,"motion_info",json(R"({"name":"Absent"})"),"MOTION_NOT_FOUND","missing motion read");
    rejected(session,"motion_info",json(R"({"time_s":1})"),"require name or graph","time evaluation without a motion");
    check(accepted(session.call("motion_info",V::makeObj())).find("count")->num==0,"empty document lists no motions");
}
}
int main(){
    try{
        const std::pair<const char*,void(*)()> stages[]={{"segments",testSegmentsAndComposition},{"couplings",testCouplingsAndRejections},{"glass",testGlassBakeAndStale},{"robot",testRobotWave},{"four-bar",testConstrainedFollowersAndConflicts},{"contracts",testContracts},{"timing",measureLargerAssemblyBake}};
        for(const auto& stage:stages){std::cerr<<"stage "<<stage.first<<'\n';stage.second();}
        std::cout<<"Skeleton motion 044B tests passed: "<<checks<<" checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAILED after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
