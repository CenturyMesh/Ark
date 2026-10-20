#include "ark/agent.hpp"
#include "ark/motion_graph.hpp"
#include "ark/native_features.hpp"
#include "ark/onboarding.hpp"
#include "ark/serialize.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

// 046B motion clearance over baked motions. Synthetic fixtures only: a revolute
// arm sweeping past obstacles with analytic distances, a known interference
// range, a thin blade between two frames, stale/digest binding, pair filtering,
// a coupled serial chain and a closed four-bar.
using namespace ark;
using V=arkjson::Value;
namespace {
int checks=0;
void check(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
void close(double actual,double expected,double tolerance,const std::string& message){check(std::isfinite(actual)&&std::fabs(actual-expected)<=tolerance,message+": actual="+std::to_string(actual)+", expected="+std::to_string(expected));}
V json(const std::string& source){V value;if(!arkjson::parse(source,value))throw std::runtime_error("test JSON parses: "+source.substr(0,80));return value;}
V accepted(const V& reply){if(reply.find("isError")->b)throw std::runtime_error(reply.find("structuredContent")->dump().substr(0,1200));return *reply.find("structuredContent");}
V withRevision(const AgentSession& session,const std::string& source){auto args=json(source);args.set("expected_revision",V::makeNum((double)session.revision));return args;}
V apply(AgentSession& session,const std::string& source){return accepted(session.call("doc_apply",withRevision(session,source)));}
V rejected(AgentSession& session,const char* tool,const V& args,const std::string& code,const std::string& message){
    const auto before=documentToJson(session.document);const auto revision=session.revision;const auto reply=session.call(tool,args);
    check(reply.find("isError")->b,message+" must reject");const auto text=reply.find("structuredContent")->dump();check(text.find(code)!=std::string::npos,message+" reports "+code+": "+text.substr(0,500));
    check(session.revision==revision&&documentToJson(session.document)==before,message+" leaves document and revision unchanged");return *reply.find("structuredContent");}
V clearance(AgentSession& session,const std::string& request){V args=V::makeObj();args.set("name",V::makeStr("Sweep"));args.set("clearance",json(request));const auto before=documentToJson(session.document);const auto revision=session.revision;
    const auto out=accepted(session.call("motion_info",args));check(session.revision==revision&&documentToJson(session.document)==before,"clearance query never changes the document or revision");return *out.find("clearance");}
double deg(double d){return d*K_PI/180;}
u64 node(const AgentSession& session,const char* name){for(const auto& n:session.document.assembly)if(n.name==name)return n.id;throw std::runtime_error(std::string("node missing: ")+name);}
const V& pairRow(const V& report,const AgentSession& session,const char* a,const char* b){const u64 na=node(session,a),nb=node(session,b);
    for(const auto& row:report.find("pairs")->arr){const u64 ra=(u64)row.find("a")->find("node")->num,rb=(u64)row.find("b")->find("node")->num;if((ra==na&&rb==nb)||(ra==nb&&rb==na))return row;}
    throw std::runtime_error(std::string("pair row missing: ")+a+" / "+b);}
// Revolute arm about the world Z axis through the origin. Arm link occupies
// x in [20,120], y in [0,2], z in [0,10] of its rotating frame.
const char* kArm=R"J({"commands":[
  {"op":"feature.add","type":"box","name":"Pin","p":{"dx":4,"dy":4,"dz":4}},
  {"op":"feature.add","type":"box","name":"Arm link","p":{"dx":100,"dy":2,"dz":10}},
  {"op":"feature.add","type":"box","name":"Anchor body","p":{"dx":10,"dy":10,"dz":10}},
  {"op":"feature.add","type":"box","name":"Bridge body","p":{"dx":6,"dy":6,"dz":8}},
  {"op":"feature.add","type":"box","name":"Post body","p":{"dx":10,"dy":20,"dz":30}},
  {"op":"feature.add","type":"box","name":"Stop body","p":{"dx":100,"dy":200,"dz":10}},
  {"op":"feature.add","type":"box","name":"Blade body","p":{"dx":28,"dy":0.4,"dz":20}},
  {"op":"assembly.add","name":"Base carrier","feature":0},
  {"op":"assembly.add","name":"Arm carrier","feature":0},
  {"op":"assembly.add","name":"Anchor","parent":"Base carrier","feature":"Anchor body","fixed":true,"p":{"tx":-60,"ty":-60}},
  {"op":"assembly.add","name":"Pivot A","parent":"Base carrier","feature":"Pin"},
  {"op":"assembly.add","name":"Arm","parent":"Arm carrier","feature":"Arm link","p":{"tx":20}},
  {"op":"assembly.add","name":"Pivot B","parent":"Arm carrier","feature":"Pin"},
  {"op":"assembly.add","name":"Bridge","feature":"Bridge body","fixed":true,"p":{"tx":"63*cos(pi/4)-3*cos(pi/4)+3*sin(pi/4)","ty":"63*sin(pi/4)-3*sin(pi/4)-3*cos(pi/4)","tz":12,"rz":45}},
  {"op":"assembly.add","name":"Post","feature":"Post body","fixed":true,"p":{"tx":"125*cos(80*pi/180)+10*sin(80*pi/180)","ty":"125*sin(80*pi/180)-10*cos(80*pi/180)","tz":-10,"rz":80}},
  {"op":"assembly.add","name":"Stop","feature":"Stop body","fixed":true,"p":{"tx":"30*cos(pi/3)","ty":"30*sin(pi/3)","rz":60}},
  {"op":"assembly.add","name":"Blade","feature":"Blade body","fixed":true,"p":{"tx":"90*cos(40.5*pi/180)","ty":"90*sin(40.5*pi/180)","tz":-5,"rz":40.5}},
  {"op":"mate.add","type":"revolute","a":"Pivot A","b":"Pivot B","reference_a":"origin","reference_b":"origin","limits":{"min":-10,"max":120}}]})J";
const char* kSweep=R"J({"commands":[{"op":"motion.graph.set","name":"Sweep","graph":{"channels":[{"name":"arm","mate":1,"from":0,"curve":{"type":"linear","duration":1,"to":90}}]}},{"op":"motion.bake","name":"Sweep","fps":10}]})J";

void testAnalyticSweep(){
    AgentSession session;apply(session,kArm);apply(session,kSweep);
    const auto* bake=findMotion(session.document,"Sweep")->find("bake");check(bake->find("frame_count")->num==11,"0..90 degrees at 10 fps bakes 11 frames, 9 degrees apart");
    // Bridge 2 mm above the arm's swept band: exact minimum 2 mm at the 45 degree frame.
    auto bridge=clearance(session,R"J({"pairs":[{"a":"Arm","b":"Bridge"}],"max_subdivisions":6})J");
    close(bridge.find("min_clearance_mm")->num,2,1e-6,"bridge minimum equals the analytic 2 mm vertical gap");check(bridge.find("worst")->find("frame")->num==5,"bridge worst pose is at (or rounds to) the 45 degree frame");
    check(bridge.find("continuous_certified")->b&&bridge.find("uncertified_interval_count")->num==0,"bridge sweep certified contact free between frames: "+bridge.find("uncertified_intervals")->dump().substr(0,300));
    const double bridgeLower=bridge.find("continuous_lower_bound_mm")->num;check(bridgeLower>0&&bridgeLower<=2+1e-9,"certified lower bound is positive and below the true 2 mm minimum: "+std::to_string(bridgeLower));
    check(bridge.find("pairs")->arr[0].find("bound_model")->str=="joint_chain"&&bridge.find("pairs")->arr[0].find("bound_proven")->b,"single revolute joint uses the proven joint-chain bound");
    check(bridge.find("evaluations")->find("sub_poses")->num>0&&bridge.find("evaluations")->find("subdivision_levels")->num>=3,"the 2 mm gap needs bisection: 9 degree frames move the arm sphere by about 19 mm");
    check(!bridge.find("cached")->b&&bridge.find("persisted")->b==false&&bridge.find("dynamics_checked")->b==false,"derived evidence flags");
    // Post: analytic minimum 125 - sqrt(120^2+2^2) at 79.05 degrees, between frames 8 (72) and 9 (81).
    const double postMin=125-std::sqrt(120.*120+4),frame9=125-120*std::cos(deg(1));
    auto post=clearance(session,R"J({"pairs":[{"a":"Arm","b":"Post"}],"max_subdivisions":6})J");
    const double measured=post.find("min_clearance_mm")->num,lower=post.find("continuous_lower_bound_mm")->num;
    check(measured>=postMin-1e-6&&measured<=frame9+1e-6,"measured post minimum lies between the analytic minimum and the frame-9 distance: "+std::to_string(measured));
    check(lower>0&&lower<=postMin+1e-6,"post lower bound never exceeds the analytic between-frame minimum: "+std::to_string(lower)+" <= "+std::to_string(postMin));
    check(post.find("continuous_certified")->b,"post sweep certified contact free");
    const auto& postRow=post.find("pairs")->arr[0];close(postRow.find("frame_minimum_lower_bound_mm")->num,frame9,1e-6,"post frame minimum is the 81 degree frame (frame 9)");
    // A 6 mm threshold: the post comes within 4.98 mm, so the threshold is not certified.
    auto postThreshold=clearance(session,R"J({"pairs":[{"a":"Arm","b":"Post"}],"threshold_mm":6})J");check(postThreshold.find("continuous_certified")->b&&!postThreshold.find("threshold_certified")->b&&postThreshold.find("below_threshold_pair_frames")->num>=1,"6 mm threshold: contact free but below threshold");
    bool amber=false;for(const auto& m:postThreshold.find("markers")->arr)amber|=m.find("status")->str=="below_threshold";check(amber,"below-threshold marker on the timeline");
    {auto t5=clearance(session,R"J({"pairs":[{"a":"Arm","b":"Post"}],"threshold_mm":5})J");const auto& m=t5.find("markers")->arr;check(m.size()==2&&m[0].find("status")->str=="below_threshold"&&m[0].find("from_frame")->num==8&&m[0].find("to_frame")->num==9&&m[1].find("status")->str=="threshold_unproven"&&m[1].find("from_frame")->num==9,"5 mm: a bisection pose below 5 mm in interval 8..9 (amber), interval 9..10 clear of contact but not proven above 5 mm (hatched): "+t5.find("markers")->dump());}
    std::cout<<"timing: bridge "<<bridge.find("timing_ms")->find("total")->num<<" ms ("<<bridge.find("evaluations")->find("exact_pair_frames")->num<<" exact frames, "<<bridge.find("evaluations")->find("sub_poses")->num<<" sub-poses, "<<bridge.find("evaluations")->find("subdivision_levels")->num<<" levels); post "<<post.find("timing_ms")->find("total")->num<<" ms, measured "<<measured<<" vs analytic "<<postMin<<", lower bound "<<lower<<"\n";
}
void testInterferenceAngles(){
    AgentSession session;apply(session,kArm);apply(session,kSweep);
    // Stop face on the 60 degree radial plane beyond 30 mm: contact starts at 60 - asin(2/30) = 56.18 degrees.
    auto stop=clearance(session,R"J({"pairs":[{"a":"Arm","b":"Stop"}]})J");
    std::set<int> frames;for(const auto& row:stop.find("interference_frames")->arr){check(row.find("status")->str=="interference"&&row.find("common_volume_mm3")->num>0,"stop overlap has positive common volume");frames.insert((int)row.find("frame")->num);}
    check(frames==std::set<int>({7,8,9,10}),"interference exactly at frames 7..10 (63..90 degrees)");check(!stop.find("continuous_certified")->b,"interfering motion is not certified");
    const auto& row=stop.find("pairs")->arr[0];check(row.find("status")->str=="interference","pair status interference");close(stop.find("min_clearance_mm")->num,0,1e-6,"interference reports zero clearance");
    check(stop.find("worst")->find("status")->str=="interference"&&stop.find("worst")->find("common_volume_mm3")->num>0,"worst pose is the deepest interference");
    const auto& markers=stop.find("markers")->arr;check(!markers.empty()&&markers.back().find("status")->str=="interference"&&markers.back().find("from_frame")->num==6&&markers.back().find("to_frame")->num==10,"red marker run covers intervals 6..10");
    auto before=clearance(session,R"J({"pairs":[{"a":"Arm","b":"Stop"}],"to_frame":6,"max_subdivisions":6})J");close(before.find("min_clearance_mm")->num,30*std::sin(deg(6))-2,1e-6,"frame 6 (54 degrees) clearance is 30 sin 6 - 2");
    check(before.find("continuous_certified")->b&&before.find("to_frame")->num==6,"frames 0..6 before the stop are certified contact free");
}
void testThinBlade(){
    AgentSession session;apply(session,kArm);apply(session,kSweep);
    auto unrefined=clearance(session,R"J({"pairs":[{"a":"Arm","b":"Blade"}],"max_subdivisions":0})J");
    check(unrefined.find("interference_frames")->arr.empty()&&unrefined.find("contact_frame_count")->num==0,"no baked frame touches the 0.4 mm blade");
    close(unrefined.find("min_clearance_mm")->num,90*std::sin(deg(4.5))-2,1e-6,"closest frame is 36 degrees, 90 sin 4.5 - 2 from the blade");
    check(!unrefined.find("continuous_certified")->b&&unrefined.find("uncertified_interval_count")->num>=1,"frame sampling alone cannot certify the 36..45 degree interval");
    bool flagged=false;for(const auto& u:unrefined.find("uncertified_intervals")->arr)flagged|=u.find("from_frame")->num==4&&u.find("to_frame")->num==5;check(flagged,"certification flags interval 4..5");
    auto refined=clearance(session,R"J({"pairs":[{"a":"Arm","b":"Blade"}]})J");
    check(refined.find("between_frame_contact_count")->num>=1&&!refined.find("continuous_certified")->b,"subdivision finds the blade between frames");
    const auto& hit=refined.find("between_frame_contacts")->arr[0];check(hit.find("from_frame")->num==4&&hit.find("to_frame")->num==5&&hit.find("status")->str=="interference","blade interference inside interval 4..5: "+hit.dump());
    close(hit.find("time_s")->num,.45,1e-9,"first bisection pose at 40.5 degrees (t=0.45 s) cuts the blade");
    bool red=false;for(const auto& m:refined.find("markers")->arr)red|=m.find("status")->str=="interference"&&m.find("from_frame")->num==4&&m.find("to_frame")->num==5;check(red,"interference marker on interval 4..5");
    check(refined.find("worst")->find("at_baked_frame")->b==false&&refined.find("worst")->find("status")->str=="interference","worst pose is the between-frame interference");
}
void testStaleAndBinding(){
    AgentSession session;apply(session,kArm);apply(session,kSweep);
    const auto* bake=findMotion(session.document,"Sweep")->find("bake");const auto sourceDigest=bake->find("source_digest")->str,graphDigest=bake->find("graph_digest")->str;
    auto first=clearance(session,R"J({"pairs":[{"a":"Arm","b":"Bridge"}]})J");const auto& binding=*first.find("bound_to");
    check(binding.find("revision")->num==(double)session.revision&&binding.find("source_digest")->str==sourceDigest&&binding.find("graph_digest")->str==graphDigest,"result is bound to revision, source_digest and graph_digest");
    auto again=clearance(session,R"J({"pairs":[{"a":"Arm","b":"Bridge"}]})J");check(again.find("cached")->b&&again.find("min_clearance_mm")->num==first.find("min_clearance_mm")->num,"same digests and request reuse the in-memory result");
    V args=json(R"J({"name":"Sweep","clearance":{"pairs":[{"a":"Arm","b":"Bridge"}]}})J");args.set("expected_revision",V::makeNum((double)session.revision+3));const auto conflict=rejected(session,"motion_info",args,"Revision conflict","stale expected_revision");check(conflict.find("error_code")->str=="REVISION_CONFLICT","revision conflict code");
    // A feature edit that changes no placement keeps the bake fresh, but changes the exact geometry: not served from the cache.
    apply(session,R"J({"commands":[{"op":"feature.edit","id":"Arm link","p":{"dz":11}}]})J");
    auto taller=clearance(session,R"J({"pairs":[{"a":"Arm","b":"Bridge"}]})J");check(!taller.find("cached")->b,"changed B-rep misses the cache");close(taller.find("min_clearance_mm")->num,1,1e-6,"taller arm leaves a 1 mm gap under the bridge");
    check(taller.find("bound_to")->find("revision")->num==(double)session.revision&&taller.find("bound_to")->find("source_digest")->str==sourceDigest,"fresh bake keeps its source digest at the new revision");
    // Placement change: stale, rejected explicitly, never re-baked.
    apply(session,R"J({"commands":[{"op":"assembly.edit","id":"Bridge","p":{"tz":13}}]})J");
    const auto revision=session.revision;const auto staleError=rejected(session,"motion_info",json(R"J({"name":"Sweep","clearance":{}})J"),"MOTION_BAKE_STALE","assembly change");check(staleError.dump().find("assembly_changed")!=std::string::npos,"stale reason assembly_changed");
    check(findMotion(session.document,"Sweep")->find("bake")->find("source_digest")->str==sourceDigest&&session.revision==revision,"a rejected clearance does not re-bake");
    apply(session,R"J({"commands":[{"op":"motion.bake","name":"Sweep","fps":10}]})J");auto rebaked=clearance(session,R"J({"pairs":[{"a":"Arm","b":"Bridge"}]})J");close(rebaked.find("min_clearance_mm")->num,2,1e-6,"after re-bake the bridge (now 13 mm up) is 2 mm above the 11 mm arm");
    apply(session,R"J({"commands":[{"op":"motion.graph.set","name":"Sweep","graph":{"channels":[{"name":"arm","mate":1,"from":0,"curve":{"type":"linear","duration":1,"to":80}}]}}]})J");
    check(rejected(session,"motion_info",json(R"J({"name":"Sweep","clearance":{}})J"),"MOTION_BAKE_STALE","graph change").dump().find("graph_changed")!=std::string::npos,"stale reason graph_changed");
    apply(session,R"J({"commands":[{"op":"motion.graph.set","name":"Unbaked","graph":{"channels":[{"name":"arm","mate":1,"from":0,"curve":{"type":"linear","duration":1,"to":10}}]}}]})J");
    rejected(session,"motion_info",json(R"J({"name":"Unbaked","clearance":{}})J"),"MOTION_NOT_BAKED","unbaked motion");
    rejected(session,"motion_info",json(R"J({"name":"Sweep","clearance":{},"time_s":0.5})J"),"excludes graph","clearance with time_s");
}
void testPairFilteringAndLimits(){
    AgentSession session;apply(session,kArm);apply(session,kSweep);
    auto all=clearance(session,R"J({})J");
    check(all.find("scope")->str=="all"&&all.find("pairs_checked")->num==11,"scope all: arm against six solids, pivot B against five (its mated pin skipped): "+all.find("pairs_checked")->dump());
    check(all.find("pairs_skipped")->find("rigid_cluster")->num==1&&all.find("pairs_skipped")->find("mated")->num>=1,"scope skips the rigid co-moving pivot and the mated pins: "+all.find("pairs_skipped")->dump());
    check(all.find("moving_instances")->arr.size()==2,"arm and pivot B move");check(!all.find("continuous_certified")->b,"stop and blade prevent certification");
    auto staticScope=clearance(session,R"J({"scope":"static"})J");check(staticScope.find("pairs_checked")->num==all.find("pairs_checked")->num,"no moving-moving pair in this fixture");
    auto one=clearance(session,R"J({"pairs":[{"a":"Arm","b":"Post"},{"a":"Anchor","b":"Arm"}]})J");check(one.find("pairs_checked")->num==2&&one.find("scope")->str=="pairs","explicit pairs only");
    const auto& anchor=pairRow(one,session,"Arm","Anchor");check(anchor.find("a")->find("name")->str=="Arm"&&anchor.find("moving")->str=="a","pair orientation puts the moving occurrence first");
    check(anchor.find("frame_minimum_lower_bound_mm")->num>40&&!anchor.find("min_clearance_mm"),"far anchor is settled by the box bound without an exact evaluation");
    V bad=json(R"J({"name":"Sweep","clearance":{"pairs":[{"a":"Anchor","b":"Bridge"}]}})J");rejected(session,"motion_info",bad,"MOTION_CLEARANCE_STATIC_PAIR","static pair");
    rejected(session,"motion_info",json(R"J({"name":"Sweep","clearance":{"pairs":[{"a":"Arm","b":"Post"},{"a":"Post","b":"Arm"}]}})J"),"duplicate pair","duplicate pair");
    rejected(session,"motion_info",json(R"J({"name":"Sweep","clearance":{"pairs":[{"a":"Arm","b":"Nowhere"}]}})J"),"not found","unknown occurrence");
    rejected(session,"motion_info",json(R"J({"name":"Sweep","clearance":{"pairs":[{"a":"Arm","b":"Post"}],"scope":"all"}})J"),"not both","pairs with scope");
    rejected(session,"motion_info",json(R"J({"name":"Sweep","clearance":{"threshold_mm":-1}})J"),"threshold_mm","negative threshold");
    rejected(session,"motion_info",json(R"J({"name":"Sweep","clearance":{"tolerance":1}})J"),"unknown clearance field","larger tolerances are not a request field");
    // Cancellation is polled before the solve and between native requests.
    std::atomic_bool cancel{true};bool cancelled=false;try{(void)inspectMotionClearance(session.document,session.revision,"Sweep",json(R"J({"pairs":[{"a":"Arm","b":"Blade"}],"max_subdivisions":5})J"),&cancel);}catch(const std::exception& e){cancelled=std::string(e.what()).find("MOTION_CLEARANCE_CANCELLED")!=std::string::npos;}check(cancelled,"a set cancel flag stops the check without a result");
    // Capacity: overlapping pins touch at every frame of a long bake.
    apply(session,R"J({"commands":[{"op":"motion.graph.set","name":"Long","graph":{"channels":[{"name":"arm","mate":1,"from":0,"curve":{"type":"linear","duration":20,"to":90}}]}},{"op":"motion.bake","name":"Long","fps":240}]})J");
    const auto limit=rejected(session,"motion_info",json(R"J({"name":"Long","clearance":{"pairs":[{"a":"Pivot A","b":"Pivot B"}]}})J"),"MOTION_CLEARANCE_CAPACITY","4801 exact contact frames");check(limit.find("error_code")->str=="RESOURCE_LIMIT","capacity rejection is a RESOURCE_LIMIT");
    V window=json(R"J({"name":"Long","clearance":{"pairs":[{"a":"Pivot A","b":"Pivot B"}],"from_frame":100,"to_frame":140}})J");const auto windowed=*accepted(session.call("motion_info",window)).find("clearance");
    check(windowed.find("contact_frame_count")->num==41&&windowed.find("pairs")->arr[0].find("status")->str=="interference","a frame window splits the long check: the coincident pins overlap in all 41 frames");
}
void testChainAndLoop(){
    {AgentSession session;apply(session,R"J({"commands":[
      {"op":"param.set","name":"L1","expr":120},{"op":"param.set","name":"L2","expr":90},
      {"op":"feature.add","type":"box","name":"Link body","p":{"dx":20,"dy":10,"dz":10}},{"op":"feature.add","type":"box","name":"Pin","p":{"dx":4,"dy":4,"dz":4}},{"op":"feature.add","type":"box","name":"Wall body","p":{"dx":10,"dy":200,"dz":40}},
      {"op":"assembly.add","name":"Base carrier","feature":0},{"op":"assembly.add","name":"Upper carrier","feature":0},{"op":"assembly.add","name":"Fore carrier","feature":0,"p":{"tx":"L1"}},{"op":"assembly.add","name":"Hand carrier","feature":0,"p":{"tx":"L1+L2"}},
      {"op":"assembly.add","name":"Base","parent":"Base carrier","feature":"Link body","fixed":true},{"op":"assembly.add","name":"Shoulder A","parent":"Base carrier","feature":"Pin"},
      {"op":"assembly.add","name":"Upper arm","parent":"Upper carrier","feature":"Link body"},{"op":"assembly.add","name":"Shoulder B","parent":"Upper carrier","feature":"Pin"},{"op":"assembly.add","name":"Elbow A","parent":"Upper carrier","feature":"Pin","p":{"tx":"L1"}},
      {"op":"assembly.add","name":"Forearm","parent":"Fore carrier","feature":"Link body"},{"op":"assembly.add","name":"Elbow B","parent":"Fore carrier","feature":"Pin"},{"op":"assembly.add","name":"Wrist A","parent":"Fore carrier","feature":"Pin","p":{"tx":"L2"}},
      {"op":"assembly.add","name":"Hand","parent":"Hand carrier","feature":"Link body"},{"op":"assembly.add","name":"Wrist B","parent":"Hand carrier","feature":"Pin"},
      {"op":"assembly.add","name":"Wall","feature":"Wall body","fixed":true,"p":{"tx":260,"ty":-60,"tz":-15}},
      {"op":"assembly.add","name":"Near wall","feature":"Wall body","fixed":true,"p":{"tx":232,"ty":-60,"tz":-15}},
      {"op":"mate.add","type":"revolute","a":"Shoulder A","b":"Shoulder B","reference_a":"origin","reference_b":"origin","limits":{"min":-90,"max":90}},
      {"op":"mate.add","type":"revolute","a":"Elbow A","b":"Elbow B","reference_a":"origin","reference_b":"origin","limits":{"min":-120,"max":120}},
      {"op":"mate.add","type":"revolute","a":"Wrist A","b":"Wrist B","reference_a":"origin","reference_b":"origin"}]})J");
     apply(session,R"J({"commands":[{"op":"motion.graph.set","name":"Sweep","graph":{"channels":[
      {"name":"shoulder","mate":1,"from":0,"curve":{"type":"sequence","items":[{"type":"ease","duration":1,"to":30},{"type":"sine","duration":2,"amplitude":15,"period_s":1},{"type":"ease","duration":1,"to":0}]}},
      {"name":"elbow","mate":2,"expr":"0.5*shoulder+10"},
      {"name":"wrist","mate":3,"from":0,"curve":{"type":"ping_pong","count":2,"item":{"type":"bezier","duration":1,"to":40,"c1":[0.3,0],"c2":[0.7,40]}}}]}},{"op":"motion.bake","name":"Sweep","fps":30}]})J");
     auto chain=clearance(session,R"J({"pairs":[{"a":"Hand","b":"Wall"},{"a":"Hand","b":"Upper arm"},{"a":"Forearm","b":"Wall"}],"threshold_mm":1})J");
     for(const auto& row:chain.find("pairs")->arr)check(row.find("bound_model")->str=="joint_chain"&&row.find("bound_proven")->b,"coupled three-joint serial chain keeps the proven joint-chain bound: "+row.dump().substr(0,200));
     check(chain.find("continuous_certified")->b,"robot wave certified against the wall and its own upper arm: "+chain.find("uncertified_intervals")->dump().substr(0,300));
     const auto& self=pairRow(chain,session,"Hand","Upper arm");check(self.find("moving")->str=="both","self-collision pair moves on both sides");
     std::cout<<"timing: robot wave 121 frames, 3 pairs "<<chain.find("timing_ms")->find("total")->num<<" ms, min "<<chain.find("min_clearance_mm")->num<<" mm, lower bound "<<chain.find("continuous_lower_bound_mm")->num<<" mm, sub-poses "<<chain.find("evaluations")->find("sub_poses")->num<<"\n";
     // Independent check of the bound: exact distance at 4x the bake rate through
     // the same solver never falls below the certified lower bound.
     auto tight=clearance(session,R"J({"pairs":[{"a":"Hand","b":"Near wall"}],"max_subdivisions":6})J");const double bound=tight.find("continuous_lower_bound_mm")->num;
     check(tight.find("continuous_certified")->b&&tight.find("continuous_lower_bound_proven")->b&&bound>0,"near wall certified with a proven positive bound: "+tight.find("uncertified_intervals")->dump().substr(0,300));
     AssemblyManipulationProgram program;std::string error;check(buildAssemblyManipulation(session.document,program,error),error);const auto* motion=findMotion(session.document,"Sweep");std::vector<MotionJointBinding> bindings;check(bindMotionJoints(program,session.document,*motion->find("graph"),bindings,error),error);
     std::map<std::string,double> starts;for(const auto& b:bindings)if(b.mate)starts[b.channel]=b.position;CompiledMotion compiled;check(compileMotionGraph(*motion->find("graph"),session.document.buildEnv(),starts,compiled,error),error);
     MotionSolvedFrames dense;check(solveMotionFrames(session.document,compiled,bindings,motionFrameTimes(compiled.duration,120),dense,error),error);
     size_t hand=0,wall=0;for(size_t i=0;i<dense.baseline.size();++i){if(dense.baseline[i].name=="Hand")hand=i;if(dense.baseline[i].name=="Near wall")wall=i;}
     for(size_t i:{hand,wall})ensureNativeBody(session.document,dense.baseline[i].feature,session.document.evalBodies);auto body=[&](size_t i)->const Body&{for(const auto& b:session.document.evalBodies)if(b.id==dense.baseline[i].feature)return b;throw std::runtime_error("body missing");};
     double denseMin=1e30;for(size_t k=0;k<dense.times.size();++k){const auto report=inspectNativeCollision(body(hand),dense.poses[k][hand],body(wall),dense.poses[k][wall]);denseMin=std::min(denseMin,report.find("min_distance_mm")->num);}
     check(denseMin>=bound-1e-9,"dense exact sampling ("+std::to_string(dense.times.size())+" poses) never goes below the certified bound: "+std::to_string(denseMin)+" >= "+std::to_string(bound));
     check(tight.find("min_clearance_mm")->num>=denseMin-0.05,"measured minimum agrees with the dense minimum");
     std::cout<<"near wall: certified lower bound "<<bound<<" mm, measured minimum "<<tight.find("min_clearance_mm")->num<<" mm, dense 120 Hz minimum "<<denseMin<<" mm over "<<dense.times.size()<<" poses, "<<tight.find("evaluations")->find("sub_poses")->num<<" sub-poses, "<<tight.find("timing_ms")->find("total")->num<<" ms\n";}
    {AgentSession session;apply(session,R"J({"commands":[
      {"op":"param.set","name":"arm_length","expr":60},{"op":"param.set","name":"pivot_spacing","expr":100},{"op":"param.set","name":"arm_angle","expr":25},
      {"op":"feature.add","type":"box","name":"Rail","p":{"dx":12,"dy":112,"dz":8}},{"op":"feature.add","type":"box","name":"Beam","p":{"dx":60,"dy":8,"dz":8}},{"op":"feature.add","type":"box","name":"Pin","p":{"dx":5,"dy":5,"dz":12}},{"op":"feature.add","type":"box","name":"Fence body","p":{"dx":10,"dy":10,"dz":40}},
      {"op":"assembly.add","name":"Ground carrier","feature":0},{"op":"assembly.add","name":"Lower carrier","feature":0,"p":{"rz":"arm_angle"}},
      {"op":"assembly.add","name":"Upright carrier","feature":0,"p":{"tx":"arm_length*cos(arm_angle*pi/180)","ty":"arm_length*sin(arm_angle*pi/180)","rz":90}},{"op":"assembly.add","name":"Upper carrier","feature":0,"p":{"ty":"pivot_spacing","rz":"arm_angle"}},
      {"op":"assembly.add","name":"Chassis","parent":"Ground carrier","feature":"Rail","fixed":true},{"op":"assembly.add","name":"Lower chassis pin","parent":"Ground carrier","feature":"Pin"},{"op":"assembly.add","name":"Upper chassis pin","parent":"Ground carrier","feature":"Pin","p":{"ty":"pivot_spacing"}},
      {"op":"assembly.add","name":"Lower arm","parent":"Lower carrier","feature":"Beam"},{"op":"assembly.add","name":"Lower inner pin","parent":"Lower carrier","feature":"Pin"},{"op":"assembly.add","name":"Lower outer pin","parent":"Lower carrier","feature":"Pin","p":{"tx":"arm_length"}},
      {"op":"assembly.add","name":"Upright","parent":"Upright carrier","feature":"Beam"},{"op":"assembly.add","name":"Upright lower pin","parent":"Upright carrier","feature":"Pin"},{"op":"assembly.add","name":"Upright upper pin","parent":"Upright carrier","feature":"Pin","p":{"tx":"pivot_spacing"}},
      {"op":"assembly.add","name":"Upper arm","parent":"Upper carrier","feature":"Beam"},{"op":"assembly.add","name":"Upper inner pin","parent":"Upper carrier","feature":"Pin"},{"op":"assembly.add","name":"Upper outer pin","parent":"Upper carrier","feature":"Pin","p":{"tx":"arm_length"}},
      {"op":"assembly.add","name":"Fence","feature":"Fence body","fixed":true,"p":{"tx":80,"ty":20,"tz":-10}},
      {"op":"mate.add","type":"revolute","a":"Lower chassis pin","b":"Lower inner pin","reference_a":"origin","reference_b":"origin","limits":{"min":-35,"max":70}},
      {"op":"mate.add","type":"revolute","a":"Lower outer pin","b":"Upright lower pin","reference_a":"origin","reference_b":"origin"},
      {"op":"mate.add","type":"revolute","a":"Upright upper pin","b":"Upper outer pin","reference_a":"origin","reference_b":"origin"},
      {"op":"mate.add","type":"revolute","a":"Upper chassis pin","b":"Upper inner pin","reference_a":"origin","reference_b":"origin"}]})J");
     apply(session,R"J({"commands":[{"op":"motion.graph.set","name":"Sweep","graph":{"channels":[{"name":"lower","mate":1,"curve":{"type":"ease","duration":1,"to":45}}]}},{"op":"motion.bake","name":"Sweep","fps":30}]})J");
     auto loop=clearance(session,R"J({"pairs":[{"a":"Upright","b":"Fence"}]})J");const auto& row=loop.find("pairs")->arr[0];
     check(row.find("bound_model")->str=="endpoint_envelope"&&!row.find("bound_proven")->b,"closed-loop follower uses the assumed endpoint envelope");
     check(!loop.find("continuous_certified")->b&&loop.find("envelope_assumption_certified")&&!loop.find("continuous_lower_bound_proven")->b,"a closed loop is never reported continuous_certified: "+loop.find("envelope_assumption_certified")->dump());
     std::cout<<"timing: four-bar 31 frames "<<loop.find("timing_ms")->find("total")->num<<" ms, min "<<loop.find("min_clearance_mm")->num<<" mm, envelope certified "<<loop.find("envelope_assumption_certified")->dump()<<"\n";}
}
void testContractsAndTiming(){
    const auto tools=AgentSession::tools();bool schema=false;for(const auto& tool:tools.arr)if(tool.find("name")->str=="motion_info"){schema=tool.dump().find("\"clearance\"")!=std::string::npos&&tool.find("annotations")->find("readOnlyHint")->b;}check(schema,"motion_info advertises the read-only clearance request");
    AgentSession session;const auto guide=agentOnboard(session,json(R"J({"topic":"motion","detail":"guide"})J"));const auto text=guide.dump();
    check(text.find("continuous_certified")!=std::string::npos&&text.find("clearance")!=std::string::npos&&text.find("MOTION_BAKE_STALE")!=std::string::npos,"motion onboarding explains clearance, continuous_certified and stale bakes");
    // Timing: the arm fixture plus 120 far fixed blocks, scope all.
    apply(session,kArm);V commands=V::makeArr();for(int i=0;i<120;++i){V a=V::makeObj(),p=V::makeObj();p.set("tx",V::makeNum(400.+60.*(i%12)));p.set("ty",V::makeNum(400.+60.*(i/12)));a.set("op",V::makeStr("assembly.add"));a.set("name",V::makeStr("Far "+std::to_string(i)));a.set("feature",V::makeStr("Anchor body"));a.set("fixed",V::makeBool(true));a.set("p",p);commands.arr.push_back(a);}
    V args=V::makeObj();args.set("commands",commands);args.set("expected_revision",V::makeNum((double)session.revision));accepted(session.call("doc_apply",args));apply(session,kSweep);
    const auto started=std::chrono::steady_clock::now();auto wide=clearance(session,R"J({"scope":"static"})J");const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    check(wide.find("pairs_checked")->num==251,"arm against 126 static solids and pivot B against 125: "+wide.find("pairs_checked")->dump());check(wide.find("evaluations")->find("box_settled_pair_frames")->num>=240*11,"far blocks are settled by the box lower bound");
    std::cout<<"timing: scope static, 126 pairs x 11 frames: "<<ms<<" ms total, "<<wide.find("evaluations")->find("exact_pair_frames")->num<<" exact pair-frames, "<<wide.find("evaluations")->find("native_requests")->num<<" native requests, "<<wide.find("evaluations")->find("sub_poses")->num<<" sub-poses\n";
}
}
int main(){
    try{
        const std::pair<const char*,void(*)()> stages[]={{"analytic",testAnalyticSweep},{"interference",testInterferenceAngles},{"blade",testThinBlade},{"binding",testStaleAndBinding},{"pairs",testPairFilteringAndLimits},{"chain",testChainAndLoop},{"contracts",testContractsAndTiming}};
        for(const auto& stage:stages){std::cerr<<"stage "<<stage.first<<'\n';stage.second();}
        std::cout<<"Motion clearance 046B tests passed: "<<checks<<" checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAILED after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
