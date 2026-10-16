#include "ark/document_container.hpp"
#include "ark/iteration_history.hpp"
#include "sqlite3.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs=std::filesystem;
using V=arkjson::Value;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string pathText(const fs::path& path){auto text=path.u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
struct Workspace {
    fs::path directory;
    Workspace(){for(int i=0;i<100;++i){auto candidate=fs::temp_directory_path()/("ark-snapshot-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(i));if(fs::create_directory(candidate)){directory=candidate;return;}}throw std::runtime_error("Cannot create synthetic workspace");}
    ~Workspace(){std::error_code error;for(fs::directory_iterator it(directory,error),end;!error&&it!=end;it.increment(error))if(it->is_regular_file(error))fs::remove(it->path(),error);fs::remove(directory,error);}
};
struct Database {
    sqlite3* db=nullptr;
    explicit Database(const fs::path& path){check(sqlite3_open(pathText(path).c_str(),&db)==SQLITE_OK,"Synthetic database opens");}
    ~Database(){sqlite3_close(db);}
    void exec(const char* sql){char* error=nullptr;const int status=sqlite3_exec(db,sql,nullptr,nullptr,&error);const std::string message=error?error:"Synthetic SQL failed";sqlite3_free(error);check(status==SQLITE_OK,message);}
    int version(){sqlite3_stmt* stmt=nullptr;check(sqlite3_prepare_v2(db,"PRAGMA user_version",-1,&stmt,nullptr)==SQLITE_OK,"Version query prepares");check(sqlite3_step(stmt)==SQLITE_ROW,"Version row exists");const int value=sqlite3_column_int(stmt,0);sqlite3_finalize(stmt);return value;}
};
std::string read(const fs::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
std::string canonical(const V& value){std::string result,error;check(ark::encodeDocumentValue(value,result,&error),error);return result;}
}
int main(){try{
    Workspace workspace;const auto path=workspace.directory/"synthetic.ark";const auto filename=pathText(path);std::string error,diagnostic;
    V root=V::makeObj();root.set("format",V::makeStr("ark"));root.set("version",V::makeNum(1));root.set("authoring",V::makeStr("unchanged authoritative history"));
    check(ark::saveDocumentContainer(root,filename.c_str(),&error),error);
    {Database db(path);check(db.version()==1,"Authoring-only new saves retain container v1");}
    V loaded;ark::DocumentDerivedPayloads restored{{"old","stale"}};
    check(ark::loadDocumentContainer(filename.c_str(),loaded,&error,&restored,&diagnostic),error);check(restored.empty()&&diagnostic.empty()&&canonical(loaded)==canonical(root),"Legacy v1 loads without derived results");
    ark::DocumentDerivedPayloads small{{ark::iterationSha256("small request"),"small result"}};V blockedRoot=root;blockedRoot.set("authoring",V::makeStr("uncommitted replacement"));
    {Database reader(path);reader.exec("BEGIN; SELECT * FROM ark_meta");check(!ark::saveDocumentContainer(blockedRoot,filename.c_str(),&error,nullptr,&small),"Concurrent reader blocks migration commit");reader.exec("ROLLBACK");}
    {Database db(path);check(db.version()==1,"Failed migration rolls back schema version and derived table");}
    check(ark::loadDocumentContainer(filename.c_str(),loaded,&error,&restored,&diagnostic)&&canonical(loaded)==canonical(root)&&restored.empty(),"Failed migration preserves old authoring and snapshot state");
    std::string binary(17*1024*1024,'x');binary[123]='\0';binary.back()='z';
    ark::DocumentDerivedPayloads snapshots{{ark::iterationSha256("request+producer"),binary},{ark::iterationSha256("other request"),"opaque result"}};
    ark::DocumentContainerWriteStats stats;
    check(ark::saveDocumentContainer(root,filename.c_str(),&error,&stats,&snapshots),error);
    {Database db(path);check(db.version()==2,"Adding derived results migrates to container v2");}
    check(ark::loadDocumentContainer(filename.c_str(),loaded,&error,&restored,&diagnostic),error);
    check(diagnostic.empty()&&canonical(loaded)==canonical(root)&&restored.size()==2,"Snapshot does not alter authoring");
    bool found=false;for(const auto& item:restored)if(item.key==snapshots[0].key){found=true;check(item.bytes==binary,"Binary derived payload retains NUL bytes and exceeds old typed-value limit");}check(found,"Exact request identity survives migration");
    const auto prior=read(path);auto invalid=snapshots;invalid.push_back(snapshots[0]);V changed=root;changed.set("authoring",V::makeStr("changed"));
    check(!ark::saveDocumentContainer(changed,filename.c_str(),&error,nullptr,&invalid),"Duplicate derived identity rejects save");check(read(path)==prior,"Rejected save preserves complete existing file");
    {Database db(path);db.exec("UPDATE ark_derived SET bytes=x'626164' WHERE key=(SELECT key FROM ark_derived ORDER BY key LIMIT 1)");}
    check(ark::loadDocumentContainer(filename.c_str(),loaded,&error,&restored,&diagnostic),error);check(restored.empty()&&!diagnostic.empty()&&canonical(loaded)==canonical(root),"Damaged optional snapshot is discarded with explicit diagnostic and authoring preserved");
    check(ark::saveDocumentContainer(changed,filename.c_str(),&error),error);diagnostic="old";
    check(ark::loadDocumentContainer(filename.c_str(),loaded,&error,&restored,&diagnostic),error);check(restored.empty()&&diagnostic.empty()&&canonical(loaded)==canonical(changed),"Authoring-only v2 save removes stale snapshot");
    {Database db(path);check(db.version()==2,"Existing v2 remains v2 without derived results");db.exec("UPDATE ark_meta SET root_hash='0000000000000000000000000000000000000000000000000000000000000000'");}
    const auto unchanged=loaded.dump();check(!ark::loadDocumentContainer(filename.c_str(),loaded,&error,&restored,&diagnostic),"Authoring corruption remains a hard rejection");check(loaded.dump()==unchanged,"Failed load does not publish partial authoring");
    std::cout<<"031B container snapshots: v1 migration, binary custody, rejection atomicity, optional corruption and strict authoring passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
