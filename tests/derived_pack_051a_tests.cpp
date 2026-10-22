#include "ark/derived_pack.hpp"
#include "ark/document_container.hpp"
#include "ark/iteration_history.hpp"
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include "sqlite3.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
namespace fs=std::filesystem;
using V=arkjson::Value;
using ark::DocumentDerivedPayloads;
size_t checks=0;
void check(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
std::string pathText(const fs::path& path){auto text=path.u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
struct Workspace {
    fs::path directory;
    Workspace(){for(int i=0;i<100;++i){auto candidate=fs::temp_directory_path()/("ark-pack-051a-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(i));if(fs::create_directory(candidate)){directory=candidate;return;}}throw std::runtime_error("Cannot create synthetic workspace");}
    ~Workspace(){std::error_code error;fs::remove_all(directory,error);}
    size_t staged()const{size_t n=0;for(const auto& entry:fs::directory_iterator(directory))if(entry.path().filename().string().find(".ark-stage-")!=std::string::npos)++n;return n;}
};
struct Database {
    sqlite3* db=nullptr;
    explicit Database(const fs::path& path){check(sqlite3_open(pathText(path).c_str(),&db)==SQLITE_OK,"Synthetic database opens");}
    ~Database(){sqlite3_close(db);}
    void exec(const std::string& sql){char* error=nullptr;const int status=sqlite3_exec(db,sql.c_str(),nullptr,nullptr,&error);const std::string message=error?error:"Synthetic SQL failed";sqlite3_free(error);check(status==SQLITE_OK,message);}
    std::vector<std::pair<std::string,std::string>> rows(){
        std::vector<std::pair<std::string,std::string>> out;sqlite3_stmt* q=nullptr;check(sqlite3_prepare_v2(db,"SELECT key,hash,bytes FROM ark_derived ORDER BY key",-1,&q,nullptr)==SQLITE_OK,"Derived rows prepare");
        while(sqlite3_step(q)==SQLITE_ROW){const std::string key(reinterpret_cast<const char*>(sqlite3_column_text(q,0))),hash(reinterpret_cast<const char*>(sqlite3_column_text(q,1)));const std::string bytes(reinterpret_cast<const char*>(sqlite3_column_blob(q,2)),(size_t)sqlite3_column_bytes(q,2));check(hash==ark::iterationSha256(bytes),"Pack chunk digest is portable SHA-256");out.emplace_back(key,bytes);}
        sqlite3_finalize(q);return out;
    }
    void put(const std::string& key,const std::string& bytes){
        sqlite3_stmt* q=nullptr;check(sqlite3_prepare_v2(db,"INSERT INTO ark_derived(key,hash,bytes) VALUES(?1,?2,?3)",-1,&q,nullptr)==SQLITE_OK,"Derived insert prepares");const auto hash=ark::iterationSha256(bytes);
        sqlite3_bind_text(q,1,key.data(),(int)key.size(),SQLITE_TRANSIENT);sqlite3_bind_text(q,2,hash.data(),(int)hash.size(),SQLITE_TRANSIENT);sqlite3_bind_blob(q,3,bytes.data(),(int)bytes.size(),SQLITE_TRANSIENT);
        check(sqlite3_step(q)==SQLITE_DONE,"Derived row inserted");sqlite3_finalize(q);
    }
};
std::string canonical(const V& value){std::string result,error;check(ark::encodeDocumentValue(value,result,&error),error);return result;}
std::string key(const std::string& seed){return ark::iterationSha256(seed);}
std::string random(size_t size,uint64_t seed){std::string out(size,'\0');uint64_t x=seed*0x9E3779B97F4A7C15ull+1;for(auto& c:out){x^=x<<13;x^=x>>7;x^=x<<17;c=(char)(x>>24);}return out;}
std::string b64(const std::string& raw){
    static const char* a="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string out;size_t i=0;
    for(;i+3<=raw.size();i+=3){const unsigned v=(unsigned char)raw[i]<<16|(unsigned char)raw[i+1]<<8|(unsigned char)raw[i+2];out+=a[v>>18];out+=a[(v>>12)&63];out+=a[(v>>6)&63];out+=a[v&63];}
    if(raw.size()-i==1){const unsigned v=(unsigned char)raw[i]<<16;out+=a[v>>18];out+=a[(v>>12)&63];out+="==";}
    else if(raw.size()-i==2){const unsigned v=(unsigned char)raw[i]<<16|(unsigned char)raw[i+1]<<8;out+=a[v>>18];out+=a[(v>>12)&63];out+=a[(v>>6)&63];out+='=';}
    return out;
}
std::string nativeLike(const std::string& id,const std::string& brep,const std::string& extra={}){
    return "ARK-NATIVE-CACHE1\n"+id+"\n"+key("digest "+id)+"\n{\"brep_base64\":\""+b64(brep)+"\""+extra+",\"topology\":{\"brep_sha256\":\""+key(brep)+"\",\"references\":[{\"name\":\"@ark.face."+key(brep)+".0\",\"status\":\"obsolete\"}]}}";
}
bool same(const DocumentDerivedPayloads& a,const DocumentDerivedPayloads& b){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(a[i].key!=b[i].key||a[i].bytes!=b[i].bytes)return false;return true;}
bool rejects(const std::function<void()>& action){try{action();}catch(const std::runtime_error&){return true;}return false;}
void varint(std::string& out,uint64_t n){do{unsigned char c=(unsigned char)(n&127);n>>=7;out+=(char)(c|(n?128:0));}while(n);}
std::string frame(const std::string& body,bool checksum=true,bool contentSize=true){
    ZSTD_CCtx* c=ZSTD_createCCtx();ZSTD_CCtx_setParameter(c,ZSTD_c_checksumFlag,checksum?1:0);ZSTD_CCtx_setParameter(c,ZSTD_c_contentSizeFlag,contentSize?1:0);
    std::string out(ZSTD_compressBound(body.size()),'\0');const size_t n=ZSTD_compress2(c,out.data(),out.size(),body.data(),body.size());ZSTD_freeCCtx(c);check(!ZSTD_isError(n),"Fixture frame compresses");out.resize(n);return out;
}
std::string body(const std::vector<std::pair<std::string,std::string>>& entries){std::string out="ARK-DERIVED-PACK1\n";varint(out,entries.size());for(const auto& [k,bytes]:entries){out+=k;varint(out,bytes.size());varint(out,0);out+=bytes;}return out;}
V root(const std::string& text){V r=V::makeObj();r.set("format",V::makeStr("ark"));r.set("version",V::makeNum(1));r.set("authoring",V::makeStr(text));return r;}

void codec(){
    // 600000 bytes is a multiple of 3: entry one's base64 aligns with the shared bytes,
    // entry two prepends one byte so its base64 text shares nothing character-wise.
    const auto shared=random(600000,1);
    DocumentDerivedPayloads records{
        {key("a"),nativeLike(key("a"),shared)},
        {key("b"),nativeLike(key("b"),std::string(1,'\x7f')+shared)},
        {key("c"),"{\"noncanonical\":\""+std::string(2044,'A')+"QR==\"}"},
        {key("d"),std::string("ARK-MOTION-FRAMES1\n\0:\"",22)+random(5000,4)},
        {key("e"),"x"},
        {key("f"),"{\"short\":\""+std::string(1020,'A')+"\"}"},
        {key("g"),"{\"open\":\""+std::string(4096,'B')},
        {key("h"),nativeLike(key("h"),random(30000,8),",\"flat_brep_base64\":\""+b64(random(9000,9))+"\"")}};
    ark::DerivedPackReport packed,unpacked;const auto stored=ark::encodeDerivedPack(records,ark::maxDocumentDerivedStoredBytes,&packed);
    const auto restored=ark::decodeDerivedPack(stored,&unpacked);
    check(same(records,restored),"Pack reproduces every entry byte for byte and in order");
    check(packed.entries==records.size()&&packed.omittedEntries==0&&unpacked.entries==records.size()&&unpacked.decodedBytes==packed.decodedBytes,"Pack report counts entries and decoded bytes");
    check(packed.splicedStrings==4&&unpacked.splicedStrings==4,"Only long canonical, terminated base64 values are spliced");
    check(stored.size()<900000,"Shared B-rep bytes compress once across entries despite base64 misalignment: "+std::to_string(stored.size()));
    check(ark::derivedPackManifest(restored,restored.size())==ark::derivedPackManifest(records,records.size())&&ark::derivedPackManifest(records,2)!=ark::derivedPackManifest(records,3),"Manifest identifies the exact entry prefix");
    check(ark::encodeDerivedPack({},ark::maxDocumentDerivedStoredBytes).empty(),"No entries produce no pack");
    check(rejects([&]{ark::encodeDerivedPack({{key("a"),"1"},{key("a"),"2"}},ark::maxDocumentDerivedStoredBytes);})&&rejects([&]{ark::encodeDerivedPack({{"not-a-digest","1"}},ark::maxDocumentDerivedStoredBytes);})&&rejects([&]{ark::encodeDerivedPack({{key("a"),""}},ark::maxDocumentDerivedStoredBytes);}),"Encoder rejects duplicate, invalid and empty entries");

    // Hostile or damaged stored bytes never produce entries.
    std::string flipped=stored;flipped[flipped.size()/2]^=0x40;
    std::string skippable("\x50\x2a\x4d\x18\x04\x00\x00\x00skip",12);const auto plain=body({{key("a"),"payload"}});
    check(same(ark::decodeDerivedPack(frame(plain)),{{key("a"),"payload"}}),"Hand-built fixture body decodes");
    const std::vector<std::pair<std::string,std::string>> hostile{
        {"empty",""},{"truncated",stored.substr(0,stored.size()-1)},{"flipped",flipped},{"trailing",stored+"x"},{"two frames",stored+stored},{"skippable prefix",skippable+stored},
        {"no checksum",frame(plain,false)},{"unknown content size",frame(plain,true,false)},{"wrong magic",frame("ARK-DERIVED-PACK2\n"+plain.substr(18))},
        {"count over capacity",frame([]{std::string b="ARK-DERIVED-PACK1\n";varint(b,ark::maxDocumentDerivedEntries+1);return b;}())},
        {"duplicate keys",frame(body({{key("a"),"1"},{key("a"),"2"}}))},{"uppercase key",frame(body({{std::string(64,'A'),"1"}}))},{"empty entry",frame(body({{key("a"),""}}))},
        {"trailing body",frame(body({{key("a"),"1"}})+"!")},{"truncated body",frame(body({{key("a"),"payload"}}).substr(0,90))},
        {"noncanonical varint",frame([]{std::string b="ARK-DERIVED-PACK1\n";b+="\x81";b+='\0';return b;}())},
        {"splice past entry",frame([]{std::string b="ARK-DERIVED-PACK1\n";varint(b,1);b+=key("a");varint(b,10);varint(b,1);varint(b,0);varint(b,3000);b+=std::string(3000,'z');return b;}())},
        {"splice too short",frame([]{std::string b="ARK-DERIVED-PACK1\n";varint(b,1);b+=key("a");varint(b,8);varint(b,1);varint(b,0);varint(b,6);b+=std::string(6,'z');return b;}())}};
    for(const auto& [name,bytes]:hostile)check(rejects([&]{ark::decodeDerivedPack(bytes);}),"Hostile pack rejects: "+name);

    // Over the stored capacity the pack keeps the longest document-order prefix.
    DocumentDerivedPayloads incompressible;for(int i=0;i<4;++i)incompressible.push_back({key("r"+std::to_string(i)),random(300000,20+i)});
    ark::DerivedPackReport tail;const auto prefix=ark::encodeDerivedPack(incompressible,700000,&tail);
    check(!prefix.empty()&&prefix.size()<=700000&&tail.entries>=1&&tail.entries<4&&tail.omittedEntries==4-tail.entries,"Stored capacity omits the tail instead of failing");
    const auto kept=ark::decodeDerivedPack(prefix);check(same(kept,DocumentDerivedPayloads(incompressible.begin(),incompressible.begin()+(long)tail.entries)),"Omitted tail leaves an exact prefix");
    ark::DerivedPackReport none;check(ark::encodeDerivedPack(incompressible,100,&none).empty()&&none.entries==0&&none.omittedEntries==4,"Nothing fits a tiny capacity");
}

void container(){
    Workspace workspace;const auto path=workspace.directory/"pack.ark";const auto file=pathText(path);std::string error,diagnostic;
    const auto shared=random(200000,30);
    DocumentDerivedPayloads derived{{key("one"),nativeLike(key("one"),shared)},{key("two"),nativeLike(key("two"),shared+random(3000,31))},{key("three"),std::string("ARK-MOTION-FRAMES1\n",19)+random(4000,32)}};
    ark::DocumentContainerWriteStats first;check(ark::saveDocumentContainer(root("first"),file.c_str(),&error,&first,&derived),error);
    std::vector<std::pair<std::string,std::string>> rows;{Database db(path);rows=db.rows();}
    check(rows.size()==1&&rows[0].first.rfind("pack1.",0)==0&&rows[0].first.size()==75&&rows[0].first.substr(70)==".0000","Derived entries are stored as one pack row");
    check(rows[0].first.substr(6,64).find_first_not_of("0123456789abcdef")==std::string::npos&&rows[0].first.find_first_not_of("0123456789abcdef")!=std::string::npos,"Pack row keys are not entry digests, so 050B readers discard them as derived data");
    check(first.derivedRewritten&&first.derivedEntries==3&&first.derivedStoredBytes==rows[0].second.size()&&first.derivedStoredBytes<first.derivedDecodedBytes/2,"Write stats report the compressed pack");
    V loaded;DocumentDerivedPayloads restored;ark::DocumentContainerReadStats read;
    check(ark::loadDocumentContainer(file.c_str(),loaded,&error,&restored,&diagnostic,&read),error);
    check(diagnostic.empty()&&same(restored,derived)&&canonical(loaded)==canonical(root("first")),"Pack round trip through the container");
    check(read.derivedEncoding=="zstd_pack"&&read.derivedStoredBytes==first.derivedStoredBytes&&read.derivedDecodedBytes==first.derivedDecodedBytes,"Read stats report the pack");

    ark::DocumentContainerWriteStats again;check(ark::saveDocumentContainer(root("second"),file.c_str(),&error,&again,&derived),error);
    {Database db(path);check(db.rows()==rows,"Unchanged derived entries keep the existing pack rows");}
    check(!again.derivedRewritten&&again.derivedStoredBytes==first.derivedStoredBytes&&again.derivedEntries==3,"Unchanged pack is not recompressed");
    auto changed=derived;changed[1].bytes+="!";ark::DocumentContainerWriteStats third;check(ark::saveDocumentContainer(root("third"),file.c_str(),&error,&third,&changed),error);
    check(third.derivedRewritten&&ark::loadDocumentContainer(file.c_str(),loaded,&error,&restored,&diagnostic)&&same(restored,changed),"Changed entries rewrite the pack");

    // Container v2 files written before 051A keep one row per entry.
    {Database db(path);db.exec("DELETE FROM ark_derived");for(const auto& item:derived)db.put(item.key,item.bytes);}
    check(ark::loadDocumentContainer(file.c_str(),loaded,&error,&restored,&diagnostic,&read)&&diagnostic.empty()&&read.derivedEncoding=="entries",error);
    bool legacyEqual=restored.size()==derived.size();for(const auto& item:derived)legacyEqual=legacyEqual&&std::any_of(restored.begin(),restored.end(),[&](const auto& r){return r.key==item.key&&r.bytes==item.bytes;});check(legacyEqual,"Legacy per-entry rows still load");
    ark::DocumentContainerWriteStats migrated;check(ark::saveDocumentContainer(root("third"),file.c_str(),&error,&migrated,&derived)&&migrated.derivedRewritten,error);
    {Database db(path);rows=db.rows();}check(rows.size()==1&&rows[0].first.rfind("pack1.",0)==0,"Saving a legacy file replaces entry rows with a pack");

    // Damaged optional sections are discarded with a reason; authoring stays strict and intact.
    const auto damage=[&](const std::string& sql,const std::string& name,const std::string& expected,const V& authoring){
        const auto copy=workspace.directory/(name+".ark");fs::copy_file(path,copy);{Database db(copy);db.exec(sql);}
        V value;DocumentDerivedPayloads entries{{"stale","stale"}};std::string reason,failure;check(ark::loadDocumentContainer(pathText(copy).c_str(),value,&failure,&entries,&reason),failure);
        check(entries.empty()&&reason.find(expected)!=std::string::npos&&canonical(value)==canonical(authoring),"Damaged pack is discarded ("+name+"): "+reason);
    };
    const auto manifest=rows[0].first.substr(6,64);
    damage("INSERT INTO ark_derived(key,hash,bytes) SELECT '"+key("legacy")+"',hash,bytes FROM ark_derived","mixed","mixes pack and entry rows",root("third"));
    damage("UPDATE ark_derived SET key='pack1."+manifest+".0001'","gap","inconsistent",root("third"));
    damage("UPDATE ark_derived SET key='pack1."+key("other")+".0000'","manifest","manifest mismatch",root("third"));
    damage("UPDATE ark_derived SET bytes=substr(bytes,1,length(bytes)-1),hash=hash","digest","checksum/identity mismatch",root("third"));

    // Packs over 16 MiB span several rows; a missing chunk discards the section.
    DocumentDerivedPayloads large{{key("big1"),nativeLike(key("big1"),random(9*1024*1024,40))},{key("big2"),nativeLike(key("big2"),random(9*1024*1024,41))}};
    ark::DocumentContainerWriteStats big;check(ark::saveDocumentContainer(root("large"),file.c_str(),&error,&big,&large),error);
    {Database db(path);rows=db.rows();}check(rows.size()==2&&rows[0].first.substr(70)==".0000"&&rows[1].first.substr(70)==".0001"&&rows[0].second.size()==ark::derivedPackChunkBytes,"Large pack is split into ordered 16 MiB rows");
    check(ark::loadDocumentContainer(file.c_str(),loaded,&error,&restored,&diagnostic)&&diagnostic.empty()&&same(restored,large),"Multi-row pack round trip");
    damage("DELETE FROM ark_derived WHERE key LIKE '%.0001'","missing-chunk","zstd",root("large"));

    // Removing the derived entries frees most of the file: the save compacts it.
    const auto before=fs::file_size(path);ark::DocumentContainerWriteStats compact;
    check(ark::saveDocumentContainer(root("authoring only"),file.c_str(),&error,&compact),error);
    check(compact.compacted&&compact.compactionSkipped.empty()&&compact.bytesBeforeCompaction>=before&&compact.physicalBytes==fs::file_size(path)&&fs::file_size(path)<before/10,"Mostly free file is compacted: "+std::to_string(before)+" -> "+std::to_string(fs::file_size(path))+" "+compact.compactionSkipped);
    check(workspace.staged()==0&&ark::loadDocumentContainer(file.c_str(),loaded,&error,&restored,&diagnostic)&&restored.empty()&&canonical(loaded)==canonical(root("authoring only")),"Compacted file reloads and leaves no staged copy");
    {Database db(path);sqlite3_stmt* q=nullptr;sqlite3_prepare_v2(db.db,"PRAGMA freelist_count",-1,&q,nullptr);sqlite3_step(q);check(sqlite3_column_int(q,0)==0,"Compacted file has no free pages");sqlite3_finalize(q);}
    ark::DocumentContainerWriteStats small;check(ark::saveDocumentContainer(root("authoring only, edited"),file.c_str(),&error,&small)&&!small.compacted&&small.bytesBeforeCompaction==0&&small.compactionSkipped.empty(),"A small edit does not compact");

    // Another open handle blocks the replacement on Windows: the save still succeeds.
    check(ark::saveDocumentContainer(root("large again"),file.c_str(),&error,nullptr,&large),error);
    {
        Database holder(path);holder.exec("SELECT count(*) FROM ark_meta");ark::DocumentContainerWriteStats blocked;
        check(ark::saveDocumentContainer(root("blocked"),file.c_str(),&error,&blocked),error);
#ifdef _WIN32
        check(!blocked.compacted&&!blocked.compactionSkipped.empty()&&blocked.bytesBeforeCompaction>0,"Replacement blocked by an open handle keeps the incremental save");
#else
        check(blocked.compacted,"POSIX rename replaces an open file");
#endif
    }
    check(workspace.staged()==0&&ark::loadDocumentContainer(file.c_str(),loaded,&error,&restored,&diagnostic)&&canonical(loaded)==canonical(root("blocked"))&&restored.empty(),"Blocked compaction leaves a valid saved file and no staged copy");
}
} // namespace

int main(){try{
    codec();container();
    std::cout<<"051A derived pack: "<<checks<<" checks passed (exact round trip, hostile frames, legacy rows, capacity tail, chunks, compaction)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
