#include "ark/derived_bakes.hpp"
#include "ark/iteration_history.hpp"
#include "ark/motion_graph.hpp"
#include "ark/serialize.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace ark {
namespace {
using V=arkjson::Value;namespace fs=std::filesystem;
void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
std::string utc(){auto now=std::time(nullptr);std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm,&now);
#else
    gmtime_r(&now,&tm);
#endif
    char buf[32];std::strftime(buf,sizeof buf,"%Y-%m-%dT%H:%M:%SZ",&tm);return buf;
}
std::string utf8(const fs::path& path){auto s=path.u8string();return std::string(s.begin(),s.end());}
V* member(V& v,const char* key){for(auto& item:v.obj)if(item.first==key)return &item.second;return nullptr;}
size_t inlineBakes(const Document& d){size_t n=0;if(d.motions.t==V::Arr)for(const auto& row:d.motions.arr)if(const auto* bake=row.find("bake");bake&&bakeHasInlineFrames(*bake))++n;return n;}
// Publishes a staged file under a name that must not exist yet; never replaces.
bool publishNew(const fs::path& from,const fs::path& to){
#ifdef _WIN32
    return MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
#else
    if(::link(from.c_str(),to.c_str())!=0)return false;std::error_code ec;fs::remove(from,ec);return true;
#endif
}
}
Document migrateDerivedBakes(const Document& source,const V& actor,const std::string& motivation,const V& backup,DerivedBakeMigration& r){
    r={};need(source.iterationHistory.t==V::Obj,"HISTORY_MIGRATION_NO_JOURNAL: this document has no iteration journal; run motion.bake to replace an inline bake with a receipt");
    const V& h=source.iterationHistory;iterationInlineBakePayloads(h,r.payloadReferences,r.payloadBytes);const size_t current=inlineBakes(source);
    need(r.payloadReferences||current,"HISTORY_MIGRATION_NOT_NEEDED: the authoring and the journal hold no inline baked-frame payloads; nothing was changed");
    r.nodes=h.find("nodes")->arr.size();r.states=r.nodes+1;r.blobsBefore=h.find("blobs")->obj.size();r.journalBytesBefore=h.dump().size();r.documentBytesBefore=documentToJson(source).size();r.headBefore=h.find("head_id")->str;r.baselineBefore=h.find("baseline_id")->str;
    // Each state keeps every authored field; only inline bakes become receipts, and receipts select format 49.
    auto transform=[&](V& state){V* motions=member(state,"motions");if(!motions||motions->t!=V::Arr)return;bool any=false;
        for(auto& row:motions->arr)if(V* bake=member(row,"bake"))if(motionReceiptFromInline(*bake,false))any=true;
        if(any)state.set("version",V::makeNum(std::max(49.,state.find("version")?state.find("version")->num:49.)));};
    std::map<std::string,std::string> ids;V history=iterationRewrite(h,transform,ids);
    Document out=source;if(out.motions.t==V::Arr)for(auto& row:out.motions.arr)if(V* bake=member(row,"bake"))if(motionReceiptFromInline(*bake,true))++r.framesCached;
    r.headAfter=history.find("head_id")->str;r.baselineAfter=history.find("baseline_id")->str;r.blobsAfter=history.find("blobs")->obj.size();
    V record=V::makeObj();record.set("kind",V::makeStr("derived_bakes"));record.set("format",V::makeStr("ark.history_migration.v1"));record.set("tool",V::makeStr("history_migrate_derived"));record.set("actor",actor);record.set("provenance",V::makeStr("caller_declared"));record.set("motivation",V::makeStr(motivation));record.set("utc",V::makeStr(utc()));
    record.set("schema_before",*h.find("schema"));record.set("previous_head_id",V::makeStr(r.headBefore));record.set("previous_baseline_id",V::makeStr(r.baselineBefore));record.set("head_id",V::makeStr(r.headAfter));record.set("baseline_id",V::makeStr(r.baselineAfter));
    record.set("nodes",V::makeNum((double)r.nodes));record.set("states_rewritten",V::makeNum((double)r.states));record.set("bake_payload_references",V::makeNum((double)r.payloadReferences));record.set("bake_payload_bytes",V::makeNum((double)r.payloadBytes));
    record.set("blobs_before",V::makeNum((double)r.blobsBefore));record.set("blobs_after",V::makeNum((double)r.blobsAfter));record.set("journal_bytes_before",V::makeNum((double)r.journalBytesBefore));record.set("document_bytes_before",V::makeNum((double)r.documentBytesBefore));record.set("backup",backup);
    record.set("digests",V::makeStr("node ids, baseline and node SHA-256 values are recomputed over receipt-form authoring; actors, tools, summaries, motivations, times and ordinals are unchanged"));
    V list=h.find("migrations")?*h.find("migrations"):V::makeArr();list.arr.push_back(record);history.set("migrations",list);out.iterationHistory=std::move(history);
    // The record states its own after-sizes: iterate until the byte counts it contains are stable.
    for(int i=0;i<6;++i){const size_t journal=out.iterationHistory.dump().size(),document=documentToJson(out).size();V& last=member(out.iterationHistory,"migrations")->arr.back();
        if(last.find("journal_bytes_after")&&last.find("journal_bytes_after")->num==(double)journal&&last.find("document_bytes_after")->num==(double)document){r.journalBytesAfter=journal;r.documentBytesAfter=document;break;}
        last.set("journal_bytes_after",V::makeNum((double)journal));last.set("document_bytes_after",V::makeNum((double)document));}
    need(r.journalBytesAfter&&r.documentBytesAfter<=maxDocumentBytes,"HISTORY_MIGRATION_FAILED: the migrated document size did not settle within the 4 MiB capacity");
    std::string error;need(iterationValidate(out,out.iterationHistory,error),"HISTORY_MIGRATION_FAILED: the rewritten journal does not validate: "+error);
    Document reopened;need(documentFromJson(documentToJson(out),reopened,&error),"HISTORY_MIGRATION_FAILED: the migrated document does not reopen: "+error);
    return out;
}
V historyMigrateDerivedTool(Document& document,u64& revision,const V& args){
    need(args.t==V::Obj,"Arguments must be an object");for(const auto& field:args.obj)need(field.first=="path"||field.first=="expected_revision"||field.first=="confirm"||field.first=="motivation"||field.first=="actor","HISTORY_MIGRATION_INVALID: unknown field "+field.first);
    const auto* expected=args.find("expected_revision");need(expected&&expected->t==V::Num&&expected->num==(double)revision,"Revision conflict; the current revision is in this result. Read history_info and retry");
    const auto* confirm=args.find("confirm");need(confirm&&confirm->t==V::Bool&&confirm->b,"HISTORY_MIGRATION_CONFIRM_REQUIRED: rewriting the iteration journal needs confirm:true, given only on the user's explicit instruction; nothing was changed");
    const auto* pathArg=args.find("path");need(pathArg&&pathArg->t==V::Str&&!pathArg->str.empty(),"HISTORY_MIGRATION_PATH_REQUIRED: path names the saved .ark document; the original is backed up beside it before the journal is rewritten");
    const fs::path path=fs::absolute(fs::u8path(pathArg->str));auto extension=path.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return (char)std::tolower(c);});
    need(extension==".ark"&&fs::is_regular_file(path),"HISTORY_MIGRATION_PATH_REQUIRED: "+utf8(path)+" is not a saved .ark document; save the document first, then migrate it");
    const V actor=iterationActor(args);std::string motivation;if(const auto* m=args.find("motivation")){need(m->t==V::Str&&m->str.size()<=4096,"motivation must be text of at most 4096 bytes");motivation=m->str;}
    need(document.iterationHistory.t==V::Obj,"HISTORY_MIGRATION_NO_JOURNAL: this document has no iteration journal; run motion.bake to replace an inline bake with a receipt");
    size_t references=0,bytes=0;iterationInlineBakePayloads(document.iterationHistory,references,bytes);need(references||inlineBakes(document),"HISTORY_MIGRATION_NOT_NEEDED: the authoring and the journal hold no inline baked-frame payloads; nothing was changed");
    // Backup: the complete pre-migration document (authoring, journal, usage, activity) beside path, never replacing a file.
    fs::path backup;for(int n=1;n<=99&&backup.empty();++n){const fs::path candidate=path.parent_path()/fs::u8path(utf8(path.stem())+(n==1?".pre-049A":".pre-049A-"+std::to_string(n))+".ark");if(!fs::exists(candidate))backup=candidate;}
    need(!backup.empty(),"HISTORY_MIGRATION_BACKUP_EXISTS: "+utf8(path.stem())+".pre-049A.ark through -99 already exist beside the document; move one away. Nothing was changed");
    const fs::path staging=path.parent_path()/fs::u8path(utf8(path.stem())+".pre-049A.staging-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ark");
    DocumentSaveReport saved;if(!saveDocument(document,utf8(staging).c_str(),&saved)){std::error_code ec;fs::remove(staging,ec);throw std::runtime_error("HISTORY_MIGRATION_BACKUP_FAILED: "+saved.error+"; nothing was changed");}
    if(!publishNew(staging,backup)){std::error_code ec;fs::remove(staging,ec);throw std::runtime_error("HISTORY_MIGRATION_BACKUP_FAILED: could not publish "+utf8(backup)+" without replacing a file; nothing was changed");}
    Document check;std::string error;need(loadDocument(utf8(backup).c_str(),check,&error)&&documentToJson(check)==documentToJson(document),"HISTORY_MIGRATION_BACKUP_FAILED: the backup "+utf8(backup)+" did not reopen identically ("+error+"); it is kept and the document is unchanged");
    V backupInfo=V::makeObj();backupInfo.set("path",V::makeStr(utf8(backup)));backupInfo.set("bytes",V::makeNum((double)fs::file_size(backup)));backupInfo.set("verified",V::makeBool(true));
    DerivedBakeMigration report;Document next;
    try{next=migrateDerivedBakes(document,actor,motivation,backupInfo,report);}catch(const std::exception& e){throw std::runtime_error(std::string(e.what())+"; the backup "+utf8(backup)+" is kept and the document is unchanged");}
    document=std::move(next);++revision;
    V out=V::makeObj();out.set("migrated",V::makeBool(true));out.set("revision",V::makeNum((double)revision));out.set("backup",backupInfo);out.set("schema",V::makeNum(2));
    out.set("nodes",V::makeNum((double)report.nodes));out.set("states_rewritten",V::makeNum((double)report.states));out.set("bake_payload_references",V::makeNum((double)report.payloadReferences));out.set("bake_payload_bytes",V::makeNum((double)report.payloadBytes));
    out.set("blobs_before",V::makeNum((double)report.blobsBefore));out.set("blobs_after",V::makeNum((double)report.blobsAfter));out.set("journal_bytes_before",V::makeNum((double)report.journalBytesBefore));out.set("journal_bytes_after",V::makeNum((double)report.journalBytesAfter));
    out.set("document_bytes_before",V::makeNum((double)report.documentBytesBefore));out.set("document_bytes_after",V::makeNum((double)report.documentBytesAfter));out.set("max_document_bytes",V::makeNum((double)maxDocumentBytes));
    out.set("head_id_before",V::makeStr(report.headBefore));out.set("head_id",V::makeStr(report.headAfter));out.set("frames_cached",V::makeNum((double)report.framesCached));out.set("saved",V::makeBool(false));
    out.set("next_step",V::makeStr("doc_save{path} writes the migrated document; the cached frames go to its derived section. Node ids changed: re-read history_info before iteration_restore."));return out;
}
}
