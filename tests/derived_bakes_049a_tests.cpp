#include "ark/agent.hpp"
#include "ark/derived_bakes.hpp"
#include "ark/iteration_history.hpp"
#include "ark/motion_graph.hpp"
#include "ark/serialize.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

// 049A derived bakes: receipts in authoring and journal, frames from the cache,
// the derived section or a verified re-bake, explicit journal migration and the
// frame-step fix. Synthetic fixtures only; no user document is read.
using namespace ark;
using V=arkjson::Value;
namespace fs=std::filesystem;
namespace {
int checks=0;
void check(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
V json(const std::string& source){V value;if(!arkjson::parse(source,value))throw std::runtime_error("test JSON parses: "+source.substr(0,80));return value;}
V accepted(const V& reply){if(reply.find("isError")->b)throw std::runtime_error(reply.find("structuredContent")->dump().substr(0,1500));return *reply.find("structuredContent");}
V withRevision(const AgentSession& session,const std::string& source){auto args=json(source);args.set("expected_revision",V::makeNum((double)session.revision));return args;}
V commit(AgentSession& session,const std::string& source){return accepted(session.call("doc_apply",withRevision(session,source)));}
V failed(AgentSession& session,const char* tool,const V& args,const std::string& code,const std::string& message){
    const auto before=documentToJson(session.document);const auto revision=session.revision;const auto reply=session.call(tool,args);
    check(reply.find("isError")->b,message+" must reject");const auto& out=*reply.find("structuredContent");check(out.dump().find(code)!=std::string::npos,message+" reports "+code+": "+out.dump().substr(0,600));
    check(session.revision==revision&&documentToJson(session.document)==before,message+" leaves document and revision unchanged");return out;}
std::string utf8(const fs::path& p){const auto s=p.u8string();return std::string(s.begin(),s.end());}
fs::path scratch(const char* name){auto dir=fs::temp_directory_path()/("ark-derived-bakes-049a-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directories(dir);return dir/name;}
size_t payloadRefs(const Document& d){size_t references=0,bytes=0;iterationInlineBakePayloads(d.iterationHistory,references,bytes);return references;}
V history(AgentSession& session){return accepted(session.call("history_info",json(R"({"limit":64})")));}
const V& bakeOf(const Document& d,const char* name){const auto* motion=findMotion(d,name);check(motion&&motion->find("bake"),std::string("motion is baked: ")+name);return *motion->find("bake");}
BakedMotionFrames frames(Document& d,const char* name,BakedMotionFramesSource* source=nullptr){BakedMotionFrames out;std::string error;check(resolveBakedMotion(d,*findMotion(d,name),out,error,source),error);return out;}
bool samePoses(const BakedMotionFrames& a,const BakedMotionFrames& b){if(a.poses.size()!=b.poses.size()||a.coordinates!=b.coordinates||a.nodes!=b.nodes)return false;for(size_t i=0;i<a.poses.size();++i)for(int e=0;e<16;++e)if(a.poses[i].m[e]!=b.poses[i].m[e])return false;return true;}

// A kiosk-like assembly: cabinet, swinging door, tilting screen and a drawer;
// 15 part occurrences, 11 of them move. Three motions as in the reported case.
const char* kKiosk=R"J({"commands":[
  {"op":"param.set","name":"door_open","expr":100},
  {"op":"feature.add","type":"box","name":"Cabinet body","p":{"dx":600,"dy":400,"dz":1600}},{"op":"feature.add","type":"box","name":"Pin","p":{"dx":10,"dy":10,"dz":10}},
  {"op":"feature.add","type":"box","name":"Door body","p":{"dx":500,"dy":20,"dz":700}},{"op":"feature.add","type":"box","name":"Handle body","p":{"dx":20,"dy":30,"dz":120}},
  {"op":"feature.add","type":"box","name":"Window body","p":{"dx":300,"dy":5,"dz":200}},{"op":"feature.add","type":"box","name":"Arm body","p":{"dx":40,"dy":40,"dz":300}},
  {"op":"feature.add","type":"box","name":"Screen body","p":{"dx":500,"dy":30,"dz":350}},{"op":"feature.add","type":"box","name":"Bezel body","p":{"dx":520,"dy":10,"dz":370}},
  {"op":"feature.add","type":"box","name":"Rail body","p":{"dx":20,"dy":450,"dz":20}},{"op":"feature.add","type":"box","name":"Drawer body","p":{"dx":500,"dy":380,"dz":150}},{"op":"feature.add","type":"box","name":"Front body","p":{"dx":560,"dy":20,"dz":170}},
  {"op":"assembly.add","name":"Cabinet carrier","feature":0},{"op":"assembly.add","name":"Door carrier","feature":0},{"op":"assembly.add","name":"Screen carrier","feature":0,"p":{"tz":1600}},
  {"op":"assembly.add","name":"Cabinet","parent":"Cabinet carrier","feature":"Cabinet body","fixed":true},{"op":"assembly.add","name":"Door hinge A","parent":"Cabinet carrier","feature":"Pin"},{"op":"assembly.add","name":"Screen hinge A","parent":"Cabinet carrier","feature":"Pin","p":{"tz":1600}},
  {"op":"assembly.add","name":"Door hinge B","parent":"Door carrier","feature":"Pin"},{"op":"assembly.add","name":"Door","parent":"Door carrier","feature":"Door body"},{"op":"assembly.add","name":"Door handle","parent":"Door carrier","feature":"Handle body","p":{"tx":440}},{"op":"assembly.add","name":"Door window","parent":"Door carrier","feature":"Window body","p":{"tx":100,"tz":300}},
  {"op":"assembly.add","name":"Screen hinge B","parent":"Screen carrier","feature":"Pin"},{"op":"assembly.add","name":"Screen arm","parent":"Screen carrier","feature":"Arm body"},{"op":"assembly.add","name":"Screen","parent":"Screen carrier","feature":"Screen body","p":{"tz":300}},{"op":"assembly.add","name":"Screen bezel","parent":"Screen carrier","feature":"Bezel body","p":{"tz":290}},
  {"op":"assembly.add","name":"Drawer rails","feature":"Rail body","fixed":true},{"op":"assembly.add","name":"Drawer","feature":"Drawer body"},{"op":"assembly.add","name":"Drawer front","feature":"Front body"},{"op":"assembly.add","name":"Drawer handle","feature":"Handle body"},
  {"op":"mate.add","type":"revolute","a":"Door hinge A","b":"Door hinge B","reference_a":"origin","reference_b":"origin","limits":{"min":0,"max":120}},
  {"op":"mate.add","type":"revolute","a":"Screen hinge A","b":"Screen hinge B","reference_a":"origin","reference_b":"origin","limits":{"min":-30,"max":45}},
  {"op":"mate.add","type":"prismatic","a":"Drawer rails","b":"Drawer","reference_a":"Z","reference_b":"Z","limits":{"min":-350,"max":0}},
  {"op":"mate.add","type":"rigid","a":"Drawer","b":"Drawer front"},{"op":"mate.add","type":"rigid","a":"Drawer","b":"Drawer handle"}]})J";
const char* kKioskMotions=R"J({"commands":[
  {"op":"motion.graph.set","name":"Door swing","graph":{"channels":[{"name":"door","mate":1,"from":0,"curve":{"type":"sequence","items":[{"type":"ease","duration":3,"to":"door_open"},{"type":"hold","duration":1},{"type":"ease","duration":3,"to":0}]}}]}},
  {"op":"motion.graph.set","name":"Screen tilt","graph":{"channels":[{"name":"tilt","mate":2,"from":0,"curve":{"type":"sequence","items":[{"type":"ease","duration":2,"to":35},{"type":"sine","duration":4,"amplitude":5,"period_s":2},{"type":"ease","duration":2,"to":0}]}}]}},
  {"op":"motion.graph.set","name":"Drawer cycle","graph":{"channels":[{"name":"drawer","mate":3,"from":0,"curve":{"type":"sequence","items":[{"type":"ease","duration":2,"to":-300},{"type":"hold","duration":1},{"type":"ease","duration":2,"to":0}]}}]}},
  {"op":"motion.bake","name":"Door swing","fps":30},{"op":"motion.bake","name":"Screen tilt","fps":30},{"op":"motion.bake","name":"Drawer cycle","fps":30}]})J";
std::string bakeAll(int fps){const std::string f=std::to_string(fps);return R"({"commands":[{"op":"motion.bake","name":"Door swing","fps":)"+f+R"(},{"op":"motion.bake","name":"Screen tilt","fps":)"+f+R"(},{"op":"motion.bake","name":"Drawer cycle","fps":)"+f+"}]}";}
const int kRebakes[]={60,90,120,100,80,110,70,120,96,84,108,72,116,88,104,92};
// Re-bakes like the reported document until the journal rejects an edit (legacy) or count is reached.
size_t rebakeKiosk(AgentSession& session,size_t limit,V* rejection){
    size_t done=0;for(int fps:kRebakes){if(done>=limit)break;const auto reply=session.call("doc_apply",withRevision(session,bakeAll(fps)));
        if(reply.find("isError")->b){check(rejection!=nullptr,"re-bake rejected: "+reply.find("structuredContent")->dump().substr(0,400));*rejection=*reply.find("structuredContent");break;}++done;
        if(done%3==0){const auto reply2=session.call("doc_apply",withRevision(session,R"({"commands":[{"op":"param.set","name":"door_open","expr":)"+std::to_string(90+done)+R"(},{"op":"motion.bake","name":"Door swing","fps":60}]})"));
            if(reply2.find("isError")->b){check(rejection!=nullptr,"param re-bake rejected");*rejection=*reply2.find("structuredContent");break;}}}
    return done;
}

void testReceiptsOnly(){
    AgentSession session;commit(session,kKiosk);commit(session,kKioskMotions);rebakeKiosk(session,4,nullptr);
    const auto saved=documentToJson(session.document);check(saved.find("frames_f32le_base64")==std::string::npos&&saved.find("coordinates_f32le_base64")==std::string::npos,"authoring and journal JSON contain no frame payloads after bakes");
    check(payloadRefs(session.document)==0&&history(session).find("inline_bake_payloads")->find("references")->num==0,"history_info reports no inline payloads");
    for(const char* name:{"Door swing","Screen tilt","Drawer cycle"}){const auto& bake=bakeOf(session.document,name);check(bakeIsReceipt(bake)&&!bakeHasInlineFrames(bake)&&bake.find("frames_sha256")->str.size()==64&&bake.find("coordinates_sha256")->str.size()==64,std::string("receipt digests for ")+name);
        check(bake.find("graph_digest")&&bake.find("source_digest")&&bake.find("source_revision")&&bake.find("solver")&&bake.find("clearance_checked")&&bake.find("format")->str=="ark.baked_motion.v1","receipt keeps every evidence field");}
    const auto& door=bakeOf(session.document,"Door swing");check(door.find("instances")->arr.size()==4,"door swing moves the door, its handle, window and hinge pin");
    check(json(saved).find("version")->num==49,"receipts select format 49");
    const auto list=accepted(session.call("motion_info",V::makeObj()));for(const auto& row:list.find("motions")->arr)check(row.find("frames_storage")->str=="derived"&&row.find("frames_cached")->b,"motion_info lists derived, cached frames");
    const auto detail=accepted(session.call("motion_info",json(R"({"name":"Door swing"})")));check(detail.find("bake")->find("frames_storage")->str=="derived"&&detail.find("bake")->find("frames_note")->str.find("derived")!=std::string::npos&&!detail.find("bake")->find("frames_f32le_base64"),"bake summary says frames are derived");
}

void testRebakeVerification(){
    AgentSession session;commit(session,kKiosk);commit(session,kKioskMotions);commit(session,bakeAll(60));
    BakedMotionFramesSource source;const auto cached=frames(session.document,"Door swing",&source);check(source.source=="memory"&&source.verified,"fresh bake plays from memory");
    clearBakedMotionCache();const auto stats=bakedMotionCacheStats();check(stats.entries==0,"cache cleared");
    const auto rebaked=frames(session.document,"Door swing",&source);check(source.source=="rebake"&&source.verified&&source.mismatch.empty(),"uncached receipt re-bakes and reproduces frames_sha256");
    check(samePoses(cached,rebaked),"re-baked frames are bit-identical to the original bake");check(bakedMotionCacheStats().rebakes>=1&&bakedMotionFramesCached(bakeOf(session.document,"Door swing")),"verified re-bake is cached");
    BakedMotionFrames decoded;std::string error;check(decodeBakedMotion(*findMotion(session.document,"Door swing"),decoded,error)&&samePoses(decoded,cached),"decodeBakedMotion serves the cached receipt");
    // motion_info frame through the resolver, from a cold cache.
    clearBakedMotionCache();const auto frame=accepted(session.call("motion_info",json(R"({"name":"Screen tilt","frame":120})")));check(frame.find("frames")->find("source")->str=="rebake"&&frame.find("frames")->find("verified")->b,"motion_info frame reports a verified re-bake");
    // A different solver result: the receipt claims other frames. The re-bake is labelled, never accepted as the recorded evidence.
    auto tampered=json(documentToJson(session.document,true,false));
    for(auto& f:tampered.obj)if(f.first=="motions")for(auto& row:f.second.arr)if(row.find("name")->str=="Drawer cycle")for(auto& g:row.obj)if(g.first=="bake")g.second.set("frames_sha256",V::makeStr(std::string(64,'a')));
    Document other;check(documentFromJson(tampered.dump(),other,&error),error);other.recompute();clearBakedMotionCache();
    const auto mismatch=frames(other,"Drawer cycle",&source);check(source.source=="rebake"&&!source.verified&&source.mismatch.find("frames_sha256")!=std::string::npos&&source.staleReasons.empty(),"a receipt the re-bake cannot reproduce is reported as mismatch: "+source.mismatch);
    check(mismatch.frameCount>0&&bakedMotionCacheStats().rebakeMismatches>=1,"mismatched re-bake is still playable and counted");
    const auto rebakesBefore=bakedMotionCacheStats().rebakes;frames(other,"Drawer cycle",&source);check(!source.verified&&bakedMotionCacheStats().rebakes==rebakesBefore,"a known mismatch is memoized, not solved again");
    // A stale receipt without cached frames re-bakes the current assembly and says so.
    commit(session,R"({"commands":[{"op":"param.set","name":"door_open","expr":80}]})");clearBakedMotionCache();frames(session.document,"Door swing",&source);
    check(!source.verified&&source.staleReasons.size()==1&&source.staleReasons[0]=="assembly_changed","stale receipt re-bake is flagged assembly_changed and unverified");
    const auto staleFrame=accepted(session.call("motion_info",json(R"({"name":"Door swing","frame":10})")));check(!staleFrame.find("frames")->find("verified")->b&&staleFrame.find("frames")->find("mismatch"),"motion_info frame reports the mismatch");
    V render=json(R"({"motion":"Door swing","motion_time_s":1,"resolution":"640x400","samples":1})");render.set("expected_revision",V::makeNum((double)session.revision));failed(session,"visualize_render",render,"VISUALIZE_MOTION_STALE","stale derived bake render");
    commit(session,R"({"commands":[{"op":"motion.bake","name":"Door swing","fps":60}]})");clearBakedMotionCache();render.set("expected_revision",V::makeNum((double)session.revision));
    const auto reply=session.call("visualize_render",render);check(!reply.find("isError")->b,"visualize_render at a motion time after re-bake: "+reply.find("structuredContent")->dump().substr(0,300));
    const auto& evidence=*reply.find("structuredContent")->find("visual_evidence")->find("motion")->find("frames");check(evidence.find("source")->str=="rebake"&&evidence.find("verified")->b,"visualize_render frames come from a verified re-bake");
}

void testDerivedSection(){
    AgentSession session;commit(session,kKiosk);commit(session,kKioskMotions);commit(session,bakeAll(60));
    const auto reference=frames(session.document,"Screen tilt");const auto path=scratch("kiosk.ark");
    V save=V::makeObj();save.set("path",V::makeStr(utf8(path)));const auto receipt=accepted(session.call("doc_save",save));
    check(receipt.find("saved_motion_frames")->find("entries_saved")->num==3&&receipt.find("saved_motion_frames")->find("entries_not_cached")->num==0,"save writes three cached frame entries to the derived section");
    clearBakedMotionCache();Document loaded;std::string error;check(loadDocument(utf8(path).c_str(),loaded,&error),error);
    const auto* staged=loaded.derivedGeometryStatus.find("motion_frames");check(staged&&staged->find("entries_staged")->num==3&&staged->find("entries_discarded")->num==0,"reopen stages the three entries: "+loaded.derivedGeometryStatus.dump().substr(0,300));
    BakedMotionFramesSource source;check(samePoses(frames(loaded,"Screen tilt",&source),reference)&&source.source=="derived"&&source.verified,"frames come from the derived section after reopen");
    check(documentToJson(loaded)==documentToJson(session.document),"derived frames never enter the authoring");
    // Without the derived section (JSON export) the frames re-bake to the same digests.
    const auto jsonPath=scratch("kiosk.arkjson");save.set("path",V::makeStr(utf8(jsonPath)));accepted(session.call("doc_save",save));clearBakedMotionCache();
    Document plain;check(loadDocument(utf8(jsonPath).c_str(),plain,&error),error);plain.recompute();const auto plainFrames=frames(plain,"Screen tilt",&source);check(source.source=="rebake"&&source.verified&&samePoses(plainFrames,reference),"without a derived section playback re-bakes and verifies: "+source.json().dump());
    // An .ark saved with an empty cache omits the frames and still reopens; frames re-bake.
    clearBakedMotionCache();const auto bare=scratch("bare.ark");save.set("path",V::makeStr(utf8(bare)));const auto bareReceipt=accepted(session.call("doc_save",save));check(bareReceipt.find("saved_motion_frames")->find("entries_not_cached")->num==3&&bareReceipt.find("saved_motion_frames")->find("entries_saved")->num==0,"uncached frames are omitted, not invented");
    Document bareLoaded;check(loadDocument(utf8(bare).c_str(),bareLoaded,&error),error);bareLoaded.recompute();check(samePoses(frames(bareLoaded,"Screen tilt",&source),reference)&&source.source=="rebake","omitted frames re-bake");
    // Damaged or foreign entries are discarded individually, never accepted.
    for(const char* name:{"Door swing","Screen tilt","Drawer cycle"})frames(session.document,name);
    DocumentDerivedPayloads records;MotionDerivedReport report;appendMotionDerivedPayloads(session.document,records,64u*1024u*1024u,16,&report);check(report.entries==3&&records.size()==3&&motionDerivedPayload(records[0]),"derived payloads for current receipts");
    records[0].bytes.back()^=1;records[1].key=std::string(64,'0');clearBakedMotionCache();const auto status=stageMotionDerivedPayloads(session.document,records);check(status.find("entries_staged")->num==1&&status.find("entries_discarded")->num==2,"corrupt bytes and unknown keys are discarded");
    DocumentDerivedPayloads small;appendMotionDerivedPayloads(session.document,small,100,16,&report);check(small.empty()&&report.omittedEntries>=1,"entries above the remaining capacity are omitted");
    for(const auto& p:{path,jsonPath,bare}){std::error_code ec;fs::remove_all(p.parent_path(),ec);}
}

// 044B-048B shape through the normal authoring path (fixture writer only).
AgentSession legacyKiosk(size_t rebakes,V* rejection){AgentSession session;setMotionBakeInlineFixture(true);try{commit(session,kKiosk);commit(session,kKioskMotions);rebakeKiosk(session,rebakes,rejection);}catch(...){setMotionBakeInlineFixture(false);throw;}setMotionBakeInlineFixture(false);return session;}

void testLegacyLoad(){
    auto session=legacyKiosk(2,nullptr);check(bakeHasInlineFrames(bakeOf(session.document,"Door swing"))&&payloadRefs(session.document)>0,"legacy fixture writes inline frames into authoring and journal");
    const auto path=scratch("legacy.ark");V save=V::makeObj();save.set("path",V::makeStr(utf8(path)));accepted(session.call("doc_save",save));const auto bytes=documentToJson(session.document);
    AgentSession reopened;V load=V::makeObj();load.set("path",V::makeStr(utf8(path)));accepted(reopened.call("doc_load",load));
    check(documentToJson(reopened.document)==bytes&&(int)json(bytes).find("version")->num==47,"opening a legacy document rewrites neither authoring nor journal and keeps format 47");
    BakedMotionFramesSource source;const auto legacy=frames(reopened.document,"Drawer cycle",&source);check(source.source=="inline"&&source.verified&&legacy.frameCount>0,"legacy inline frames stay readable");
    const auto refs=payloadRefs(reopened.document),nodes=reopened.document.iterationHistory.find("nodes")->arr.size();
    commit(reopened,R"({"commands":[{"op":"motion.bake","name":"Drawer cycle","fps":30}]})");const auto& head=reopened.document.iterationHistory.find("nodes")->arr.back();
    check(bakeIsReceipt(bakeOf(reopened.document,"Drawer cycle"))&&bakeHasInlineFrames(bakeOf(reopened.document,"Door swing")),"the next bake stores a receipt; untouched motions stay inline");
    check(payloadRefs(reopened.document)==refs&&reopened.document.iterationHistory.find("nodes")->arr.size()==nodes+1&&head.find("delta")->dump().find("\"value\":{\"$ark_history_blob\"")==std::string::npos,"the conversion node adds no frame payload to the journal");
    std::error_code ec;fs::remove_all(path.parent_path(),ec);
}

void testMigration(){
    V rejection;auto session=legacyKiosk(64,&rejection);
    const auto before=history(session);const double nodes=before.find("node_count")->num;const double documentBefore=before.find("document_bytes")->num,journalBefore=before.find("storage_bytes")->num;
    check(rejection.t==V::Obj&&rejection.find("error_code")->str=="HISTORY_CAPACITY_EXCEEDED"&&rejection.find("derived_bakes")&&rejection.find("derived_bakes")->find("inline_bake_payload_bytes")->num>0,"a full legacy journal rejects the next edit and names the reclaimable derived bytes: "+rejection.dump().substr(0,400));
    check(before.find("inline_bake_payloads")->find("references")->num>0&&before.find("inline_bake_payloads")->find("hint"),"history_info reports inline payloads and the migration hint");
    const auto refsBefore=before.find("inline_bake_payloads")->find("references")->num,payloadBytes=before.find("inline_bake_payloads")->find("bytes")->num;
    const auto path=scratch("full kiosk.ark");V save=V::makeObj();save.set("path",V::makeStr(utf8(path)));accepted(session.call("doc_save",save));const auto fileBefore=fs::file_size(path);
    const auto legacyFrames=frames(session.document,"Screen tilt");const auto legacyJson=documentToJson(session.document);
    // Refusals: no confirmation, wrong revision, no saved document; nothing changes and no backup appears.
    auto args=[&](bool confirm,u64 revision,const std::string& file){V a=V::makeObj();a.set("path",V::makeStr(file));a.set("expected_revision",V::makeNum((double)revision));if(confirm)a.set("confirm",V::makeBool(true));a.set("actor",json(R"({"kind":"human","id":"derived-bakes-test"})"));return a;};
    failed(session,"history_migrate_derived",args(false,session.revision,utf8(path)),"HISTORY_MIGRATION_CONFIRM_REQUIRED","migration without confirm:true");
    {V a=args(true,session.revision,utf8(path));a.set("confirm",V::makeBool(false));failed(session,"history_migrate_derived",a,"HISTORY_MIGRATION_CONFIRM_REQUIRED","migration with confirm:false");}
    failed(session,"history_migrate_derived",args(true,session.revision+3,utf8(path)),"REVISION_CONFLICT","stale expected_revision");
    failed(session,"history_migrate_derived",args(true,session.revision,utf8(path.parent_path()/"never saved.ark")),"HISTORY_MIGRATION_PATH_REQUIRED","no saved document");
    const auto backup=path.parent_path()/"full kiosk.pre-049A.ark";check(!fs::exists(backup),"refusals write no backup");
    // An existing backup name is never replaced: the next free suffix is used.
    {std::ofstream(backup,std::ios::binary)<<"keep me";}
    const auto revision=session.revision;const auto t0=std::chrono::steady_clock::now();const auto result=accepted(session.call("history_migrate_derived",args(true,session.revision,utf8(path))));const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
    const auto backup2=path.parent_path()/"full kiosk.pre-049A-2.ark";{std::ifstream kept(backup,std::ios::binary);std::string text((std::istreambuf_iterator<char>(kept)),{});check(text=="keep me","an existing backup file is never overwritten");}
    check(fs::exists(backup2)&&result.find("backup")->find("path")->str==utf8(backup2)&&result.find("backup")->find("verified")->b,"the backup is written beside the document under the next free name");
    {Document original;std::string error;check(loadDocument(utf8(backup2).c_str(),original,&error),error);check(documentToJson(original)==legacyJson&&payloadRefs(original)==(size_t)refsBefore,"the backup holds the complete original document and journal");}
    check(session.revision==revision+1&&result.find("revision")->num==(double)session.revision&&result.find("migrated")->b&&!result.find("saved")->b,"migration advances one revision and leaves saving explicit");
    const double documentAfter=result.find("document_bytes_after")->num,journalAfter=result.find("journal_bytes_after")->num;
    check(result.find("document_bytes_before")->num==documentBefore&&documentAfter==(double)documentToJson(session.document).size()&&documentAfter<documentBefore/4,"document bytes shrink by more than 4x");
    check(journalAfter==(double)session.document.iterationHistory.dump().size()&&journalAfter<journalBefore/4,"journal bytes shrink by more than 4x");
    const auto after=history(session);check(after.find("schema")->num==2&&after.find("node_count")->num==nodes&&after.find("inline_bake_payloads")->find("references")->num==0&&documentToJson(session.document).find("frames_f32le_base64")==std::string::npos,"schema 2 journal keeps every node and no payload");
    const auto& record=after.find("migrations")->arr.at(0);check(record.find("kind")->str=="derived_bakes"&&record.find("actor")->find("id")->str=="derived-bakes-test"&&record.find("bake_payload_references")->num==refsBefore&&record.find("journal_bytes_before")->num==journalBefore&&record.find("journal_bytes_after")->num==journalAfter&&record.find("document_bytes_after")->num==documentAfter&&record.find("backup")->find("path")->str==utf8(backup2),"migration record states actor, counts, bytes and backup");
    // Node metadata survives; ids are a new chain.
    const auto& oldNodes=before.find("nodes")->arr;const auto& newNodes=after.find("nodes")->arr;for(size_t i=0;i<std::min(oldNodes.size(),newNodes.size());++i)check(oldNodes[i].find("summary")->str==newNodes[i].find("summary")->str&&oldNodes[i].find("utc")->str==newNodes[i].find("utc")->str&&oldNodes[i].find("actor")->dump()==newNodes[i].find("actor")->dump()&&oldNodes[i].find("tool")->str==newNodes[i].find("tool")->str,"node metadata kept");
    check(oldNodes.front().find("id")->str!=newNodes.front().find("id")->str&&oldNodes.back().find("id")->str==newNodes.back().find("id")->str,"ids are recomputed: states with frames get new ids, the frame-free first iteration keeps its id");
    BakedMotionFramesSource source;check(samePoses(frames(session.document,"Screen tilt",&source),legacyFrames)&&source.source=="memory","migrated motions play the same frames from the cache");
    // The previously rejected edit now fits; replay, restore and undo work on the rewritten journal.
    commit(session,bakeAll(120));const auto oldest=after.find("nodes")->arr.back().find("id")->str;
    V restore=V::makeObj();restore.set("id",V::makeStr(oldest));restore.set("expected_revision",V::makeNum((double)session.revision));accepted(session.call("iteration_restore",restore));check(findMotion(session.document,"Door swing")==nullptr,"restore to the first iteration (fixture without motions) works");
    accepted(session.call("doc_undo",V::makeObj()));check(bakeIsReceipt(bakeOf(session.document,"Door swing")),"undo returns to the receipt state");
    failed(session,"history_migrate_derived",args(true,session.revision,utf8(path)),"HISTORY_MIGRATION_NOT_NEEDED","second migration");
    accepted(session.call("doc_save",save));const auto fileAfter=fs::file_size(path);Document reopened;std::string error;check(loadDocument(utf8(path).c_str(),reopened,&error),error);
    check(reopened.iterationHistory.find("schema")->num==2&&documentToJson(reopened)==documentToJson(session.document),"the migrated document saves and reopens");
    // An incremental save keeps SQLite free pages (045A limit); a save to a new file is a full staged write.
    const auto copy=path.parent_path()/"migrated copy.ark";save.set("path",V::makeStr(utf8(copy)));accepted(session.call("doc_save",save));const auto fileCopy=fs::file_size(copy);check(fileCopy<fileBefore/2,"a fresh save of the migrated document is small");
    std::cout<<"measurement: legacy kiosk ("<<session.document.assembly.size()<<" occurrences, 3 motions) nodes="<<nodes<<" document_bytes="<<documentBefore<<" journal_bytes="<<journalBefore<<" inline_payload_refs="<<refsBefore<<" inline_payload_bytes="<<payloadBytes<<" file_bytes="<<fileBefore
             <<" | migrated document_bytes="<<documentAfter<<" journal_bytes="<<journalAfter<<" blobs "<<result.find("blobs_before")->num<<"->"<<result.find("blobs_after")->num<<" file_bytes_after_incremental_save="<<fileAfter<<" file_bytes_save_as_new="<<fileCopy<<" migration_ms="<<ms<<"\n";
    std::error_code ec;fs::remove_all(path.parent_path(),ec);
}

void testReceiptsFromStartMeasurement(){
    // Same sequence written by 049A: the journal never grows with frames.
    V rejection;auto legacy=legacyKiosk(64,&rejection);const auto legacyInfo=history(legacy);const size_t commits=(size_t)legacyInfo.find("node_count")->num;
    AgentSession session;commit(session,kKiosk);commit(session,kKioskMotions);V none;const size_t done=rebakeKiosk(session,64,&none);const auto info=history(session);
    check(none.t!=V::Obj&&info.find("node_count")->num>=commits,"receipts accept every re-bake the legacy journal rejected");
    check(info.find("document_bytes")->num<legacyInfo.find("document_bytes")->num/8,"receipt documents stay small");
    std::cout<<"measurement: 049A receipts same sequence nodes="<<info.find("node_count")->num<<" re-bake batches="<<done<<" document_bytes="<<info.find("document_bytes")->num<<" journal_bytes="<<info.find("storage_bytes")->num<<" (legacy stopped at nodes="<<commits<<" document_bytes="<<legacyInfo.find("document_bytes")->num<<")\n";
}

void testMotionResultsUnchanged(){
    // The same fixtures through the legacy inline path and the receipt path give bit-identical frames and inspections.
    auto legacy=legacyKiosk(1,nullptr);AgentSession session;commit(session,kKiosk);commit(session,kKioskMotions);rebakeKiosk(session,1,nullptr);
    for(const char* name:{"Door swing","Screen tilt","Drawer cycle"}){check(samePoses(frames(legacy.document,name),frames(session.document,name)),std::string("frames match the legacy bake for ")+name);
        V args=V::makeObj();args.set("name",V::makeStr(name));auto a=accepted(legacy.call("motion_info",args)),b=accepted(session.call("motion_info",args));check(a.find("diagnostics")->dump()==b.find("diagnostics")->dump()&&a.find("status")->dump()==b.find("status")->dump(),"motion_info diagnostics and status unchanged");
        args.set("frame",V::makeNum(17));a=accepted(legacy.call("motion_info",args));b=accepted(session.call("motion_info",args));check(a.find("frame")->dump()==b.find("frame")->dump()&&a.find("frames")->find("source")->str=="inline","decoded frame unchanged");
        auto ba=*a.find("bake"),bb=*b.find("bake");for(auto* v:{&ba,&bb})v->obj.erase(std::remove_if(v->obj.begin(),v->obj.end(),[](const auto& p){return p.first.rfind("frames_",0)==0||p.first=="coordinates_sha256";}),v->obj.end());check(ba.dump()==bb.dump(),"bake summary fields unchanged apart from frame storage");}
}

#ifdef ARK_DERIVED_BAKES_NATIVE
const char* kArm=R"J({"commands":[
  {"op":"feature.add","type":"box","name":"Pin","p":{"dx":4,"dy":4,"dz":4}},{"op":"feature.add","type":"box","name":"Arm link","p":{"dx":100,"dy":2,"dz":10}},{"op":"feature.add","type":"box","name":"Anchor body","p":{"dx":10,"dy":10,"dz":10}},
  {"op":"feature.add","type":"box","name":"Bridge body","p":{"dx":6,"dy":6,"dz":8}},{"op":"feature.add","type":"box","name":"Post body","p":{"dx":10,"dy":20,"dz":30}},
  {"op":"assembly.add","name":"Base carrier","feature":0},{"op":"assembly.add","name":"Arm carrier","feature":0},
  {"op":"assembly.add","name":"Anchor","parent":"Base carrier","feature":"Anchor body","fixed":true,"p":{"tx":-60,"ty":-60}},{"op":"assembly.add","name":"Pivot A","parent":"Base carrier","feature":"Pin"},
  {"op":"assembly.add","name":"Arm","parent":"Arm carrier","feature":"Arm link","p":{"tx":20}},{"op":"assembly.add","name":"Pivot B","parent":"Arm carrier","feature":"Pin"},
  {"op":"assembly.add","name":"Bridge","feature":"Bridge body","fixed":true,"p":{"tx":"63*cos(pi/4)-3*cos(pi/4)+3*sin(pi/4)","ty":"63*sin(pi/4)-3*sin(pi/4)-3*cos(pi/4)","tz":12,"rz":45}},
  {"op":"assembly.add","name":"Post","feature":"Post body","fixed":true,"p":{"tx":"125*cos(80*pi/180)+10*sin(80*pi/180)","ty":"125*sin(80*pi/180)-10*cos(80*pi/180)","tz":-10,"rz":80}},
  {"op":"mate.add","type":"revolute","a":"Pivot A","b":"Pivot B","reference_a":"origin","reference_b":"origin","limits":{"min":-10,"max":120}}]})J";
const char* kSweep=R"J({"commands":[{"op":"motion.graph.set","name":"Sweep","graph":{"channels":[{"name":"arm","mate":1,"from":0,"curve":{"type":"linear","duration":1,"to":90}}]}},{"op":"motion.bake","name":"Sweep","fps":10}]})J";
void testClearanceUnchanged(){
    // 046B fixture: the inline path compares stored float32 frames, the receipt path reproduces frames_sha256.
    // Pair orders differ so the process clearance cache cannot answer the second query.
    AgentSession session;commit(session,kArm);commit(session,kSweep);AgentSession legacy;commit(legacy,kArm);setMotionBakeInlineFixture(true);try{commit(legacy,kSweep);}catch(...){setMotionBakeInlineFixture(false);throw;}setMotionBakeInlineFixture(false);
    check(bakeIsReceipt(bakeOf(session.document,"Sweep"))&&bakeHasInlineFrames(bakeOf(legacy.document,"Sweep")),"clearance fixtures in both forms");
    auto query=[](AgentSession& s,const char* request){V args=V::makeObj();args.set("name",V::makeStr("Sweep"));args.set("clearance",json(request));return *accepted(s.call("motion_info",args)).find("clearance");};
    clearBakedMotionCache();
    for(const auto& pair:{std::pair{R"({"pairs":[{"a":"Arm","b":"Bridge"}],"max_subdivisions":6})",R"({"pairs":[{"a":"Bridge","b":"Arm"}],"max_subdivisions":6})"},std::pair{R"({"pairs":[{"a":"Arm","b":"Post"}],"max_subdivisions":6})",R"({"pairs":[{"a":"Post","b":"Arm"}],"max_subdivisions":6})"}}){
        const auto a=query(session,pair.first),b=query(legacy,pair.second);check(!a.find("cached")->b&&!b.find("cached")->b,"both clearance queries were computed");
        for(const char* key:{"min_clearance_mm","continuous_certified","continuous_lower_bound_mm","worst","markers","contact_frame_count","between_frame_contact_count","uncertified_interval_count","pairs"})check(a.find(key)&&b.find(key)&&a.find(key)->dump()==b.find(key)->dump(),std::string("clearance ")+key+" unchanged: "+(a.find(key)?a.find(key)->dump().substr(0,200):std::string("missing")));
        check(a.find("method")->find("bake_pose_max_deviation_mm")->num==b.find("method")->find("bake_pose_max_deviation_mm")->num&&a.find("method")->find("bake_frames")->str.find("frames_sha256")!=std::string::npos&&b.find("method")->find("bake_frames")->str.find("inline")!=std::string::npos,"same float32 deviation; the receipt path verifies frames_sha256");}
    check(bakedMotionFramesCached(bakeOf(session.document,"Sweep")),"a clearance re-solve caches the verified frames");
}
#endif

void testFrameStepFix(){
    AgentSession session;commit(session,R"({"commands":[{"op":"feature.add","type":"box","name":"Link body","p":{"dx":20,"dy":10,"dz":10}},{"op":"feature.add","type":"box","name":"Pin","p":{"dx":4,"dy":4,"dz":4}},
      {"op":"assembly.add","name":"Base carrier","feature":0},{"op":"assembly.add","name":"Wheel carrier","feature":0},{"op":"assembly.add","name":"Base","parent":"Base carrier","feature":"Link body","fixed":true},{"op":"assembly.add","name":"Axle A","parent":"Base carrier","feature":"Pin"},
      {"op":"assembly.add","name":"Wheel","parent":"Wheel carrier","feature":"Link body"},{"op":"assembly.add","name":"Axle B","parent":"Wheel carrier","feature":"Pin"},
      {"op":"mate.add","type":"revolute","a":"Axle A","b":"Axle B","reference_a":"origin","reference_b":"origin"}]})");
    auto stepError=[&](const char* graph,int fps){const auto args=withRevision(session,std::string(R"({"commands":[{"op":"motion.graph.set","name":"Spin","graph":)")+graph+R"(},{"op":"motion.bake","name":"Spin","fps":)"+std::to_string(fps)+"}]}");
        const auto out=failed(session,"doc_apply",args,"MOTION_FRAME_STEP_TOO_LARGE","fast unlimited revolute");check(out.find("error_code")->str=="MOTION_FRAME_STEP_TOO_LARGE"&&out.find("frame_step"),"structured frame_step: "+out.dump().substr(0,500));return *out.find("frame_step");};
    // 720 degrees in 1 s at 2 fps: 360 deg per frame from frame 1; 720/f <= 170 needs 5 fps.
    auto step=stepError(R"({"channels":[{"name":"wheel","mate":1,"from":0,"curve":{"type":"linear","duration":1,"to":720}}]})",2);
    check(step.find("channel")->str=="wheel"&&step.find("frame")->num==1&&step.find("time_s")->num==.5&&step.find("max_step_deg")->num==360&&step.find("fps")->num==2&&step.find("required_fps")->num==5&&step.find("required_fps_within_limit")->b,"first offending frame, max step and required fps: "+step.dump());
    commit(session,R"({"commands":[{"op":"motion.graph.set","name":"Spin","graph":{"channels":[{"name":"wheel","mate":1,"from":0,"curve":{"type":"linear","duration":1,"to":720}}]}},{"op":"motion.bake","name":"Spin","fps":5}]})");check(bakeOf(session.document,"Spin").find("frame_count")->num==6,"the reported fps bakes");
    // A hold then a spin: the first offending frame is after the hold; the largest step is reported over all remaining frames.
    step=stepError(R"({"channels":[{"name":"wheel","mate":1,"from":0,"curve":{"type":"sequence","items":[{"type":"hold","duration":1},{"type":"linear","duration":1,"to":720},{"type":"linear","duration":1,"to":1440}]}}]})",2);
    check(step.find("frame")->num==3&&step.find("time_s")->num==1.5&&step.find("max_step_deg")->num==360&&step.find("required_fps")->num==5,"offending frame after a hold: "+step.dump());
    // Too fast for 240 fps: no fps within the limit, stated explicitly.
    step=stepError(R"({"channels":[{"name":"wheel","mate":1,"from":0,"curve":{"type":"linear","duration":1,"to":100000}}]})",30);
    check(step.find("required_fps")->num==0&&!step.find("required_fps_within_limit")->b&&step.find("fps_limit")->num==240,"beyond the fps limit: "+step.dump());
    const auto reply=session.call("doc_apply",withRevision(session,R"({"commands":[{"op":"motion.graph.set","name":"Fast","graph":{"channels":[{"name":"wheel","mate":1,"from":0,"curve":{"type":"linear","duration":1,"to":100000}}]}},{"op":"motion.bake","name":"Fast","fps":30}]})"));
    check(reply.find("structuredContent")->find("error")->str.find("No fps up to the 240 fps limit")!=std::string::npos,"message says no fps within the limit");
}
}
int main(){
    try{
        const std::pair<const char*,void(*)()> stages[]={{"receipts",testReceiptsOnly},{"rebake",testRebakeVerification},{"derived",testDerivedSection},{"legacy",testLegacyLoad},{"results",testMotionResultsUnchanged},{"migration",testMigration},{"measurement",testReceiptsFromStartMeasurement},{"frame-step",testFrameStepFix}
#ifdef ARK_DERIVED_BAKES_NATIVE
            ,{"clearance",testClearanceUnchanged}
#endif
        };
        for(const auto& stage:stages){std::cerr<<"stage "<<stage.first<<'\n';stage.second();}
        std::cout<<"Derived bakes 049A tests passed: "<<checks<<" checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAILED after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
