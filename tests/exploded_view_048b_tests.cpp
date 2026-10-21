#include "ark/agent.hpp"
#include "ark/exploded_view.hpp"
#include "ark/motion_graph.hpp"
#include "ark/onboarding.hpp"
#include "ark/serialize.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
V rejected(AgentSession& session,const char* tool,const V& args,const char* code,const std::string& message){
    const auto before=documentToJson(session.document);const auto revision=session.revision;const auto reply=session.call(tool,args);
    check(reply.find("isError")->b,message+" must reject");check(reply.find("structuredContent")->dump().find(code)!=std::string::npos,message+" reports "+code+": "+reply.find("structuredContent")->dump().substr(0,400));
    check(session.revision==revision&&documentToJson(session.document)==before,message+" leaves document and revision unchanged");return *reply.find("structuredContent");}
void rejectedApply(AgentSession& session,const char* commands,const char* code,const std::string& message){const auto error=rejected(session,"doc_apply",withRevision(session,commands),code,message);check(error.find("error_code")->str==code,message+" exposes typed error_code "+code+", got "+error.find("error_code")->str);}
int version(const Document& d){const auto* v=json(documentToJson(d,true,false)).find("version");return v?(int)v->num:0;}
u64 node(const Document& d,const char* name){for(const auto& n:d.assembly)if(n.name==name)return n.id;throw std::runtime_error(std::string("node missing: ")+name);}
Vec3 world(const Document& d,const char* name){std::vector<AssemblyPlacement> placements;std::string error;check(resolveAssembly(d,placements,error),error);for(const auto& p:placements)if(p.name==name)return {p.world.m[12],p.world.m[13],p.world.m[14]};throw std::runtime_error(std::string("placement missing: ")+name);}
std::vector<unsigned char> base64(const std::string& text){std::vector<unsigned char> out;int table[256];std::fill(std::begin(table),std::end(table),-1);const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";for(int i=0;i<64;++i)table[(unsigned char)alphabet[i]]=i;
    unsigned buffer=0;int bits=0;for(unsigned char c:text){if(table[c]<0)continue;buffer=buffer<<6|(unsigned)table[c];bits+=6;if(bits>=8){bits-=8;out.push_back((unsigned char)(buffer>>bits&255));}}return out;}
struct Png {std::vector<unsigned char> bytes;unsigned width=0,height=0;V evidence;};
Png renderPng(AgentSession& session,const V& args){const auto reply=session.call("visualize_render",args);if(reply.find("isError")->b)throw std::runtime_error(reply.find("structuredContent")->dump());Png png;
    for(const auto& part:reply.find("content")->arr)if(part.find("type")->str=="image"){check(part.find("mimeType")->str=="image/png","visualize_render returns image/png");png.bytes=base64(part.find("data")->str);}
    check(png.bytes.size()>64&&png.bytes[1]=='P'&&png.bytes[2]=='N'&&png.bytes[3]=='G',"visualize_render returns a PNG signature");png.width=(unsigned)png.bytes[16]<<24|(unsigned)png.bytes[17]<<16|(unsigned)png.bytes[18]<<8|png.bytes[19];png.height=(unsigned)png.bytes[20]<<24|(unsigned)png.bytes[21]<<16|(unsigned)png.bytes[22]<<8|png.bytes[23];
    // Opt-in visual inspection: ARK_048B_PNG_DIR receives every render in call order.
    if(const char* dir=std::getenv("ARK_048B_PNG_DIR")){static int index=0;std::ofstream((std::filesystem::path(dir)/("visualize-"+std::to_string(++index)+".png")).string(),std::ios::binary).write((const char*)png.bytes.data(),(std::streamsize)png.bytes.size());}
    png.evidence=*reply.find("structuredContent")->find("visual_evidence");return png;}

void fixture(AgentSession& session){apply(session,R"({"commands":[
  {"op":"param.set","name":"lid_lift","expr":60},
  {"op":"feature.add","type":"box","name":"Base body","p":{"dx":100,"dy":100,"dz":40}},
  {"op":"feature.add","type":"box","name":"Lid body","p":{"dx":100,"dy":100,"dz":10}},
  {"op":"feature.add","type":"box","name":"Screw body","p":{"dx":6,"dy":6,"dz":20}},
  {"op":"feature.add","type":"box","name":"Cartridge body","p":{"dx":40,"dy":40,"dz":30}},
  {"op":"assembly.add","name":"Base","feature":"Base body","fixed":true},
  {"op":"assembly.add","name":"Cover","feature":0,"p":{"tz":40}},
  {"op":"assembly.add","name":"Lid","parent":"Cover","feature":"Lid body"},
  {"op":"assembly.add","name":"Screw A","parent":"Cover","feature":"Screw body","p":{"tx":10,"ty":10}},
  {"op":"assembly.add","name":"Screw B","parent":"Cover","feature":"Screw body","p":{"tx":84,"ty":84}},
  {"op":"assembly.add","name":"Cartridge","feature":"Cartridge body","p":{"tx":30,"ty":30,"tz":5}}]})");}
const char* kService=R"({"commands":[{"op":"explode.set","name":"Service","description":"Lid, screws then cartridge","steps":[
  {"instances":["Cover"],"direction":"+Z","distance":"lid_lift","label":"Cover"},
  {"instances":["Screw A","Screw B"],"direction":"z","distance":40,"with_previous":true},
  {"instances":["Cartridge"],"direction":[2,0,0],"distance":120}]}]})";

void testAuthoringAndMath(){
    AgentSession session;fixture(session);const int baseVersion=version(session.document);const auto oldDocument=documentToJson(session.document);
    rejected(session,"doc_apply",json(kService),"requires expected_revision","explode authoring without expected_revision");
    auto stale=json(kService);stale.set("expected_revision",V::makeNum((double)session.revision+3));rejected(session,"doc_apply",stale,"Revision conflict","stale expected_revision");
    const auto preview=accepted(session.call("doc_preview",withRevision(session,kService)));check(preview.find("valid")->b&&session.document.explodedViews.isNull(),"doc_preview validates an exploded view without committing it");
    const u64 before=session.revision;const auto placementsBefore=world(session.document,"Cartridge");apply(session,kService);check(session.revision==before+1,"explode.set commits one revision");
    const auto* view=findExplodedView(session.document,"Service");check(view&&view->find("steps")->arr.size()==3,"view persisted with three steps");
    const auto& first=view->find("steps")->arr[0];check(first.find("instances")->arr[0].num==(double)node(session.document,"Cover")&&first.find("direction")->str=="+Z"&&first.find("distance")->str=="lid_lift","names resolve to ids, axes canonicalize and expressions are retained");
    check(view->find("steps")->arr[1].find("direction")->str=="+Z","lowercase unsigned axis canonicalizes");
    const auto after=world(session.document,"Cartridge");check(after.approx(placementsBefore,1e-12),"authoring an exploded view never moves placements");check(version(session.document)==baseVersion,"exploded views do not bump the document format version");
    // Factor math: steps 1 and 2 share slice 0, step 3 owns slice 1.
    ExplodedViewPlan plan;std::string error;check(compileExplodedView(session.document,*view,plan,error),error);check(plan.slices==2&&plan.steps[1].slice==0&&plan.steps[2].slice==1,"with_previous shares a slice");
    close(plan.steps[0].distance,60,1e-12,"distance expression evaluates");close(plan.steps[2].direction.x,1,1e-12,"vector directions normalize");
    close(explodedStepProgress(plan,0,.25),.5,1e-12,"first slice half way at t=0.25");close(explodedStepProgress(plan,1,.25),.5,1e-12,"with_previous step runs together");close(explodedStepProgress(plan,2,.25),0,1e-12,"later slice waits");
    close(explodedStepProgress(plan,0,.75),1,1e-12,"first slice complete");close(explodedStepProgress(plan,2,.75),.5,1e-12,"second slice half way at t=0.75");close(explodedStepProgress(plan,2,1.5),1,1e-12,"factor clamps to 1");close(explodedStepProgress(plan,0,-1),0,1e-12,"factor clamps to 0");
    const std::vector<u64> ids{node(session.document,"Base"),node(session.document,"Lid"),node(session.document,"Screw A"),node(session.document,"Cartridge")};
    auto offsets=explodedOffsets(session.document,plan,ids,.25);check(offsets[0].approx({0,0,0},1e-12),"grounded base stays");close(offsets[1].z,30,1e-9,"group step moves its child at half slice");close(offsets[2].z,30+20,1e-9,"screw accumulates group and own step");check(offsets[3].approx({0,0,0},1e-12),"cartridge waits for its slice");
    offsets=explodedOffsets(session.document,plan,ids,1);close(offsets[1].z,60,1e-9,"lid fully exploded");close(offsets[2].z,100,1e-9,"screw fully exploded");close(offsets[3].x,120,1e-9,"cartridge fully exploded along +X");
    // Factor queries are read-only and never change the revision.
    const auto revision=session.revision;const auto document=documentToJson(session.document);
    for(double t:{0.,.4,1.}){V args=json(R"({"name":"Service"})");args.set("factor",V::makeNum(t));const auto info=accepted(session.call("explode_info",args));check(info.find("state")->find("factor")->num==t,"explode_info reports the factor");}
    const auto info=accepted(session.call("explode_info",json(R"({"name":"Service","factor":1})")));size_t moved=0;for(const auto& row:info.find("state")->find("moved_occurrences")->arr){++moved;if(row.find("name")->str=="Screw B")close(row.find("offset_mm")->arr[2].num,100,1e-9,"explode_info offset for a group descendant");}
    check(moved==4,"lid, both screws and cartridge move");check(session.revision==revision&&documentToJson(session.document)==document,"factor changes never change revision or document");
    const auto list=accepted(session.call("explode_info",V::makeObj()));check(list.find("count")->num==1&&list.find("views")->arr[0].find("slices")->num==2,"explode_info lists the view");
    const auto detail=accepted(session.call("explode_info",json(R"({"name":"Service"})")));const auto& range=detail.find("view")->find("steps")->arr[2].find("factor_range")->arr;close(range[0].num,.5,1e-12,"factor_range start");close(range[1].num,1,1e-12,"factor_range end");
    rejected(session,"explode_info",json(R"({"name":"Nope"})"),"EXPLODE_NOT_FOUND","unknown view inspection");rejected(session,"explode_info",json(R"({"name":"Service","factor":1.5})"),"factor must be within","out-of-range factor");
    // Typed, atomic rejections.
    rejectedApply(session,R"({"commands":[{"op":"explode.set","name":"Bad","steps":[{"instances":[999],"direction":"+Z","distance":5}]}]})","EXPLODE_UNKNOWN_INSTANCE","unknown instance id");
    rejectedApply(session,R"({"commands":[{"op":"explode.set","name":"Bad","steps":[{"instances":["Ghost"],"direction":"+Z","distance":5}]}]})","EXPLODE_UNKNOWN_INSTANCE","unknown instance name");
    rejectedApply(session,R"({"commands":[{"op":"explode.set","name":"Bad","steps":[{"instances":["Lid"],"direction":[0,0,0],"distance":5}]}]})","EXPLODE_ZERO_DIRECTION","zero direction");
    rejectedApply(session,R"({"commands":[{"op":"explode.set","name":"Bad","steps":[{"instances":["Lid"],"direction":"+Z","distance":"lid_lift*1e308"}]}]})","EXPLODE_NONFINITE_DISTANCE","overflowing distance expression");
    rejectedApply(session,R"({"commands":[{"op":"explode.set","name":"Bad","steps":[{"instances":["Lid"],"direction":"+Z","distance":2e6}]}]})","EXPLODE_NONFINITE_DISTANCE","numeric distance beyond 1e6 mm");
    rejectedApply(session,R"({"commands":[{"op":"explode.set","name":"Bad","steps":[{"instances":["Lid"],"direction":"+Z","distance":"lid_lift*1e5"}]}]})","EXPLODE_NONFINITE_DISTANCE","distance beyond 1e6 mm");
    rejectedApply(session,R"({"commands":[{"op":"explode.set","name":"Bad","steps":[{"instances":["Lid"],"direction":"+Z","distance":"missing_param+1"}]}]})","EXPLODE_INVALID_EXPRESSION","unknown parameter");
    rejectedApply(session,R"({"commands":[{"op":"explode.set","name":"Bad","steps":[{"instances":["Lid","Lid"],"direction":"+Z","distance":5}]}]})","EXPLODE_DUPLICATE_INSTANCE","instance twice in a step");
    rejectedApply(session,R"({"commands":[{"op":"explode.set","name":"Bad","steps":[{"instances":["Lid"],"direction":"up","distance":5}]}]})","EXPLODE_INVALID","unknown axis name");
    rejectedApply(session,R"({"commands":[{"op":"explode.set","name":"Bad","steps":[{"instances":["Lid"],"direction":"+Z","distance":5,"with_previous":true}]}]})","EXPLODE_INVALID","first step with_previous");
    rejectedApply(session,R"({"commands":[{"op":"explode.set","name":"Bad","steps":[{"instances":["Lid"],"direction":"+Z","distance":5,"rotate_deg":30}]}]})","EXPLODE_INVALID","unknown step field");
    {std::string many=R"({"commands":[{"op":"explode.set","name":"Bad","steps":[)";for(int i=0;i<65;++i)many+=std::string(i?",":"")+R"({"instances":["Lid"],"direction":"+Z","distance":1})";many+="]}]}";rejectedApply(session,many.c_str(),"EXPLODE_STEP_CAPACITY","65 steps");}
    rejectedApply(session,R"({"commands":[{"op":"explode.set","name":"Service","replace":false,"steps":[{"instances":["Lid"],"direction":"+Z","distance":5}]}]})","EXPLODE_NAME_CONFLICT","duplicate name without replace");
    apply(session,R"({"commands":[{"op":"explode.set","name":"Alt","steps":[{"instances":["Cartridge"],"direction":"-Y","distance":50}]}]})");
    rejectedApply(session,R"({"commands":[{"op":"explode.rename","name":"Alt","new_name":"Service"}]})","EXPLODE_NAME_CONFLICT","rename onto an existing view");
    rejectedApply(session,R"({"commands":[{"op":"explode.remove","name":"Absent"}]})","EXPLODE_NOT_FOUND","remove a missing view");
    {std::string fill=R"({"commands":[)";for(int i=0;i<14;++i)fill+=std::string(i?",":"")+R"({"op":"explode.set","name":"V)"+std::to_string(i)+R"(","steps":[{"instances":["Lid"],"direction":"+Z","distance":1}]})";fill+="]}";apply(session,fill.c_str());check(session.document.explodedViews.arr.size()==16,"sixteen views fit");
        rejectedApply(session,R"({"commands":[{"op":"explode.set","name":"Seventeenth","steps":[{"instances":["Lid"],"direction":"+Z","distance":1}]}]})","EXPLODE_CAPACITY","seventeenth view");
        std::string drop=R"({"commands":[)";for(int i=0;i<14;++i)drop+=std::string(i?",":"")+R"({"op":"explode.remove","name":"V)"+std::to_string(i)+R"("})";drop+="]}";apply(session,drop.c_str());}
    // Atomic batch: a failing later command leaves the earlier explode.set uncommitted.
    rejectedApply(session,R"({"commands":[{"op":"explode.set","name":"Batch","steps":[{"instances":["Lid"],"direction":"+Z","distance":5}]},{"op":"explode.remove","name":"Absent"}]})","EXPLODE_NOT_FOUND","failing batch");check(!findExplodedView(session.document,"Batch"),"failed batch commits nothing");
    apply(session,R"({"commands":[{"op":"explode.rename","name":"Alt","new_name":"Side"}]})");check(findExplodedView(session.document,"Side")&&!findExplodedView(session.document,"Alt"),"rename");
    // Persistence: JSON and binary, old documents, storage validation, undo/redo.
    const auto saved=documentToJson(session.document);check(saved.find("\"exploded_views\"")!=std::string::npos,"exploded views serialize");Document restored;check(documentFromJson(saved,restored,&error),error);check(restored.explodedViews.dump()==session.document.explodedViews.dump(),"JSON round trip");
    Document old;check(documentFromJson(oldDocument,old,&error),error);check(old.explodedViews.isNull(),"a document without exploded views loads with none");
    auto duplicated=json(saved);{V views=*duplicated.find("exploded_views");views.arr.push_back(views.arr[0]);duplicated.set("exploded_views",views);}Document bad;check(!documentFromJson(duplicated.dump(),bad,&error)&&error.find("EXPLODE_NAME_CONFLICT")!=std::string::npos,"duplicate persisted names reject");
    auto zero=json(saved);{V views=*zero.find("exploded_views");V steps=*views.arr[0].find("steps");steps.arr[0].set("direction",json("[0,0,0]"));views.arr[0].set("steps",steps);zero.set("exploded_views",views);}check(!documentFromJson(zero.dump(),bad,&error)&&error.find("EXPLODE_ZERO_DIRECTION")!=std::string::npos,"persisted zero direction rejects");
    const auto path=std::filesystem::temp_directory_path()/("ark-explode-048b-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ark");
    check(saveDocument(session.document,path.string().c_str()),"binary save");Document loaded;check(loadDocument(path.string().c_str(),loaded,&error),error);std::filesystem::remove(path);check(loaded.explodedViews.dump()==session.document.explodedViews.dump(),"binary .ark round trip");
    const auto beforeUndo=session.document.explodedViews.dump();accepted(session.call("doc_undo",V::makeObj()));check(findExplodedView(session.document,"Alt")&&!findExplodedView(session.document,"Side"),"undo restores the previous view name");
    accepted(session.call("doc_redo",V::makeObj()));check(session.document.explodedViews.dump()==beforeUndo,"redo restores views exactly");
    // Removing an instance leaves the view loadable and reports it missing.
    apply(session,R"({"commands":[{"op":"assembly.remove","id":"Cartridge"}]})");const auto missing=accepted(session.call("explode_info",json(R"({"name":"Service"})")));check(missing.find("view")->find("missing_instances")->arr.size()==1,"removed instance is reported missing");
    Document reloaded;check(documentFromJson(documentToJson(session.document),reloaded,&error),error);
    apply(session,R"({"commands":[{"op":"explode.remove","name":"Service"},{"op":"explode.remove","name":"Side"}]})");check(session.document.explodedViews.isNull()&&documentToJson(session.document,true,false).find("exploded_views")==std::string::npos,"removing every view drops the field");
}
void testProposal(){
    AgentSession session;fixture(session);const auto revision=session.revision;const auto document=documentToJson(session.document);
    const auto result=accepted(session.call("explode_info",json(R"({"propose":{}})")));const auto& proposal=*result.find("propose")->find("proposal");
    check(session.revision==revision&&documentToJson(session.document)==document,"propose is read-only");check(proposal.find("op")->str=="explode.set"&&!proposal.find("steps")->arr.empty(),"propose returns an explode.set command");
    check(result.find("propose")->find("base_units")->arr[0].num==(double)node(session.document,"Base"),"the grounded base stays");
    bool lidUp=false;for(const auto& step:proposal.find("steps")->arr)for(const auto& id:step.find("instances")->arr)if(id.num==(double)node(session.document,"Lid"))lidUp=step.find("direction")->str=="+Z"&&step.find("distance")->num>0;check(lidUp,"the lid above the base explodes upward");
    V args=V::makeObj(),commands=V::makeArr();commands.arr.push_back(proposal);args.set("commands",commands);args.set("expected_revision",V::makeNum((double)session.revision));accepted(session.call("doc_apply",args));check(findExplodedView(session.document,"Exploded view")!=nullptr,"a proposal commits unchanged");
    const auto top=accepted(session.call("explode_info",json(R"({"propose":{"scope":"top","axis":"+Z","gap_mm":5}})")));bool cover=false;for(const auto& step:top.find("propose")->find("proposal")->find("steps")->arr)for(const auto& id:step.find("instances")->arr)cover|=id.num==(double)node(session.document,"Cover");check(cover,"top scope proposes the cover group as one unit");
}
void glassFixture(AgentSession& session){apply(session,R"({"commands":[
  {"op":"param.set","name":"glass_travel","expr":420},
  {"op":"feature.add","type":"box","name":"Rail body","p":{"dx":20,"dy":20,"dz":600}},
  {"op":"feature.add","type":"box","name":"Glass pane","p":{"dx":800,"dy":5,"dz":450}},
  {"op":"feature.add","type":"box","name":"Carriage body","p":{"dx":40,"dy":20,"dz":30}},
  {"op":"feature.add","type":"box","name":"Trim body","p":{"dx":800,"dy":10,"dz":20}},
  {"op":"assembly.add","name":"Rails","feature":"Rail body","fixed":true},
  {"op":"assembly.add","name":"Glass","feature":"Glass pane"},
  {"op":"assembly.add","name":"Carriage","feature":"Carriage body"},
  {"op":"assembly.add","name":"Trim","feature":"Trim body","fixed":true,"p":{"tz":620}},
  {"op":"mate.add","type":"prismatic","a":"Rails","b":"Glass","reference_a":"Z","reference_b":"Z","limits":{"min":"-glass_travel","max":0}},
  {"op":"mate.add","type":"rigid","a":"Glass","b":"Carriage"},
  {"op":"explode.set","name":"Trim off","steps":[{"instances":["Trim"],"direction":"+Y","distance":200}]}]})");}
void testMotionBinding(){
    AgentSession session;glassFixture(session);
    apply(session,R"({"commands":[{"op":"motion.graph.set","name":"Lower and explode","graph":{"channels":[
      {"name":"glass","mate":1,"from":0,"curve":{"type":"ease","duration":2,"to":-200}},
      {"name":"explode","explode_view":"Trim off","from":0,"curve":{"type":"sequence","items":[{"type":"hold","duration":1},{"type":"linear","duration":1,"to":1}]}}]}},{"op":"motion.bake","name":"Lower and explode","fps":10}]})");
    const auto* motion=findMotion(session.document,"Lower and explode");check(motion&&motion->find("bake"),"explode-linked motion bakes");const auto& row=motion->find("bake")->find("channels")->arr[1];
    check(row.find("explode_view")->str=="Trim off"&&row.find("unit")->str=="factor","bake channel records explode_view and unit factor");
    BakedMotionFrames frames;std::string error;check(decodeBakedMotion(*motion,frames,error),error);check(frames.frameCount==21&&frames.explodeViews.size()==2&&frames.explodeViews[1]=="Trim off"&&frames.explodeViews[0].empty(),"decoded frames carry the explode binding");
    close(frames.coordinates[10*2+1],0,1e-6,"factor holds 0 at t=1 s");close(frames.coordinates[15*2+1],.5,1e-6,"factor 0.5 at t=1.5 s");close(frames.coordinates[20*2+1],1,1e-6,"factor 1 at the end");close(frames.coordinates[20*2],-200,1e-3,"joint channel is baked alongside");
    check(std::none_of(frames.nodes.begin(),frames.nodes.end(),[&](u64 n){return n==node(session.document,"Trim");}),"the bake stores no exploded display pose");
    const auto info=accepted(session.call("motion_info",json(R"({"name":"Lower and explode","time_s":1.5})")));const auto& channel=info.find("evaluation")->find("channels")->arr[1];check(channel.find("unit")->str=="factor"&&channel.find("explode_view")->str=="Trim off","motion_info reports unit factor and the bound view");close(channel.find("value")->num,.5,1e-9,"motion_info evaluates the factor");
    check(info.find("diagnostics")->find("channels")->arr[1].find("explode_view")->str=="Trim off","diagnostics name the bound view");
    const auto views=accepted(session.call("explode_info",V::makeObj()));check(views.find("views")->arr[0].find("driven_by_motions")->arr[0].str=="Lower and explode","explode_info lists the driving motion");
    rejectedApply(session,R"({"commands":[{"op":"explode.remove","name":"Trim off"}]})","EXPLODE_VIEW_IN_USE","removing a driven view");
    rejectedApply(session,R"({"commands":[{"op":"explode.rename","name":"Trim off","new_name":"Other"}]})","EXPLODE_VIEW_IN_USE","renaming a driven view");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Over","graph":{"channels":[{"name":"x","explode_view":"Trim off","from":0,"curve":{"type":"linear","duration":1,"to":1.5}}]}}]})"),"MOTION_EXPLODE_RANGE","factor above 1");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Under","graph":{"channels":[{"name":"x","explode_view":"Trim off","from":0,"curve":{"type":"sine","duration":1,"amplitude":0.5,"period_s":1}}]}}]})"),"MOTION_EXPLODE_RANGE","factor below 0");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Ghost","graph":{"channels":[{"name":"x","explode_view":"Nowhere","from":0,"curve":{"type":"linear","duration":1,"to":1}}]}}]})"),"MOTION_UNKNOWN_EXPLODE_VIEW","unknown exploded view");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Twice","graph":{"channels":[{"name":"a","explode_view":"Trim off","from":0,"curve":{"type":"linear","duration":1,"to":1}},{"name":"b","explode_view":"Trim off","expr":"a"}]}}]})"),"MOTION_DUPLICATE_EXPLODE_VIEW","two channels on one view");
    rejected(session,"doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Both","graph":{"channels":[{"name":"a","mate":1,"explode_view":"Trim off","from":0,"curve":{"type":"linear","duration":1,"to":-1}}]}}]})"),"not both","mate and explode_view on one channel");
    apply(session,R"({"commands":[{"op":"motion.graph.set","name":"Explode only","graph":{"channels":[{"name":"lift","explode_view":"Trim off","from":0,"curve":{"type":"ease","duration":1,"to":1}}]}},{"op":"motion.bake","name":"Explode only","fps":4}]})");
    check(findMotion(session.document,"Explode only")->find("bake")->find("instances")->arr.empty(),"an explode-only motion bakes with no moving joint instances");
    // visualize_render at a baked time applies the motion's factor; explicit conflicts and stale bakes reject.
    const auto revision=session.revision;const auto document=documentToJson(session.document);
    V args=json(R"({"motion":"Lower and explode","motion_time_s":1.5,"resolution":"640x400","samples":1})");args.set("expected_revision",V::makeNum((double)revision));auto png=renderPng(session,args);
    check(png.width==640&&png.height==400,"motion render has the requested size");const auto& motionEvidence=*png.evidence.find("motion");check(motionEvidence.find("frame")->num==15&&motionEvidence.find("explode_channels")->arr[0].find("factor")->num==.5,"render applies the baked factor at the frame");
    check(png.evidence.find("exploded")->arr[0].find("moved_occurrences")->num==1,"the trim occurrence moves in the render");check(session.revision==revision&&documentToJson(session.document)==document,"motion render is read-only");
    rejected(session,"visualize_render",json(R"({"motion":"Lower and explode","exploded_view":"Trim off"})"),"VISUALIZE_EXPLODE_CONFLICT","explicit view already driven by the motion");
    apply(session,R"({"commands":[{"op":"param.set","name":"glass_travel","expr":400}]})");rejected(session,"visualize_render",json(R"({"motion":"Lower and explode"})"),"VISUALIZE_MOTION_STALE","stale bake");
}
void testVisualize(){
    AgentSession session;fixture(session);apply(session,kService);apply(session,R"({"commands":[{"op":"bom.initialize"},{"op":"feature.edit","id":"Screw body","part":{"part_number":"M6-20","description":"Screw","procurement":"buy"}}]})");
    const auto revision=session.revision;const auto document=documentToJson(session.document);
    V args=json(R"({"exploded_view":"Service","explode_factor":1,"balloons":true,"bom_table":true,"title":"Service assembly","lighting":"technical","background":"white"})");args.set("expected_revision",V::makeNum((double)revision));
    auto png=renderPng(session,args);check(png.width==1024&&png.height==640&&png.bytes.size()>4000,"default render is a non-empty 1024x640 PNG");
    check(png.evidence.find("capture_source")->str=="software_visualize"&&png.evidence.find("authority")->str=="display_mesh"&&png.evidence.find("render_label")->str.find("display tessellation")!=std::string::npos,"render is labelled as display tessellation");
    check(png.evidence.find("bom")->arr.size()==4,"BOM table lists four definitions");bool screws=false;for(const auto& row:png.evidence.find("bom")->arr)if(row.find("name")->str=="Screw body")screws=row.find("quantity")->num==2&&row.find("part_number")->str=="M6-20";check(screws,"screw row has quantity 2 and its part number");
    check(png.evidence.find("balloons")->arr.size()==4,"every visible BOM item gets one balloon");for(const auto& b:png.evidence.find("balloons")->arr){const auto& a=b.find("anchor_px")->arr;check(a[0].num>=0&&a[0].num<1024&&a[1].num>=0&&a[1].num<640&&b.find("node"),"balloon anchors lie in the image on an occurrence");}
    check(png.evidence.find("exploded")->arr[0].find("factor")->num==1&&png.evidence.find("samples_per_pixel")->num==4,"explode state and sample count are recorded");check(session.revision==revision&&documentToJson(session.document)==document,"render is read-only");
    auto collapsed=json(R"({"exploded_view":"Service","explode_factor":0,"samples":1,"resolution":"1280x720","view":"front","lighting":"dramatic","background":"dark"})");png=renderPng(session,collapsed);check(png.width==1280&&png.height==720,"1280x720 preset");
    png=renderPng(session,json(R"({"view":"custom","camera":{"direction":[0,-1,0.3],"up":[0,0,1]},"projection":"orthographic","samples":1})"));check(png.width==1024&&png.evidence.find("projection")->str=="orthographic","custom orthographic camera");
    rejected(session,"visualize_render",json(R"({"exploded_view":"Nope"})"),"EXPLODE_NOT_FOUND","unknown view");rejected(session,"visualize_render",json(R"({"explode_factor":0.5})"),"VISUALIZE_INVALID","factor without view");
    rejected(session,"visualize_render",json(R"({"resolution":"8000x8000"})"),"VISUALIZE_INVALID","unsupported resolution");rejected(session,"visualize_render",json(R"({"samples":3})"),"VISUALIZE_INVALID","unsupported sample count");
    rejected(session,"visualize_render",json(R"({"motion":"None"})"),"MOTION_NOT_FOUND","unknown motion");auto staleArgs=json(R"({})");staleArgs.set("expected_revision",V::makeNum((double)revision+1));rejected(session,"visualize_render",staleArgs,"Revision conflict","stale expected_revision");
    // Overlay primitives draw into the image and the layout keeps balloons clear of the table.
    std::vector<VisualizeBomItem> items{{1,1,1,"Base body",""},{2,2,2,"Screw body","M6-20"}};std::vector<VisualizeAnchor> anchors{{1,300,300},{2,320,310}};const double silhouette[4]={200,200,420,420};
    const auto layout=layoutVisualizeOverlay(1024,640,items,anchors,silhouette,true,true);check(layout.rows.size()==3&&layout.balloons.size()==2,"layout rows and balloons");
    for(const auto& b:layout.balloons)check(b.bx+b.radius<=layout.tableX+1e-9&&b.bx-b.radius>=0&&b.by-b.radius>=0&&b.by+b.radius<=640,"balloons stay inside the image and left of the table");
    check(std::hypot(layout.balloons[0].bx-layout.balloons[1].bx,layout.balloons[0].by-layout.balloons[1].by)>=2*layout.balloons[0].radius,"balloons do not overlap");
    Image canvas;canvas.w=1024;canvas.h=640;canvas.px.assign(1024*640*3,255);drawVisualizeOverlay(canvas,layout,false);size_t ink=0;for(u8 c:canvas.px)ink+=c<128;check(ink>500,"overlay draws balloons, leaders and table text");
    const auto tools=AgentSession::tools();bool explodeTool=false,renderTool=false;for(const auto& tool:tools.arr){const auto name=tool.find("name")->str;if(name=="explode_info")explodeTool=tool.find("annotations")->find("readOnlyHint")->b;if(name=="visualize_render")renderTool=tool.find("annotations")->find("readOnlyHint")->b;}
    check(explodeTool&&renderTool,"explode_info and visualize_render are discoverable read-only tools");
    V topic=V::makeObj();topic.set("topic",V::makeStr("exploded_views"));const auto guide=agentOnboard(session,topic);check(guide.find("rules")&&guide.find("motion_binding_example")&&guide.find("authorization")->str.find("explicitly")!=std::string::npos,"exploded_views onboarding topic");
    V motionTopic=V::makeObj();motionTopic.set("topic",V::makeStr("motion"));check(agentOnboard(session,motionTopic).find("explode_binding")!=nullptr,"motion topic points to the explode binding");
}
}
int main(){
    try{testAuthoringAndMath();testProposal();testMotionBinding();testVisualize();}
    catch(const std::exception& error){std::cerr<<"exploded-view-048b failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
    std::cout<<"exploded-view-048b passed "<<checks<<" checks\n";return 0;
}
