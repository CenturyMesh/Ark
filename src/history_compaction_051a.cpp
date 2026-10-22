#include "ark/iteration_history.hpp"
#include "ark/serialize.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
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
void require(bool valid,const std::string& reason){if(!valid)throw std::runtime_error(reason);}
std::string utf8(const fs::path& path){const auto value=path.u8string();return {value.begin(),value.end()};}
std::string bytes(const fs::path& path){std::ifstream input(path,std::ios::binary);require(bool(input),"HISTORY_COMPACTION_ARCHIVE_FAILED: cannot read archive");return {std::istreambuf_iterator<char>(input),{}};}
bool publishNew(const fs::path& from,const fs::path& to){
#ifdef _WIN32
    return MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
#else
    if(::link(from.c_str(),to.c_str())!=0)return false;std::error_code ec;fs::remove(from,ec);return true;
#endif
}
}
V historyCompactTool(Document& document,u64& revision,const V& args){
    require(args.t==V::Obj,"HISTORY_COMPACTION_INVALID: arguments must be an object");
    for(const auto& field:args.obj)require(field.first=="archive_path"||field.first=="keep_recent"||field.first=="expected_revision"||field.first=="confirm"||field.first=="actor"||field.first=="motivation","HISTORY_COMPACTION_INVALID: unknown field "+field.first);
    const auto* expected=args.find("expected_revision");require(expected&&expected->t==V::Num&&expected->num==(double)revision,"Revision conflict; read history_info before compacting");
    const auto* confirm=args.find("confirm");require(confirm&&confirm->t==V::Bool&&confirm->b,"HISTORY_COMPACTION_CONFIRM_REQUIRED: archive and compact only on explicit instruction with confirm:true");
    const auto* count=args.find("keep_recent");require(count&&count->t==V::Num&&count->num>=0&&count->num<=256&&count->num==std::floor(count->num),"HISTORY_COMPACTION_INVALID: keep_recent must be an integer 0..256");
    const V actor=iterationActor(args);std::string motivation;if(const auto* m=args.find("motivation")){require(m->t==V::Str&&m->str.size()<=4096,"HISTORY_COMPACTION_INVALID: motivation must be text of at most 4096 bytes");motivation=m->str;}
    const auto* path=args.find("archive_path");require(path&&path->t==V::Str&&!path->str.empty()&&path->str.size()<=4096,"HISTORY_COMPACTION_INVALID: archive_path must name a new .ark file");
    const fs::path archive=fs::absolute(fs::u8path(path->str));auto extension=archive.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return (char)std::tolower(c);});
    require(extension==".ark"&&fs::is_directory(archive.parent_path())&&!fs::exists(archive),"HISTORY_COMPACTION_ARCHIVE_FAILED: archive must be a new .ark file in an existing directory; nothing was changed");
    require(document.iterationHistory.t==V::Obj,"HISTORY_COMPACTION_INVALID: no persistent history to compact");
    const std::string original=documentToJson(document);std::string error;require(iterationValidate(document,document.iterationHistory,error),"HISTORY_COMPACTION_INVALID: "+error);
    // Reserve a unique directory atomically, so concurrent calls cannot overwrite staging.
    fs::path stagingDirectory;for(int attempt=0;attempt<100&&stagingDirectory.empty();++attempt){auto candidate=archive.parent_path()/fs::u8path(".ark-compact-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(attempt));std::error_code ec;if(fs::create_directory(candidate,ec))stagingDirectory=candidate;}
    require(!stagingDirectory.empty(),"HISTORY_COMPACTION_ARCHIVE_FAILED: cannot reserve archive staging directory");
    struct Cleanup {fs::path directory;~Cleanup(){std::error_code ec;fs::remove(directory/"archive.ark",ec);fs::remove(directory,ec);}} cleanup{stagingDirectory};const auto staging=stagingDirectory/"archive.ark";
    DocumentSaveReport saved;require(saveDocument(document,utf8(staging).c_str(),&saved),"HISTORY_COMPACTION_ARCHIVE_FAILED: "+saved.error);
    Document reopened;require(loadDocument(utf8(staging).c_str(),reopened,&error)&&documentToJson(reopened)==original,"HISTORY_COMPACTION_ARCHIVE_FAILED: staged archive did not reopen identically: "+error);
    V archiveInfo=V::makeObj();archiveInfo.set("path",V::makeStr(utf8(archive)));archiveInfo.set("bytes",V::makeNum((double)fs::file_size(staging)));archiveInfo.set("verified",V::makeBool(true));archiveInfo.set("document_sha256",V::makeStr(iterationSha256(original)));archiveInfo.set("file_sha256",V::makeStr(iterationSha256(bytes(staging))));
    V record=V::makeObj();record.set("archive",archiveInfo);record.set("actor",actor);record.set("provenance",V::makeStr("caller_declared"));record.set("motivation",V::makeStr(motivation));record.set("revision_before",V::makeNum((double)revision));
    Document next=document;next.iterationHistory=iterationCompact(document,(size_t)count->num,record);const std::string persisted=documentToJson(next);
    require(persisted.size()<=maxDocumentBytes,"HISTORY_COMPACTION_CAPACITY: retained history still exceeds 4 MiB; request fewer retained nodes");
    require(iterationValidate(next,next.iterationHistory,error)&&documentFromJson(persisted,reopened,&error)&&documentToJson(reopened)==persisted,"HISTORY_COMPACTION_INVALID: compacted document did not reopen identically: "+error);
    require(iterationAuthoring(next).dump()==iterationAuthoring(document).dump(),"HISTORY_COMPACTION_INVALID: compaction changed authoring");
    // Prepare all receipts before publication. Only archive publication may fail after this.
    V out=V::makeObj();out.set("compacted",V::makeBool(true));out.set("revision",V::makeNum((double)(revision+1)));out.set("archive",archiveInfo);out.set("compaction",*next.iterationHistory.find("compaction"));out.set("capacity_before",iterationInfo(document,1));out.set("capacity_after",iterationInfo(next,1));out.set("document_bytes_before",V::makeNum(original.size()));out.set("document_bytes_after",V::makeNum(persisted.size()));out.set("next_step",V::makeStr("Save the compacted document with doc_save. Open archive.path with doc_load to recover complete previous branches and undo history; archive is never replaced. Re-read history_info: retained node ids changed."));
    require(publishNew(staging,archive),"HISTORY_COMPACTION_ARCHIVE_FAILED: archive publication refused (existing destination or I/O failure); document unchanged");
    document=std::move(next);++revision;return out;
}
}
