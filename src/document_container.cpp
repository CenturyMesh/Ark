#include "ark/document_container.hpp"
#include "ark/iteration_history.hpp"
#include "sqlite3.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ark {
namespace {
using V=arkjson::Value;
constexpr size_t maxRows=8192;
constexpr size_t maxItems=2*1024*1024;
constexpr char codecHeader[]="ARKB\1";
constexpr char metaSchema[]="CREATE TABLE ark_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),logical_bytes INTEGER NOT NULL CHECK(logical_bytes>=0 AND logical_bytes<=4194304),root_hash TEXT NOT NULL CHECK(length(root_hash)=64),generation INTEGER NOT NULL CHECK(generation>=1))";
constexpr char payloadSchema[]="CREATE TABLE ark_payload(hash TEXT PRIMARY KEY CHECK(length(hash)=64),kind INTEGER NOT NULL CHECK(kind IN(0,1)),bytes BLOB NOT NULL) WITHOUT ROWID";
constexpr char slotsSchema[]="CREATE TABLE ark_slots(path TEXT PRIMARY KEY,payload_hash TEXT NOT NULL REFERENCES ark_payload(hash)) WITHOUT ROWID";
// Semantic derived validation belongs to the optional-section reader, so a bad
// digest/key does not make SQLite's global quick_check reject sound authoring.
constexpr char derivedSchema[]="CREATE TABLE ark_derived(key TEXT PRIMARY KEY,hash TEXT NOT NULL,bytes BLOB NOT NULL) WITHOUT ROWID";
struct Payload {int kind=0;std::string bytes;};
using Payloads=std::map<std::string,Payload>;
using Slots=std::map<std::string,std::string>;
std::filesystem::path filePath(const char* path){return std::filesystem::path(reinterpret_cast<const char8_t*>(path));}
bool digest(const std::string& value){return value.size()==64&&std::all_of(value.begin(),value.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::string hashPayload(int kind,const std::string& bytes){std::string input(1,(char)kind);input+=bytes;return iterationSha256(input);}
std::string addPayload(Payloads& payloads,int kind,std::string bytes){auto hash=hashPayload(kind,bytes);auto [it,inserted]=payloads.emplace(hash,Payload{kind,std::move(bytes)});if(!inserted&&it->second.kind!=kind)throw std::runtime_error("Container payload collision");return hash;}
void bounded(size_t current,size_t added,size_t maximum,const char* message){if(current>maximum||added>maximum-current)throw std::runtime_error(message);}
void varint(std::string& out,uint64_t n){do{unsigned char c=(unsigned char)(n&127);n>>=7;out+=(char)(c|(n?128:0));}while(n);}
void writeValue(const V& v,std::string& out,Payloads* payloads,unsigned depth,size_t& items){
    if(depth>=64||++items>maxItems)throw std::runtime_error("Binary value nesting/item limit exceeded");
    bounded(out.size(),1,maxDocumentValueBytes,"Binary value exceeds 16 MiB");
    switch(v.t){
    case V::Null:out+='\0';break;
    case V::Bool:out+=(char)(v.b?2:1);break;
    case V::Num:{
        if(!std::isfinite(v.num))throw std::runtime_error("Binary value requires finite numbers");
        if(v.num==std::floor(v.num)&&std::fabs(v.num)<=9007199254740991.0&&!std::signbit(v.num)) {out+=(char)4;varint(out,(uint64_t)v.num);}
        else if(v.num<0&&v.num==std::floor(v.num)&&v.num>=-9007199254740991.0){out+=(char)5;varint(out,(uint64_t)-v.num);}
        else{out+=(char)3;uint64_t bits=std::bit_cast<uint64_t>(v.num);for(unsigned i=0;i<8;++i)out+=(char)(bits>>(i*8));}
        break;
    }
    case V::Str:
        if(payloads&&v.str.size()>=1024){out+=(char)9;out+=addPayload(*payloads,1,v.str);}
        else{out+=(char)6;varint(out,v.str.size());bounded(out.size(),v.str.size(),maxDocumentValueBytes,"Binary value exceeds 16 MiB");out+=v.str;}break;
    case V::Arr:
        out+=(char)7;varint(out,v.arr.size());for(const auto& item:v.arr)writeValue(item,out,payloads,depth+1,items);break;
    case V::Obj:{
        std::vector<const std::pair<std::string,V>*> entries;entries.reserve(v.obj.size());for(const auto& item:v.obj)entries.push_back(&item);
        std::sort(entries.begin(),entries.end(),[](auto a,auto b){return a->first<b->first;});
        out+=(char)8;varint(out,entries.size());const std::string* previous=nullptr;
        for(auto item:entries){if(previous&&*previous==item->first)throw std::runtime_error("Duplicate binary object key");previous=&item->first;varint(out,item->first.size());bounded(out.size(),item->first.size(),maxDocumentValueBytes,"Binary value exceeds 16 MiB");out+=item->first;writeValue(item->second,out,payloads,depth+1,items);}break;
    }
    default:throw std::runtime_error("Unknown value type");
    }
    if(out.size()>maxDocumentValueBytes)throw std::runtime_error("Binary value exceeds 16 MiB");
}
std::string encode(const V& v,Payloads* payloads=nullptr){std::string out(codecHeader,5);size_t items=0;writeValue(v,out,payloads,0,items);return out;}
struct ReadBudget {size_t bytes=0,items=0;};
struct Reader {
    const std::string& bytes;const Payloads* payloads;std::set<std::string>* used;
    ReadBudget* documentBudget=nullptr;
    size_t offset=5,items=0,expanded=0;
    void charge(size_t size){if(documentBudget){bounded(documentBudget->bytes,size,maxDocumentBytes,"Expanded document exceeds 4 MiB logical capacity");documentBudget->bytes+=size;}}
    unsigned char byte(){if(offset>=bytes.size())throw std::runtime_error("Truncated binary value");return(unsigned char)bytes[offset++];}
    uint64_t number(){uint64_t result=0;for(unsigned i=0;i<10;++i){unsigned char c=byte();if(i==9&&(c&254))throw std::runtime_error("Binary integer overflow");result|=uint64_t(c&127)<<(i*7);if(!(c&128)){if(i&&c==0)throw std::runtime_error("Noncanonical binary integer");return result;}}throw std::runtime_error("Binary integer overflow");}
    std::string string(size_t size,bool authoring=true){if(size>bytes.size()-offset)throw std::runtime_error("Truncated binary string");bounded(expanded,size,maxDocumentValueBytes,"Expanded binary value exceeds 16 MiB");expanded+=size;if(authoring)charge(size);std::string result=bytes.substr(offset,size);offset+=size;return result;}
    V value(unsigned depth){
        if(depth>=64||++items>maxItems)throw std::runtime_error("Binary value nesting/item limit exceeded");
        if(documentBudget&&++documentBudget->items>maxItems)throw std::runtime_error("Document value item limit exceeded");
        charge(1);
        switch(byte()){
        case 0:return V{};
        case 1:return V::makeBool(false);
        case 2:return V::makeBool(true);
        case 3:{uint64_t bits=0;for(unsigned i=0;i<8;++i)bits|=uint64_t(byte())<<(i*8);double value=std::bit_cast<double>(bits);if(!std::isfinite(value))throw std::runtime_error("Nonfinite binary number");return V::makeNum(value);}
        case 4:case 5:{unsigned char tag=(unsigned char)bytes[offset-1];uint64_t n=number();if(n>9007199254740991ULL||(tag==5&&n==0))throw std::runtime_error("Invalid exact binary integer");return V::makeNum(tag==5?-(double)n:(double)n);}
        case 6:{uint64_t n=number();if(n>maxDocumentValueBytes)throw std::runtime_error("Oversized binary string");return V::makeStr(string((size_t)n));}
        case 7:{uint64_t n=number();if(n>maxItems-items||n>bytes.size()-offset)throw std::runtime_error("Oversized binary array");V result=V::makeArr();result.arr.reserve((size_t)n);for(uint64_t i=0;i<n;++i)result.arr.push_back(value(depth+1));return result;}
        case 8:{uint64_t n=number();if(n>maxItems-items||n>(bytes.size()-offset)/2)throw std::runtime_error("Oversized binary object");V result=V::makeObj();result.obj.reserve((size_t)n);std::string previous;for(uint64_t i=0;i<n;++i){uint64_t length=number();if(length>maxDocumentValueBytes)throw std::runtime_error("Oversized binary key");auto key=string((size_t)length);if(i&&key<=previous)throw std::runtime_error("Unsorted/duplicate binary object key");previous=key;result.obj.emplace_back(std::move(key),value(depth+1));}return result;}
        case 9:{auto hash=string(64,false);if(!payloads||!digest(hash))throw std::runtime_error("Unexpected binary blob reference");auto it=payloads->find(hash);if(it==payloads->end()||it->second.kind!=1)throw std::runtime_error("Missing binary string payload");bounded(expanded,it->second.bytes.size(),maxDocumentValueBytes,"Expanded binary value exceeds 16 MiB");expanded+=it->second.bytes.size();charge(it->second.bytes.size());if(used)used->insert(hash);return V::makeStr(it->second.bytes);}
        default:throw std::runtime_error("Unknown binary value tag");
        }
    }
};
V decode(const std::string& bytes,const Payloads* payloads=nullptr,std::set<std::string>* used=nullptr,ReadBudget* budget=nullptr){if(bytes.size()<6||bytes.size()>maxDocumentValueBytes||bytes.compare(0,5,codecHeader,5)!=0)throw std::runtime_error("Unsupported binary value format");Reader reader{bytes,payloads,used,budget};V result=reader.value(0);if(reader.offset!=bytes.size())throw std::runtime_error("Trailing binary value bytes");return result;}
struct Db {
    sqlite3* db=nullptr;
    Db(const std::filesystem::path& path,int flags){auto utf8=path.u8string();int result=sqlite3_open_v2(reinterpret_cast<const char*>(utf8.c_str()),&db,flags,nullptr);if(result!=SQLITE_OK){std::string message=db?sqlite3_errmsg(db):"cannot allocate SQLite connection";if(db)sqlite3_close(db);db=nullptr;throw std::runtime_error("Cannot open ARK container: "+message);}sqlite3_extended_result_codes(db,1);sqlite3_busy_timeout(db,0);sqlite3_limit(db,SQLITE_LIMIT_LENGTH,(int)maxDocumentValueBytes);sqlite3_limit(db,SQLITE_LIMIT_SQL_LENGTH,8192);sqlite3_limit(db,SQLITE_LIMIT_COLUMN,32);sqlite3_limit(db,SQLITE_LIMIT_EXPR_DEPTH,64);sqlite3_db_config(db,SQLITE_DBCONFIG_TRUSTED_SCHEMA,0,nullptr);sqlite3_db_config(db,SQLITE_DBCONFIG_DEFENSIVE,1,nullptr);}
    ~Db(){if(db)sqlite3_close(db);}
    void exec(const char* sql){char* error=nullptr;int result=sqlite3_exec(db,sql,nullptr,nullptr,&error);if(result!=SQLITE_OK){std::string message=error?error:sqlite3_errmsg(db);sqlite3_free(error);throw std::runtime_error("ARK container: "+message);}}
};
struct Statement {
    sqlite3_stmt* stmt=nullptr;sqlite3* db;
    Statement(Db& connection,const char* sql):db(connection.db){if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    ~Statement(){sqlite3_finalize(stmt);}
    void text(int n,const std::string& value){if(sqlite3_bind_text(stmt,n,value.data(),(int)value.size(),SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    void integer(int n,int64_t value){if(sqlite3_bind_int64(stmt,n,value)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    void blob(int n,const std::string& value){if(sqlite3_bind_blob(stmt,n,value.data(),(int)value.size(),SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    bool row(){int result=sqlite3_step(stmt);if(result==SQLITE_ROW)return true;if(result==SQLITE_DONE)return false;throw std::runtime_error(sqlite3_errmsg(db));}
    void done(){if(row())throw std::runtime_error("Unexpected ARK container result row");}
    void reset(){sqlite3_reset(stmt);sqlite3_clear_bindings(stmt);}
    std::string text(int column){if(sqlite3_column_type(stmt,column)!=SQLITE_TEXT)throw std::runtime_error("ARK container text type mismatch");auto bytes=sqlite3_column_bytes(stmt,column);if(bytes<0||bytes>(int)maxDocumentValueBytes)throw std::runtime_error("ARK container text size exceeded");return std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt,column)),(size_t)bytes);}
    std::string blob(int column,size_t maximum=maxDocumentValueBytes){if(sqlite3_column_type(stmt,column)!=SQLITE_BLOB)throw std::runtime_error("ARK container payload type mismatch");auto bytes=sqlite3_column_bytes(stmt,column);if(bytes<0||(size_t)bytes>maximum)throw std::runtime_error("ARK container payload size exceeded");if(!bytes)return{};return std::string(reinterpret_cast<const char*>(sqlite3_column_blob(stmt,column)),(size_t)bytes);}
    int64_t integer(int column){if(sqlite3_column_type(stmt,column)!=SQLITE_INTEGER)throw std::runtime_error("ARK container integer type mismatch");return sqlite3_column_int64(stmt,column);}
};
int64_t scalar(Db& db,const char* sql){Statement query(db,sql);if(!query.row())throw std::runtime_error("Missing ARK container scalar");auto value=query.integer(0);if(query.row())throw std::runtime_error("Multiple ARK container scalar rows");return value;}
int schema(Db& db){
    if(scalar(db,"PRAGMA application_id")!=documentContainerApplicationId)throw std::runtime_error("SQLite file is not an ARK document");
    const auto version=scalar(db,"PRAGMA user_version");if(version!=1&&version!=2)throw std::runtime_error("Unsupported ARK container version");
    if(scalar(db,"PRAGMA page_size")!=4096)throw std::runtime_error("Unsupported ARK container page size");
    Statement mode(db,"PRAGMA journal_mode");if(!mode.row()||mode.text(0)!="delete")throw std::runtime_error("Unsupported ARK container journal mode");
    std::map<std::string,std::string> expected{{"ark_meta",metaSchema},{"ark_payload",payloadSchema},{"ark_slots",slotsSchema}};
    if(version==2)expected.emplace("ark_derived",derivedSchema);
    Statement query(db,"SELECT type,name,tbl_name,sql FROM sqlite_schema ORDER BY name");size_t count=0;
    while(query.row()){auto name=query.text(1);auto found=expected.find(name);if(query.text(0)!="table"||query.text(2)!=name||found==expected.end()||query.text(3)!=found->second)throw std::runtime_error("Unsupported ARK container schema");++count;}
    if(count!=expected.size())throw std::runtime_error("Incomplete ARK container schema");
    Statement check(db,"PRAGMA quick_check(1)");if(!check.row()||check.text(0)!="ok"||check.row())throw std::runtime_error("Corrupt ARK container pages");
    Statement foreign(db,"PRAGMA foreign_key_check");if(foreign.row())throw std::runtime_error("Broken ARK container payload references");
    return (int)version;
}
void init(Db& db){db.exec("PRAGMA page_size=4096");db.exec("PRAGMA journal_mode=DELETE");db.exec("PRAGMA synchronous=FULL");db.exec("PRAGMA max_page_count=16384");db.exec("PRAGMA application_id=1095912241");db.exec("PRAGMA user_version=1");db.exec(metaSchema);db.exec(payloadSchema);db.exec(slotsSchema);}
const V& required(const V& value,const char* key){auto p=value.find(key);if(!p)throw std::runtime_error(std::string("Container manifest missing ")+key);return*p;}
std::string text(const V& value,const char* key){const auto& v=required(value,key);if(v.t!=V::Str)throw std::runtime_error("Invalid container manifest text");return v.str;}
struct Package {
    Payloads payloads;Slots slots;size_t logicalBytes=0;std::string rootHash;
    V slot(const V& value,const std::string& path){if(slots.size()>=maxRows||path.size()>4096)throw std::runtime_error("ARK container slot capacity exceeded");auto hash=addPayload(payloads,0,encode(value,&payloads));if(!slots.emplace(path,hash).second)throw std::runtime_error("Duplicate ARK container slot");V d=V::makeObj();d.set("k",V::makeStr("v"));d.set("p",V::makeStr(path));return d;}
    V split(const V& value,const std::string& path,unsigned depth,const std::string& key={}){
        bool object=value.t==V::Obj&&(depth==0||(depth==1&&key=="iteration_history")||(depth==2&&key=="blobs"));
        if(object){V d=V::makeObj(),entries=V::makeArr();d.set("k",V::makeStr("o"));std::vector<const std::pair<std::string,V>*> ordered;for(const auto& entry:value.obj)ordered.push_back(&entry);std::sort(ordered.begin(),ordered.end(),[](auto a,auto b){return a->first<b->first;});for(const auto* item:ordered){V entry=V::makeArr();entry.arr.push_back(V::makeStr(item->first));entry.arr.push_back(split(item->second,path+"/"+item->first,depth+1,item->first));entries.arr.push_back(std::move(entry));}d.set("e",std::move(entries));return d;}
        if(value.t==V::Arr&&(depth==1||depth==2)){V d=V::makeObj(),entries=V::makeArr();d.set("k",V::makeStr("a"));for(size_t i=0;i<value.arr.size();++i)entries.arr.push_back(slot(value.arr[i],path+"/"+std::to_string(i)));d.set("e",std::move(entries));return d;}
        return slot(value,path);
    }
    explicit Package(const V& root){if(root.t!=V::Obj)throw std::runtime_error("ARK document root must be an object");logicalBytes=root.dump().size();if(logicalBytes>maxDocumentBytes)throw std::runtime_error("Document exceeds 4 MiB logical capacity");rootHash=iterationSha256(encode(root));V manifest=split(root,"r",0);slots.emplace("/",addPayload(payloads,0,encode(manifest)));size_t total=0;for(const auto& item:payloads){bounded(total,item.second.bytes.size(),maxDocumentValueBytes,"Container payload total exceeds 16 MiB");total+=item.second.bytes.size();}}
};
V assemble(const V& descriptor,const Slots& slots,const Payloads& payloads,std::set<std::string>& usedSlots,std::set<std::string>& usedPayloads,ReadBudget& budget,unsigned depth){
    if(depth>=64||descriptor.t!=V::Obj||descriptor.obj.size()!=2)throw std::runtime_error("Invalid container manifest shape");
    auto kind=text(descriptor,"k");
    if(kind=="v"){auto path=text(descriptor,"p");auto slot=slots.find(path);if(slot==slots.end()||!usedSlots.insert(path).second)throw std::runtime_error("Missing/duplicate ARK container slot");auto payload=payloads.find(slot->second);if(payload==payloads.end()||payload->second.kind!=0)throw std::runtime_error("Missing typed ARK payload");usedPayloads.insert(payload->first);return decode(payload->second.bytes,&payloads,&usedPayloads,&budget);}
    const auto& entries=required(descriptor,"e");if(entries.t!=V::Arr||entries.arr.size()>maxRows)throw std::runtime_error("Invalid container manifest entries");
    if(kind=="a"){V value=V::makeArr();for(const auto& entry:entries.arr)value.arr.push_back(assemble(entry,slots,payloads,usedSlots,usedPayloads,budget,depth+1));return value;}
    if(kind=="o"){V value=V::makeObj();std::string previous;for(size_t i=0;i<entries.arr.size();++i){const auto& entry=entries.arr[i];if(entry.t!=V::Arr||entry.arr.size()!=2||entry.arr[0].t!=V::Str||(i&&entry.arr[0].str<=previous))throw std::runtime_error("Invalid container manifest object key");previous=entry.arr[0].str;bounded(budget.bytes,previous.size(),maxDocumentBytes,"Expanded document exceeds 4 MiB logical capacity");budget.bytes+=previous.size();value.obj.emplace_back(previous,assemble(entry.arr[1],slots,payloads,usedSlots,usedPayloads,budget,depth+1));}return value;}
    throw std::runtime_error("Unknown ARK container manifest kind");
}
size_t physicalSize(const std::filesystem::path& path){std::error_code error;auto size=std::filesystem::file_size(path,error);if(error||size>maxDocumentContainerBytes)throw std::runtime_error("ARK container exceeds 256 MiB physical capacity");if(size==0||size%4096!=0)throw std::runtime_error("Truncated ARK container pages");return(size_t)size;}
void checkDerived(const DocumentDerivedPayloads* derived){
    if(!derived)return;size_t total=0;std::set<std::string> keys;
    if(derived->size()>maxRows)throw std::runtime_error("ARK derived row capacity exceeded");
    for(const auto& item:*derived){if(!digest(item.key)||!keys.insert(item.key).second)throw std::runtime_error("Invalid/duplicate ARK derived key");if(item.bytes.empty()||item.bytes.size()>maxDocumentDerivedEntryBytes)throw std::runtime_error("ARK derived entry exceeds capacity");bounded(total,item.bytes.size(),maxDocumentDerivedBytes,"ARK derived section exceeds 128 MiB");total+=item.bytes.size();}
}
void writePackage(Db& db,const Package& package,DocumentContainerWriteStats& stats,const DocumentDerivedPayloads* derived){
    const bool existingV2=scalar(db,"PRAGMA user_version")==2;
    const bool useDerived=existingV2||(derived&&!derived->empty());
    db.exec("PRAGMA synchronous=FULL");db.exec(useDerived?"PRAGMA max_page_count=65536":"PRAGMA max_page_count=16384");db.exec("BEGIN IMMEDIATE");
    try{
        if(useDerived&&!existingV2){db.exec(derivedSchema);db.exec("PRAGMA user_version=2");}
        Statement meta(db,"SELECT generation FROM ark_meta WHERE singleton=1");int64_t previous=0;if(meta.row()){previous=meta.integer(0);if(previous<1||previous==std::numeric_limits<int64_t>::max()||meta.row())throw std::runtime_error("Invalid container generation");}stats.generation=(uint64_t)(previous+1);
        Statement find(db,"SELECT kind,bytes FROM ark_payload WHERE hash=?1"),insert(db,"INSERT INTO ark_payload(hash,kind,bytes) VALUES(?1,?2,?3)");
        for(const auto& item:package.payloads){find.text(1,item.first);if(find.row()){if(find.integer(0)!=item.second.kind||find.blob(1)!=item.second.bytes||find.row())throw std::runtime_error("Existing ARK payload digest/content mismatch");}else{insert.text(1,item.first);insert.integer(2,item.second.kind);insert.blob(3,item.second.bytes);insert.done();insert.reset();++stats.payloadsInserted;stats.payloadBytesInserted+=item.second.bytes.size();}find.reset();}
        Slots old;Statement current(db,"SELECT path,payload_hash FROM ark_slots");while(current.row()){if(old.size()>=maxRows)throw std::runtime_error("Existing container slot capacity exceeded");old.emplace(current.text(0),current.text(1));}
        Statement upsert(db,"INSERT INTO ark_slots(path,payload_hash) VALUES(?1,?2) ON CONFLICT(path) DO UPDATE SET payload_hash=excluded.payload_hash"),remove(db,"DELETE FROM ark_slots WHERE path=?1");
        for(const auto& item:package.slots){auto found=old.find(item.first);if(found==old.end()||found->second!=item.second){upsert.text(1,item.first);upsert.text(2,item.second);upsert.done();upsert.reset();++stats.slotsChanged;}if(found!=old.end())old.erase(found);}
        for(const auto& item:old){remove.text(1,item.first);remove.done();remove.reset();++stats.slotsRemoved;}
        db.exec("DELETE FROM ark_payload WHERE hash NOT IN(SELECT payload_hash FROM ark_slots) AND kind=0");
        // Raw string payloads may be referenced inside typed chunks, so prune with
        // the exact current package census rather than only SQL foreign keys.
        Statement all(db,"SELECT hash FROM ark_payload WHERE kind=1");std::vector<std::string> obsolete;while(all.row()){auto hash=all.text(0);if(!package.payloads.count(hash))obsolete.push_back(std::move(hash));}
        Statement removePayload(db,"DELETE FROM ark_payload WHERE hash=?1");for(const auto& hash:obsolete){removePayload.text(1,hash);removePayload.done();removePayload.reset();}
        Statement update(db,"INSERT INTO ark_meta(singleton,logical_bytes,root_hash,generation) VALUES(1,?1,?2,?3) ON CONFLICT(singleton) DO UPDATE SET logical_bytes=excluded.logical_bytes,root_hash=excluded.root_hash,generation=excluded.generation");update.integer(1,(int64_t)package.logicalBytes);update.text(2,package.rootHash);update.integer(3,(int64_t)stats.generation);update.done();
        if(useDerived){
            std::set<std::string> stale;Statement oldDerived(db,"SELECT key FROM ark_derived");while(oldDerived.row())stale.insert(oldDerived.text(0));
            Statement findDerived(db,"SELECT hash,bytes FROM ark_derived WHERE key=?1"),putDerived(db,"INSERT INTO ark_derived(key,hash,bytes) VALUES(?1,?2,?3) ON CONFLICT(key) DO UPDATE SET hash=excluded.hash,bytes=excluded.bytes");
            if(derived)for(const auto& item:*derived){const auto hash=iterationSha256(item.bytes);findDerived.text(1,item.key);bool same=false;if(findDerived.row())same=findDerived.text(0)==hash&&findDerived.blob(1,maxDocumentDerivedEntryBytes)==item.bytes;findDerived.reset();if(!same){putDerived.text(1,item.key);putDerived.text(2,hash);putDerived.blob(3,item.bytes);putDerived.done();putDerived.reset();}stale.erase(item.key);}
            Statement removeDerived(db,"DELETE FROM ark_derived WHERE key=?1");for(const auto& key:stale){removeDerived.text(1,key);removeDerived.done();removeDerived.reset();}
        }
        db.exec("COMMIT");
    }catch(...){try{db.exec("ROLLBACK");}catch(...){}throw;}
}
bool replace(const std::filesystem::path& staged,const std::filesystem::path& destination){
#ifdef _WIN32
    return MoveFileExW(staged.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::error_code error;std::filesystem::rename(staged,destination,error);if(error)return false;auto parent=destination.parent_path();if(parent.empty())parent=".";int fd=open(parent.c_str(),O_RDONLY|O_DIRECTORY);if(fd>=0){fsync(fd);close(fd);}return true;
#endif
}
void cleanup(const std::filesystem::path& path){std::error_code error;std::filesystem::remove(path,error);auto journal=path;journal+="-journal";std::filesystem::remove(journal,error);}
}

bool documentContainerFile(const char* path){if(!path||!*path)return false;std::ifstream input(filePath(path),std::ios::binary);char header[16]{};input.read(header,16);return input.gcount()==16&&std::memcmp(header,"SQLite format 3\0",16)==0;}
bool encodeDocumentValue(const V& value,std::string& bytes,std::string* error){try{auto encoded=encode(value);bytes=std::move(encoded);return true;}catch(const std::exception& e){if(error)*error=e.what();return false;}}
bool decodeDocumentValue(const std::string& bytes,V& value,std::string* error){try{auto decoded=decode(bytes);value=std::move(decoded);return true;}catch(const std::exception& e){if(error)*error=e.what();return false;}}
bool saveDocumentContainer(const V& root,const char* path,std::string* error,DocumentContainerWriteStats* receipt,const DocumentDerivedPayloads* derived){
    std::filesystem::path staged;
    try{
        if(!path||!*path)throw std::runtime_error("Empty document path");
        checkDerived(derived);Package package(root);auto destination=filePath(path);DocumentContainerWriteStats stats;
        if(documentContainerFile(path)){physicalSize(destination);{Db db(destination,SQLITE_OPEN_READWRITE|SQLITE_OPEN_NOMUTEX);sqlite3_limit(db.db,SQLITE_LIMIT_LENGTH,(int)maxDocumentDerivedEntryBytes+4096);schema(db);writePackage(db,package,stats,derived);}stats.physicalBytes=physicalSize(destination);}
        else{
            std::error_code existsError;bool exists=std::filesystem::exists(destination,existsError);if(existsError)throw std::runtime_error("Cannot inspect document destination");
            if(exists){std::ifstream prior(destination,std::ios::binary);char header[16]{};prior.read(header,16);if(prior.gcount()>=6&&std::memcmp(header,"SQLite",6)==0)throw std::runtime_error("Refusing to replace an unrecognized SQLite file");}
            static std::atomic<uint64_t> sequence{0};auto tick=std::chrono::steady_clock::now().time_since_epoch().count();staged=destination;staged+=".ark-stage-"+std::to_string(tick)+"-"+std::to_string(++sequence);
            if(std::filesystem::exists(staged))throw std::runtime_error("Staged container already exists");
            {Db db(staged,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX);sqlite3_limit(db.db,SQLITE_LIMIT_LENGTH,(int)maxDocumentDerivedEntryBytes+4096);init(db);writePackage(db,package,stats,derived);}stats.physicalBytes=physicalSize(staged);
            if(!replace(staged,destination))throw std::runtime_error("Cannot replace document with staged ARK container");
            staged.clear();stats.stagedReplacement=true;
        }
        if(receipt)*receipt=stats;
        return true;
    }catch(const std::exception& e){if(!staged.empty())cleanup(staged);if(error)*error=e.what();return false;}
}
bool loadDocumentContainer(const char* path,V& root,std::string* error,DocumentDerivedPayloads* derived,std::string* derivedDiscardedReason){
    try{
        if(!path||!*path)throw std::runtime_error("Empty document path");
        if(!documentContainerFile(path))throw std::runtime_error("Not an ARK binary container");
        auto source=filePath(path);physicalSize(source);
        // Read/write permits SQLite to recover a hot rollback journal after an
        // interrupted process. Read-only files remain readable without recovery.
        std::unique_ptr<Db> connection;try{connection=std::make_unique<Db>(source,SQLITE_OPEN_READWRITE|SQLITE_OPEN_NOMUTEX);}catch(...){connection=std::make_unique<Db>(source,SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX);}
        Db& db=*connection;sqlite3_limit(db.db,SQLITE_LIMIT_LENGTH,(int)maxDocumentDerivedEntryBytes+4096);db.exec("PRAGMA query_only=ON");db.exec("BEGIN");const int version=schema(db);
        Statement meta(db,"SELECT logical_bytes,root_hash,generation FROM ark_meta WHERE singleton=1");if(!meta.row())throw std::runtime_error("Missing ARK document metadata");auto logical=meta.integer(0);auto rootHash=meta.text(1);auto generation=meta.integer(2);if(logical<0||logical>(int64_t)maxDocumentBytes||!digest(rootHash)||generation<1||meta.row())throw std::runtime_error("Invalid ARK document metadata");
        Payloads payloads;size_t total=0;Statement payload(db,"SELECT hash,kind,bytes FROM ark_payload");while(payload.row()){if(payloads.size()>=maxRows*2)throw std::runtime_error("ARK payload row capacity exceeded");auto hash=payload.text(0);auto kind=payload.integer(1);auto bytes=payload.blob(2);bounded(total,bytes.size(),maxDocumentValueBytes,"ARK payload total exceeds 16 MiB");total+=bytes.size();if((kind!=0&&kind!=1)||!digest(hash)||hashPayload((int)kind,bytes)!=hash)throw std::runtime_error("ARK payload checksum mismatch");payloads.emplace(std::move(hash),Payload{(int)kind,std::move(bytes)});}
        Slots slots;Statement slot(db,"SELECT path,payload_hash FROM ark_slots");while(slot.row()){if(slots.size()>=maxRows)throw std::runtime_error("ARK slot row capacity exceeded");auto name=slot.text(0),hash=slot.text(1);if(name.size()>4096||!digest(hash)||!payloads.count(hash))throw std::runtime_error("Invalid ARK slot reference");slots.emplace(std::move(name),std::move(hash));}
        auto manifestSlot=slots.find("/");if(manifestSlot==slots.end())throw std::runtime_error("Missing ARK document manifest");auto manifestPayload=payloads.find(manifestSlot->second);if(manifestPayload==payloads.end()||manifestPayload->second.kind!=0)throw std::runtime_error("Missing typed ARK manifest");std::set<std::string> usedSlots{"/"},usedPayloads{manifestPayload->first};V manifest=decode(manifestPayload->second.bytes);ReadBudget budget;V candidate=assemble(manifest,slots,payloads,usedSlots,usedPayloads,budget,0);
        if(candidate.t!=V::Obj||usedSlots.size()!=slots.size()||usedPayloads.size()!=payloads.size())throw std::runtime_error("Unreferenced ARK container data");
        if(candidate.dump().size()!=(size_t)logical||iterationSha256(encode(candidate))!=rootHash)throw std::runtime_error("ARK document root checksum/size mismatch");
        DocumentDerivedPayloads restored;std::string discarded;
        if(version==2)try{
            size_t totalDerived=0;Statement rows(db,"SELECT key,hash,bytes FROM ark_derived ORDER BY key");
            while(rows.row()){if(restored.size()>=maxRows)throw std::runtime_error("ARK derived row capacity exceeded");auto key=rows.text(0),hash=rows.text(1),bytes=rows.blob(2,maxDocumentDerivedEntryBytes);bounded(totalDerived,bytes.size(),maxDocumentDerivedBytes,"ARK derived section exceeds 128 MiB");totalDerived+=bytes.size();if(!digest(key)||!digest(hash)||bytes.empty()||iterationSha256(bytes)!=hash)throw std::runtime_error("ARK derived payload checksum/identity mismatch");restored.push_back({std::move(key),std::move(bytes)});}
        }catch(const std::exception& failure){restored.clear();discarded=failure.what();}
        db.exec("COMMIT");root=std::move(candidate);if(derived)*derived=std::move(restored);if(derivedDiscardedReason)*derivedDiscardedReason=std::move(discarded);return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
} // namespace ark
