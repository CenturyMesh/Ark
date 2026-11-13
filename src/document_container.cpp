#include "ark/document_container.hpp"
#include "ark/derived_pack.hpp"
#include "ark/iteration_history.hpp"
#include "sqlite3.h"
#include "zstd.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
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
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ark {
namespace {
using V=arkjson::Value;
constexpr size_t legacyRows=8192,legacyItems=2*1024*1024;
constexpr size_t legacyLogicalBytes=4u*1024u*1024u,legacyValueBytes=16u*1024u*1024u,legacyPhysicalBytes=256u*1024u*1024u;
constexpr size_t maxDerivedRows=maxDocumentDerivedEntries;
constexpr char codecHeader[]="ARKB\1";
constexpr char legacyMetaSchema[]="CREATE TABLE ark_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),logical_bytes INTEGER NOT NULL CHECK(logical_bytes>=0 AND logical_bytes<=4194304),root_hash TEXT NOT NULL CHECK(length(root_hash)=64),generation INTEGER NOT NULL CHECK(generation>=1))";
constexpr char capacityMetaSchema[]="CREATE TABLE ark_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),logical_bytes INTEGER NOT NULL CHECK(logical_bytes>=0),root_hash TEXT NOT NULL CHECK(length(root_hash)=64),generation INTEGER NOT NULL CHECK(generation>=1))";
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
std::string containerDigest(const std::string& bytes){
#ifdef _WIN32
    // Same SHA-256 bytes as existing containers. CNG avoids copying/padding the
    // complete optional geometry section in the portable implementation.
    struct Provider {
        BCRYPT_ALG_HANDLE handle=nullptr;
        Provider(){if(BCryptOpenAlgorithmProvider(&handle,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)handle=nullptr;}
        ~Provider(){if(handle)BCryptCloseAlgorithmProvider(handle,0);}
    };
    static Provider provider;
    struct Hash {BCRYPT_HASH_HANDLE handle=nullptr;~Hash(){if(handle)BCryptDestroyHash(handle);}} hash;
    unsigned char result[32]{};
    if(provider.handle&&bytes.size()<=std::numeric_limits<ULONG>::max()&&BCryptCreateHash(provider.handle,&hash.handle,nullptr,0,nullptr,0,0)>=0&&
       BCryptHashData(hash.handle,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),(ULONG)bytes.size(),0)>=0&&
       BCryptFinishHash(hash.handle,result,sizeof result,0)>=0){
        constexpr char hex[]="0123456789abcdef";std::string out;out.reserve(64);
        for(auto byte:result){out+=hex[byte>>4];out+=hex[byte&15];}return out;
    }
#endif
    return iterationSha256(bytes);
}
std::string hashPayload(int kind,const std::string& bytes){std::string input(1,(char)kind);input+=bytes;return containerDigest(input);}
std::string addPayload(Payloads& payloads,int kind,std::string bytes){auto hash=hashPayload(kind,bytes);auto [it,inserted]=payloads.emplace(hash,Payload{kind,std::move(bytes)});if(!inserted&&it->second.kind!=kind)throw std::runtime_error("Container payload collision");return hash;}
void bounded(size_t current,size_t added,size_t maximum,const char* message){if(current>maximum||added>maximum-current)throw std::runtime_error(message);}
void varint(std::string& out,uint64_t n){do{unsigned char c=(unsigned char)(n&127);n>>=7;out+=(char)(c|(n?128:0));}while(n);}
void writeValue(const V& v,std::string& out,Payloads* payloads,unsigned depth,size_t& items){
    if(depth>=64)throw std::runtime_error("Binary value nesting limit exceeded");
    if(items==std::numeric_limits<size_t>::max())throw std::runtime_error("Binary value item count overflow");++items;
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
        else{out+=(char)6;varint(out,v.str.size());out+=v.str;}break;
    case V::Arr:
        out+=(char)7;varint(out,v.arr.size());for(const auto& item:v.arr)writeValue(item,out,payloads,depth+1,items);break;
    case V::Obj:{
        std::vector<const std::pair<std::string,V>*> entries;entries.reserve(v.obj.size());for(const auto& item:v.obj)entries.push_back(&item);
        std::sort(entries.begin(),entries.end(),[](auto a,auto b){return a->first<b->first;});
        out+=(char)8;varint(out,entries.size());const std::string* previous=nullptr;
        for(auto item:entries){if(previous&&*previous==item->first)throw std::runtime_error("Duplicate binary object key");previous=&item->first;varint(out,item->first.size());out+=item->first;writeValue(item->second,out,payloads,depth+1,items);}break;
    }
    default:throw std::runtime_error("Unknown value type");
    }
}
size_t jsonStringSize(const std::string& value){size_t size=2;for(unsigned char c:value){const size_t n=c=='"'||c=='\\'||c=='\n'||c=='\r'||c=='\t'?2:c<0x20?6:1;bounded(size,n,std::numeric_limits<size_t>::max(),"JSON size overflow");size+=n;}return size;}
size_t jsonSize(const V& value,unsigned depth=0){
    if(depth>=64)throw std::runtime_error("Document nesting limit exceeded");size_t size=0;auto add=[&](size_t n){bounded(size,n,std::numeric_limits<size_t>::max(),"JSON size overflow");size+=n;};
    switch(value.t){case V::Null:return 4;case V::Bool:return value.b?4:5;case V::Num:{char bytes[32];if(!std::isfinite(value.num))return 4;if(value.num==std::floor(value.num)&&std::fabs(value.num)<1e15)std::snprintf(bytes,sizeof bytes,"%lld",(long long)value.num);else std::snprintf(bytes,sizeof bytes,"%.17g",value.num);return std::strlen(bytes);}case V::Str:return jsonStringSize(value.str);case V::Arr:add(2);if(!value.arr.empty())add(value.arr.size()-1);for(const auto& item:value.arr)add(jsonSize(item,depth+1));return size;case V::Obj:add(2);if(!value.obj.empty())add(value.obj.size()-1);for(const auto& item:value.obj){add(jsonStringSize(item.first));add(1);add(jsonSize(item.second,depth+1));}return size;default:throw std::runtime_error("Invalid JSON value");}
}
size_t varintSize(uint64_t n){size_t result=1;while(n>=128){n>>=7;++result;}return result;}
size_t encodedSize(const V& value,DocumentReadBudget072& budget,unsigned depth=0){
    if(depth>=64)throw std::runtime_error("Binary value nesting limit exceeded");size_t size=1;
    auto add=[&](size_t bytes){bounded(size,bytes,std::numeric_limits<size_t>::max(),"Binary value size overflow");size+=bytes;};
    switch(value.t){
    case V::Null:case V::Bool:break;
    case V::Num:if(value.num==std::floor(value.num)&&std::fabs(value.num)<=9007199254740991.0&&!std::signbit(value.num))add(varintSize((uint64_t)value.num));else if(value.num<0&&value.num==std::floor(value.num)&&value.num>=-9007199254740991.0)add(varintSize((uint64_t)-value.num));else add(8);break;
    case V::Str:add(varintSize(value.str.size()));add(value.str.size());break;
    case V::Arr:add(varintSize(value.arr.size()));for(const auto& item:value.arr)add(encodedSize(item,budget,depth+1));break;
    case V::Obj:budget.charge(documentCheckedProduct072(value.obj.size(),sizeof(const std::pair<std::string,V>*)));add(varintSize(value.obj.size()));for(const auto& item:value.obj){add(varintSize(item.first.size()));add(item.first.size());add(encodedSize(item.second,budget,depth+1));}break;
    default:throw std::runtime_error("Unknown binary value type");
    }return size;
}
std::string encode(const V& v,Payloads* payloads=nullptr,DocumentReadBudget072* budget=nullptr,size_t* itemCount=nullptr){
    std::string out;if(budget){const auto size=encodedSize(v,*budget);bounded(size,5,std::numeric_limits<size_t>::max(),"Binary value size overflow");budget->charge(size+5);out.reserve(size+5);}out.assign(codecHeader,5);size_t items=0;writeValue(v,out,payloads,0,items);if(itemCount)*itemCount=items;return out;
}
struct Reader {
    const std::string& bytes;const Payloads* payloads;std::set<std::string>* used;
    DocumentReadBudget072& budget;size_t offset=5;
    unsigned char byte(){if(offset>=bytes.size())throw std::runtime_error("Truncated binary value");return(unsigned char)bytes[offset++];}
    uint64_t number(){uint64_t result=0;for(unsigned i=0;i<10;++i){unsigned char c=byte();if(i==9&&(c&254))throw std::runtime_error("Binary integer overflow");result|=uint64_t(c&127)<<(i*7);if(!(c&128)){if(i&&c==0)throw std::runtime_error("Noncanonical binary integer");return result;}}throw std::runtime_error("Binary integer overflow");}
    size_t length(){const auto n=number();if(n>std::numeric_limits<size_t>::max())throw std::runtime_error("Binary length overflow");return(size_t)n;}
    std::string string(size_t size){if(size>bytes.size()-offset)throw std::runtime_error("Truncated binary string");budget.charge(size);std::string result=bytes.substr(offset,size);offset+=size;return result;}
    V value(unsigned depth,bool reserved=false){
        if(depth>=64)throw std::runtime_error("Binary value nesting limit exceeded");if(!reserved)budget.charge(sizeof(V));
        switch(byte()){
        case 0:return V{};
        case 1:return V::makeBool(false);
        case 2:return V::makeBool(true);
        case 3:{uint64_t bits=0;for(unsigned i=0;i<8;++i)bits|=uint64_t(byte())<<(i*8);double value=std::bit_cast<double>(bits);if(!std::isfinite(value))throw std::runtime_error("Nonfinite binary number");return V::makeNum(value);}
        case 4:case 5:{unsigned char tag=(unsigned char)bytes[offset-1];uint64_t n=number();if(n>9007199254740991ULL||(tag==5&&n==0))throw std::runtime_error("Invalid exact binary integer");return V::makeNum(tag==5?-(double)n:(double)n);}
        case 6:return V::makeStr(string(length()));
        case 7:{const size_t n=length();if(n>bytes.size()-offset)throw std::runtime_error("Oversized binary array");budget.charge(documentCheckedProduct072(n,sizeof(V)));V result=V::makeArr();result.arr.reserve(n);for(size_t i=0;i<n;++i)result.arr.push_back(value(depth+1,true));return result;}
        case 8:{const size_t n=length();if(n>(bytes.size()-offset)/2)throw std::runtime_error("Oversized binary object");budget.charge(documentCheckedProduct072(n,sizeof(std::pair<std::string,V>)));V result=V::makeObj();result.obj.reserve(n);for(size_t i=0;i<n;++i){auto key=string(length());if(i&&key<=result.obj.back().first)throw std::runtime_error("Unsorted/duplicate binary object key");result.obj.emplace_back(std::move(key),value(depth+1,true));}return result;}
        case 9:{auto hash=string(64);if(!payloads||!digest(hash))throw std::runtime_error("Unexpected binary blob reference");auto it=payloads->find(hash);if(it==payloads->end()||it->second.kind!=1)throw std::runtime_error("Missing binary string payload");budget.charge(it->second.bytes.size());if(used&&!used->count(hash)){budget.charge(sizeof(std::string)+64+4*sizeof(void*));used->insert(hash);}return V::makeStr(it->second.bytes);}
        default:throw std::runtime_error("Unknown binary value tag");
        }
    }
};
V decode(const std::string& bytes,const Payloads* payloads=nullptr,std::set<std::string>* used=nullptr,DocumentReadBudget072* shared=nullptr){
    if(bytes.size()<6||bytes.compare(0,5,codecHeader,5)!=0)throw std::runtime_error("Unsupported binary value format");DocumentReadBudget072 local(shared?shared->limit:documentReadMemoryBudget072());auto& budget=shared?*shared:local;Reader reader{bytes,payloads,used,budget};V result=reader.value(0);if(reader.offset!=bytes.size())throw std::runtime_error("Trailing binary value bytes");return result;
}
struct Db {
    sqlite3* db=nullptr;DocumentReadBudget072* readBudget=nullptr;
    Db(const std::filesystem::path& path,int flags,DocumentReadBudget072* budget=nullptr):readBudget(budget){auto utf8=path.u8string();int result=sqlite3_open_v2(reinterpret_cast<const char*>(utf8.c_str()),&db,flags,nullptr);if(result!=SQLITE_OK){std::string message=db?sqlite3_errmsg(db):"cannot allocate SQLite connection";if(db)sqlite3_close(db);db=nullptr;throw std::runtime_error("Cannot open ARK container: "+message);}sqlite3_extended_result_codes(db,1);sqlite3_busy_timeout(db,0);if(readBudget)sqlite3_limit(db,SQLITE_LIMIT_LENGTH,(int)std::min<size_t>(readBudget->limit,(size_t)std::numeric_limits<int>::max()));sqlite3_limit(db,SQLITE_LIMIT_SQL_LENGTH,8192);sqlite3_limit(db,SQLITE_LIMIT_COLUMN,32);sqlite3_limit(db,SQLITE_LIMIT_EXPR_DEPTH,64);sqlite3_db_config(db,SQLITE_DBCONFIG_TRUSTED_SCHEMA,0,nullptr);sqlite3_db_config(db,SQLITE_DBCONFIG_DEFENSIVE,1,nullptr);}
    ~Db(){if(db)sqlite3_close(db);}
    void exec(const char* sql){char* error=nullptr;int result=sqlite3_exec(db,sql,nullptr,nullptr,&error);if(result!=SQLITE_OK){std::string message=error?error:sqlite3_errmsg(db);sqlite3_free(error);throw std::runtime_error("ARK container: "+message);}}
};
struct Statement {
    sqlite3_stmt* stmt=nullptr;sqlite3* db;DocumentReadBudget072* budget;
    Statement(Db& connection,const char* sql):db(connection.db),budget(connection.readBudget){if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    ~Statement(){sqlite3_finalize(stmt);}
    void text(int n,const std::string& value){if(value.size()>(size_t)std::numeric_limits<int>::max())throw std::runtime_error("SQLite text exceeds backend integer-length range");if(sqlite3_bind_text(stmt,n,value.data(),(int)value.size(),SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    void integer(int n,int64_t value){if(sqlite3_bind_int64(stmt,n,value)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    void blob(int n,const std::string& value){if(value.size()>(size_t)std::numeric_limits<int>::max())throw std::runtime_error("SQLite blob exceeds backend integer-length range");if(sqlite3_bind_blob(stmt,n,value.data(),(int)value.size(),SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    bool row(){int result=sqlite3_step(stmt);if(result==SQLITE_ROW)return true;if(result==SQLITE_DONE)return false;throw std::runtime_error(sqlite3_errmsg(db));}
    void done(){if(row())throw std::runtime_error("Unexpected ARK container result row");}
    void reset(){sqlite3_reset(stmt);sqlite3_clear_bindings(stmt);}
    std::string text(int column,size_t maximum=std::numeric_limits<size_t>::max(),bool accounted=false){if(sqlite3_column_type(stmt,column)!=SQLITE_TEXT)throw std::runtime_error("ARK container text type mismatch");auto bytes=sqlite3_column_bytes(stmt,column);if(bytes<0||(size_t)bytes>maximum)throw std::runtime_error("ARK container text size exceeded");if(budget&&!accounted)budget->charge((size_t)bytes);return std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt,column)),(size_t)bytes);}
    std::string blob(int column,size_t maximum=std::numeric_limits<size_t>::max(),bool accounted=false){if(sqlite3_column_type(stmt,column)!=SQLITE_BLOB)throw std::runtime_error("ARK container payload type mismatch");auto bytes=sqlite3_column_bytes(stmt,column);if(bytes<0||(size_t)bytes>maximum)throw std::runtime_error("ARK container payload size exceeded");if(budget&&!accounted)budget->charge(documentCheckedProduct072((size_t)bytes,2));if(!bytes)return{};return std::string(reinterpret_cast<const char*>(sqlite3_column_blob(stmt,column)),(size_t)bytes);}
    int64_t integer(int column){if(sqlite3_column_type(stmt,column)!=SQLITE_INTEGER)throw std::runtime_error("ARK container integer type mismatch");return sqlite3_column_int64(stmt,column);}
};
int64_t scalar(Db& db,const char* sql){Statement query(db,sql);if(!query.row())throw std::runtime_error("Missing ARK container scalar");auto value=query.integer(0);if(query.row())throw std::runtime_error("Multiple ARK container scalar rows");return value;}
int schema(Db& db){
    if(scalar(db,"PRAGMA application_id")!=documentContainerApplicationId)throw std::runtime_error("SQLite file is not an ARK document");
    const auto version=scalar(db,"PRAGMA user_version");if(version!=1&&version!=2&&version!=3)throw std::runtime_error("Unsupported ARK container version");
    if(scalar(db,"PRAGMA page_size")!=4096)throw std::runtime_error("Unsupported ARK container page size");
    Statement mode(db,"PRAGMA journal_mode");if(!mode.row()||mode.text(0)!="delete")throw std::runtime_error("Unsupported ARK container journal mode");
    std::map<std::string,std::string> expected{{"ark_meta",version==3?capacityMetaSchema:legacyMetaSchema},{"ark_payload",payloadSchema},{"ark_slots",slotsSchema}};
    if(version>=2)expected.emplace("ark_derived",derivedSchema);
    Statement query(db,"SELECT type,name,tbl_name,sql FROM sqlite_schema ORDER BY name");size_t count=0;
    while(query.row()){auto name=query.text(1);auto found=expected.find(name);if(query.text(0)!="table"||query.text(2)!=name||found==expected.end()||query.text(3)!=found->second)throw std::runtime_error("Unsupported ARK container schema");++count;}
    if(count!=expected.size())throw std::runtime_error("Incomplete ARK container schema");
    Statement check(db,"PRAGMA quick_check(1)");if(!check.row()||check.text(0)!="ok"||check.row())throw std::runtime_error("Corrupt ARK container pages");
    Statement foreign(db,"PRAGMA foreign_key_check");if(foreign.row())throw std::runtime_error("Broken ARK container payload references");
    return (int)version;
}
void init(Db& db){db.exec("PRAGMA page_size=4096");db.exec("PRAGMA journal_mode=DELETE");db.exec("PRAGMA synchronous=FULL");db.exec("PRAGMA max_page_count=4294967294");db.exec("PRAGMA application_id=1095912241");db.exec("PRAGMA user_version=1");db.exec(legacyMetaSchema);db.exec(payloadSchema);db.exec(slotsSchema);}
const V& required(const V& value,const char* key){auto p=value.find(key);if(!p)throw std::runtime_error(std::string("Container manifest missing ")+key);return*p;}
std::string text(const V& value,const char* key){const auto& v=required(value,key);if(v.t!=V::Str)throw std::runtime_error("Invalid container manifest text");return v.str;}
struct Package {
    Payloads payloads;Slots slots;size_t logicalBytes=0;std::string rootHash;bool legacyCompatible=true;
    V slot(const V& value,const std::string& path){auto hash=addPayload(payloads,0,encode(value,&payloads));if(!slots.emplace(path,hash).second)throw std::runtime_error("Duplicate ARK container slot");V d=V::makeObj();d.set("k",V::makeStr("v"));d.set("p",V::makeStr(path));return d;}
    V split(const V& value,const std::string& path,unsigned depth,const std::string& key={}){
        bool object=value.t==V::Obj&&(depth==0||(depth==1&&key=="iteration_history")||(depth==2&&key=="blobs"));
        if(object){V d=V::makeObj(),entries=V::makeArr();d.set("k",V::makeStr("o"));std::vector<const std::pair<std::string,V>*> ordered;for(const auto& entry:value.obj)ordered.push_back(&entry);std::sort(ordered.begin(),ordered.end(),[](auto a,auto b){return a->first<b->first;});for(const auto* item:ordered){V entry=V::makeArr();entry.arr.push_back(V::makeStr(item->first));entry.arr.push_back(split(item->second,path+"/"+item->first,depth+1,item->first));entries.arr.push_back(std::move(entry));}d.set("e",std::move(entries));return d;}
        if(value.t==V::Arr&&(depth==1||depth==2)){V d=V::makeObj(),entries=V::makeArr();d.set("k",V::makeStr("a"));for(size_t i=0;i<value.arr.size();++i)entries.arr.push_back(slot(value.arr[i],path+"/"+std::to_string(i)));d.set("e",std::move(entries));return d;}
        return slot(value,path);
    }
    explicit Package(const V& root){
        if(root.t!=V::Obj)throw std::runtime_error("ARK document root must be an object");logicalBytes=jsonSize(root);if(logicalBytes>(size_t)std::numeric_limits<int64_t>::max())throw std::runtime_error("Document logical size exceeds the SQLite integer range");size_t items=0;const auto encoded=encode(root,nullptr,nullptr,&items);rootHash=containerDigest(encoded);legacyCompatible=logicalBytes<=legacyLogicalBytes&&encoded.size()<=legacyValueBytes&&items<=legacyItems;
        // Capacity signalling for older readers, never a new edit quota. The
        // journal and every id/hash are written unchanged in the v3 schema.
        if(const auto* history=root.find("iteration_history")){
            const auto* nodes=history->find("nodes"),*baseline=history->find("baseline_id"),*head=history->find("head_id");
            if(history->t!=V::Obj||!nodes||nodes->t!=V::Arr||nodes->arr.size()>512||!baseline||baseline->t!=V::Str||baseline->str.empty()||!head||head->t!=V::Str)legacyCompatible=false;
            else{
                std::map<std::string,size_t> chainCost{{baseline->str,0}};std::string current=baseline->str;size_t replayOperations=0;
                for(const auto& node:nodes->arr){const auto* id=node.find("id"),*parent=node.find("parent_id"),*delta=node.find("delta");
                    if(!id||id->t!=V::Str||id->str.empty()||chainCost.count(id->str)||!parent||parent->t!=V::Str||!delta||delta->t!=V::Arr){legacyCompatible=false;break;}
                    const auto prior=chainCost.find(parent->str);if(prior==chainCost.end()){legacyCompatible=false;break;}const size_t count=delta->arr.size();
                    if(count>65536||count>131072-prior->second){legacyCompatible=false;break;}
                    if(parent->str!=current){if(prior->second>131072-replayOperations){legacyCompatible=false;break;}replayOperations+=prior->second;}
                    if(count>131072-replayOperations){legacyCompatible=false;break;}replayOperations+=count;chainCost.emplace(id->str,prior->second+count);current=id->str;
                }
                if(!chainCost.count(head->str))legacyCompatible=false;
            }
        }
        V manifest=split(root,"r",0);slots.emplace("/",addPayload(payloads,0,encode(manifest)));size_t total=0;for(const auto& item:payloads){bounded(total,item.second.bytes.size(),std::numeric_limits<size_t>::max(),"Container payload size overflow");total+=item.second.bytes.size();if(item.second.bytes.size()>legacyValueBytes)legacyCompatible=false;}legacyCompatible=legacyCompatible&&total<=legacyValueBytes&&slots.size()<=legacyRows&&payloads.size()<=legacyRows*2;for(const auto& slot:slots)if(slot.first.size()>4096)legacyCompatible=false;
    }
};
V assemble(const V& descriptor,const Slots& slots,const Payloads& payloads,std::set<std::string>& usedSlots,std::set<std::string>& usedPayloads,DocumentReadBudget072& budget,unsigned depth,bool reserved=false){
    if(depth>=64||descriptor.t!=V::Obj||descriptor.obj.size()!=2)throw std::runtime_error("Invalid container manifest shape");
    const V& kindValue=required(descriptor,"k");if(kindValue.t!=V::Str)throw std::runtime_error("Invalid container manifest kind");const auto& kind=kindValue.str;
    if(kind=="v"){const V& pathValue=required(descriptor,"p");if(pathValue.t!=V::Str)throw std::runtime_error("Invalid container manifest slot");const auto& path=pathValue.str;auto slot=slots.find(path);if(slot==slots.end()||usedSlots.count(path))throw std::runtime_error("Missing/duplicate ARK container slot");budget.charge(sizeof(std::string)+path.size()+4*sizeof(void*));usedSlots.insert(path);auto payload=payloads.find(slot->second);if(payload==payloads.end()||payload->second.kind!=0)throw std::runtime_error("Missing typed ARK payload");if(!usedPayloads.count(payload->first)){budget.charge(sizeof(std::string)+64+4*sizeof(void*));usedPayloads.insert(payload->first);}return decode(payload->second.bytes,&payloads,&usedPayloads,&budget);}
    const auto& entries=required(descriptor,"e");if(entries.t!=V::Arr)throw std::runtime_error("Invalid container manifest entries");if(!reserved)budget.charge(sizeof(V));
    if(kind=="a"){budget.charge(documentCheckedProduct072(entries.arr.size(),sizeof(V)));V value=V::makeArr();value.arr.reserve(entries.arr.size());for(const auto& entry:entries.arr)value.arr.push_back(assemble(entry,slots,payloads,usedSlots,usedPayloads,budget,depth+1,true));return value;}
    if(kind=="o"){budget.charge(documentCheckedProduct072(entries.arr.size(),sizeof(std::pair<std::string,V>)));V value=V::makeObj();value.obj.reserve(entries.arr.size());for(size_t i=0;i<entries.arr.size();++i){const auto& entry=entries.arr[i];if(entry.t!=V::Arr||entry.arr.size()!=2||entry.arr[0].t!=V::Str||(i&&entry.arr[0].str<=value.obj.back().first))throw std::runtime_error("Invalid container manifest object key");budget.charge(entry.arr[0].str.size());value.obj.emplace_back(entry.arr[0].str,assemble(entry.arr[1],slots,payloads,usedSlots,usedPayloads,budget,depth+1,true));}return value;}
    throw std::runtime_error("Unknown ARK container manifest kind");
}
size_t physicalSize(const std::filesystem::path& path){std::error_code error;auto size=std::filesystem::file_size(path,error);if(error||size>std::numeric_limits<size_t>::max())throw std::runtime_error("Cannot represent ARK container physical size");if(size==0||size%4096!=0)throw std::runtime_error("Truncated ARK container pages");return(size_t)size;}
void checkDerived(const DocumentDerivedPayloads* derived){
    if(!derived)return;size_t total=0;std::set<std::string> keys;
    if(derived->size()>maxDocumentDerivedEntries)throw std::runtime_error("ARK derived row capacity exceeded");
    for(const auto& item:*derived){if(!digest(item.key)||!keys.insert(item.key).second)throw std::runtime_error("Invalid/duplicate ARK derived key");if(item.bytes.empty()||item.bytes.size()>maxDocumentDerivedEntryBytes)throw std::runtime_error("ARK derived entry exceeds capacity");bounded(total,item.bytes.size(),maxDocumentDerivedBytes,"ARK derived section exceeds its 256 MiB decoded capacity");total+=item.bytes.size();}
}
std::string packRowKey(const std::string& manifest,size_t index){char suffix[16];std::snprintf(suffix,sizeof suffix,".%04zu",index);return derivedPackRowPrefix+manifest+suffix;}
// True when the rows are exactly the chunks of this manifest and each matches its digest.
bool packRowsIntact(Db& db,const std::vector<std::pair<std::string,std::string>>& rows,const std::string& manifest,size_t& stored){
    stored=0;if(rows.empty())return false;Statement read(db,"SELECT bytes FROM ark_derived WHERE key=?1");std::string packed;
    for(size_t i=0;i<rows.size();++i){
        if(rows[i].first!=packRowKey(manifest,i))return false;read.text(1,rows[i].first);if(!read.row())return false;
        const auto bytes=read.blob(0,maxDocumentDerivedEntryBytes);read.reset();if(containerDigest(bytes)!=rows[i].second||bytes.size()>maxDocumentDerivedStoredBytes-std::min(stored,maxDocumentDerivedStoredBytes))return false;stored+=bytes.size();packed+=bytes;
    }
    // Row hashes alone do not bind the compressed content to the manifest in
    // its row name. Reject damaged/relabelled frames before claiming cache reuse.
    try{const auto decoded=decodeDerivedPack(packed);return derivedPackManifest(decoded,decoded.size())==manifest;}catch(const std::exception&){return false;}
}
// 051A: one zstd pack replaces per-entry rows; an identical intact pack is kept.
void writeDerived(Db& db,const DocumentDerivedPayloads* derived,DocumentContainerWriteStats& stats){
    std::vector<std::pair<std::string,std::string>> rows;
    {Statement old(db,"SELECT key,hash FROM ark_derived ORDER BY key");while(old.row()){if(rows.size()>=maxDerivedRows)throw std::runtime_error("Existing ARK derived row capacity exceeded");rows.emplace_back(old.text(0),old.text(1));}}
    const bool any=derived&&!derived->empty();const auto manifest=any?derivedPackManifest(*derived,derived->size()):std::string();
    size_t kept=0;
    if(any&&packRowsIntact(db,rows,manifest,kept)){
        stats.derivedEntries=derived->size();for(const auto& item:*derived)stats.derivedDecodedBytes+=item.bytes.size();stats.derivedStoredBytes=kept;return;
    }
    // Freed pages are reused by the inserts below in the same transaction.
    db.exec("DELETE FROM ark_derived");stats.derivedRewritten=!rows.empty()||any;if(!any)return;
    DerivedPackReport packed;const auto stored=encodeDerivedPack(*derived,maxDocumentDerivedStoredBytes,&packed);
    stats.derivedEntries=packed.entries;stats.derivedDecodedBytes=packed.decodedBytes;stats.derivedStoredBytes=stored.size();stats.derivedOmittedEntries=packed.omittedEntries;stats.derivedOmittedBytes=packed.omittedBytes;
    if(stored.empty())return;
    const auto name=packed.entries==derived->size()?manifest:derivedPackManifest(*derived,packed.entries);
    Statement put(db,"INSERT INTO ark_derived(key,hash,bytes) VALUES(?1,?2,?3)");
    for(size_t offset=0,index=0;offset<stored.size();offset+=derivedPackChunkBytes,++index){
        const auto chunk=stored.substr(offset,derivedPackChunkBytes);put.text(1,packRowKey(name,index));put.text(2,containerDigest(chunk));put.blob(3,chunk);put.done();put.reset();
    }
}
void writePackage(Db& db,const Package& package,DocumentContainerWriteStats& stats,const DocumentDerivedPayloads* derived){
    db.exec("PRAGMA synchronous=FULL");db.exec("PRAGMA max_page_count=4294967294");db.exec("BEGIN IMMEDIATE");
    try{
        // Validate the exact schema under the same writer lock as publication.
        const int existingVersion=schema(db);
        const bool capacitySchema=existingVersion==3||!package.legacyCompatible;
        const bool useDerived=existingVersion>=2||capacitySchema||(derived&&!derived->empty());
        if(useDerived&&existingVersion==1)db.exec(derivedSchema);
        if(capacitySchema&&existingVersion!=3){
            db.exec("ALTER TABLE ark_meta RENAME TO ark_meta_legacy072");db.exec(capacityMetaSchema);
            db.exec("INSERT INTO ark_meta(singleton,logical_bytes,root_hash,generation) SELECT singleton,logical_bytes,root_hash,generation FROM ark_meta_legacy072");
            db.exec("DROP TABLE ark_meta_legacy072");db.exec("PRAGMA user_version=3");stats.capacitySchemaUpgraded=true;
        }else if(useDerived&&existingVersion==1)db.exec("PRAGMA user_version=2");
        stats.storageVersion=capacitySchema?3:useDerived?2:1;
        Statement meta(db,"SELECT generation FROM ark_meta WHERE singleton=1");int64_t previous=0;if(meta.row()){previous=meta.integer(0);if(previous<1||previous==std::numeric_limits<int64_t>::max()||meta.row())throw std::runtime_error("Invalid container generation");}stats.generation=(uint64_t)(previous+1);
        Statement find(db,"SELECT kind,bytes FROM ark_payload WHERE hash=?1"),insert(db,"INSERT INTO ark_payload(hash,kind,bytes) VALUES(?1,?2,?3)");
        for(const auto& item:package.payloads){find.text(1,item.first);if(find.row()){if(find.integer(0)!=item.second.kind||find.blob(1)!=item.second.bytes||find.row())throw std::runtime_error("Existing ARK payload digest/content mismatch");}else{insert.text(1,item.first);insert.integer(2,item.second.kind);insert.blob(3,item.second.bytes);insert.done();insert.reset();++stats.payloadsInserted;stats.payloadBytesInserted+=item.second.bytes.size();}find.reset();}
        Slots old;Statement current(db,"SELECT path,payload_hash FROM ark_slots");while(current.row()){old.emplace(current.text(0),current.text(1));}
        Statement upsert(db,"INSERT INTO ark_slots(path,payload_hash) VALUES(?1,?2) ON CONFLICT(path) DO UPDATE SET payload_hash=excluded.payload_hash"),remove(db,"DELETE FROM ark_slots WHERE path=?1");
        for(const auto& item:package.slots){auto found=old.find(item.first);if(found==old.end()||found->second!=item.second){upsert.text(1,item.first);upsert.text(2,item.second);upsert.done();upsert.reset();++stats.slotsChanged;}if(found!=old.end())old.erase(found);}
        for(const auto& item:old){remove.text(1,item.first);remove.done();remove.reset();++stats.slotsRemoved;}
        db.exec("DELETE FROM ark_payload WHERE hash NOT IN(SELECT payload_hash FROM ark_slots) AND kind=0");
        // Raw string payloads may be referenced inside typed chunks, so prune with
        // the exact current package census rather than only SQL foreign keys.
        Statement all(db,"SELECT hash FROM ark_payload WHERE kind=1");std::vector<std::string> obsolete;while(all.row()){auto hash=all.text(0);if(!package.payloads.count(hash))obsolete.push_back(std::move(hash));}
        Statement removePayload(db,"DELETE FROM ark_payload WHERE hash=?1");for(const auto& hash:obsolete){removePayload.text(1,hash);removePayload.done();removePayload.reset();}
        Statement update(db,"INSERT INTO ark_meta(singleton,logical_bytes,root_hash,generation) VALUES(1,?1,?2,?3) ON CONFLICT(singleton) DO UPDATE SET logical_bytes=excluded.logical_bytes,root_hash=excluded.root_hash,generation=excluded.generation");update.integer(1,(int64_t)package.logicalBytes);update.text(2,package.rootHash);update.integer(3,(int64_t)stats.generation);update.done();
        if(useDerived)writeDerived(db,derived,stats);
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
std::filesystem::path stagedSibling(const std::filesystem::path& destination){
    static std::atomic<uint64_t> sequence{0};auto tick=std::chrono::steady_clock::now().time_since_epoch().count();auto staged=destination;staged+=".ark-stage-"+std::to_string(tick)+"-"+std::to_string(++sequence);
    if(std::filesystem::exists(staged))throw std::runtime_error("Staged container already exists");return staged;
}
// Reclaim free pages inside SQLite's transaction/locking protocol. Replacing a
// VACUUM INTO snapshot after closing the source could lose another writer's save
// in that gap (or leave other processes attached to an obsolete inode on POSIX).
// VACUUM itself is atomic; failure retains the preceding committed save.
void compactCommitted(Db& db,DocumentContainerWriteStats& stats){
    try{
        const auto pages=scalar(db,"PRAGMA page_count"),free=scalar(db,"PRAGMA freelist_count"),pageSize=scalar(db,"PRAGMA page_size");
        if(free*pageSize<1024*1024||free*4<pages)return;
        stats.bytesBeforeCompaction=(size_t)pages*(size_t)pageSize;
        db.exec("VACUUM");stats.compacted=true;
    }catch(const std::exception& failure){stats.compactionSkipped=failure.what();}
}
}

std::string documentContainerDigest(const std::string& bytes){return containerDigest(bytes);}

bool documentContainerFile(const char* path){if(!path||!*path)return false;std::ifstream input(filePath(path),std::ios::binary);char header[16]{};input.read(header,16);return input.gcount()==16&&std::memcmp(header,"SQLite format 3\0",16)==0;}
bool encodeDocumentValue(const V& value,std::string& bytes,std::string* error){try{auto encoded=encode(value);bytes=std::move(encoded);return true;}catch(const std::exception& e){if(error)*error=e.what();return false;}}
bool decodeDocumentValue(const std::string& bytes,V& value,std::string* error,size_t readMemoryBudget){try{DocumentReadBudget072 budget(readMemoryBudget);auto decoded=decode(bytes,nullptr,nullptr,&budget);value=std::move(decoded);return true;}catch(const std::exception& e){if(error)*error=e.what();return false;}}
bool saveDocumentContainer(const V& root,const char* path,std::string* error,DocumentContainerWriteStats* receipt,const DocumentDerivedPayloads* derived){
    std::filesystem::path staged;
    try{
        if(!path||!*path)throw std::runtime_error("Empty document path");
        checkDerived(derived);Package package(root);auto destination=filePath(path);DocumentContainerWriteStats stats;
        if(documentContainerFile(path)){
            physicalSize(destination);
            {Db db(destination,SQLITE_OPEN_READWRITE|SQLITE_OPEN_NOMUTEX);writePackage(db,package,stats,derived);compactCommitted(db,stats);}
            stats.physicalBytes=physicalSize(destination);
        }
        else{
            std::error_code existsError;bool exists=std::filesystem::exists(destination,existsError);if(existsError)throw std::runtime_error("Cannot inspect document destination");
            if(exists){std::ifstream prior(destination,std::ios::binary);char header[16]{};prior.read(header,16);if(prior.gcount()>=6&&std::memcmp(header,"SQLite",6)==0)throw std::runtime_error("Refusing to replace an unrecognized SQLite file");}
            staged=stagedSibling(destination);
            {Db db(staged,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX);init(db);writePackage(db,package,stats,derived);}stats.physicalBytes=physicalSize(staged);
            if(!replace(staged,destination))throw std::runtime_error("Cannot replace document with staged ARK container");
            staged.clear();stats.stagedReplacement=true;
        }
        if(receipt)*receipt=stats;
        return true;
    }catch(const std::exception& e){if(!staged.empty())cleanup(staged);if(error)*error=e.what();return false;}
}
bool loadDocumentContainer(const char* path,V& root,std::string* error,DocumentDerivedPayloads* derived,std::string* derivedDiscardedReason,DocumentContainerReadStats* readStats,size_t readMemoryBudget){
    try{
        if(!path||!*path)throw std::runtime_error("Empty document path");
        if(!documentContainerFile(path))throw std::runtime_error("Not an ARK binary container");
        auto source=filePath(path);physicalSize(source);DocumentReadBudget072 budget(readMemoryBudget);budget.charge(4096);
        // Read/write permits SQLite to recover a hot rollback journal after an
        // interrupted process. Read-only files remain readable without recovery.
        std::unique_ptr<Db> connection;try{connection=std::make_unique<Db>(source,SQLITE_OPEN_READWRITE|SQLITE_OPEN_NOMUTEX,&budget);}catch(...){connection=std::make_unique<Db>(source,SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX,&budget);}
        Db& db=*connection;db.exec("PRAGMA query_only=ON");db.exec("BEGIN");const int version=schema(db);
        Statement meta(db,"SELECT logical_bytes,root_hash,generation FROM ark_meta WHERE singleton=1");if(!meta.row())throw std::runtime_error("Missing ARK document metadata");auto logical=meta.integer(0);auto rootHash=meta.text(1,64);auto generation=meta.integer(2);if(logical<0||(uint64_t)logical>std::numeric_limits<size_t>::max()||!digest(rootHash)||generation<1||meta.row())throw std::runtime_error("Invalid ARK document metadata");
        // Query lengths before asking SQLite to materialize a potentially large
        // external BLOB. Reserve host/hash/SQLite working copies before fetch.
        Payloads payloads;Statement payload(db,"SELECT hash,kind,length(bytes),typeof(bytes) FROM ark_payload"),contents(db,"SELECT bytes FROM ark_payload WHERE hash=?1");
        while(payload.row()){
            auto hash=payload.text(0,64);const auto kind=payload.integer(1),length=payload.integer(2);if(length<0||(uint64_t)length>std::numeric_limits<size_t>::max()||payload.text(3,16)!="blob"||(kind!=0&&kind!=1)||!digest(hash))throw std::runtime_error("Invalid ARK payload metadata");
            budget.charge(sizeof(Payloads::value_type)+4*sizeof(void*));budget.charge(documentCheckedProduct072((size_t)length,3));
            contents.text(1,hash);if(!contents.row())throw std::runtime_error("Missing ARK payload");auto bytes=contents.blob(0,(size_t)length,true);if(bytes.size()!=(size_t)length||contents.row())throw std::runtime_error("ARK payload length changed");contents.reset();
            if(hashPayload((int)kind,bytes)!=hash)throw std::runtime_error("ARK payload checksum mismatch");if(!payloads.emplace(std::move(hash),Payload{(int)kind,std::move(bytes)}).second)throw std::runtime_error("Duplicate ARK payload");
        }
        // Separate metadata and value scans are in one read transaction. Blob
        // lengths account for embedded NUL and UTF-16-to-UTF-8 conversion too.
        Slots slots;Statement slotLengths(db,"SELECT length(CAST(path AS BLOB)),length(CAST(payload_hash AS BLOB)) FROM ark_slots ORDER BY path"),slot(db,"SELECT path,payload_hash FROM ark_slots ORDER BY path");
        while(slotLengths.row()){
            const auto pathLength=slotLengths.integer(0),hashLength=slotLengths.integer(1);if(pathLength<0||hashLength<0||(uint64_t)pathLength>std::numeric_limits<size_t>::max()||(uint64_t)hashLength>std::numeric_limits<size_t>::max())throw std::runtime_error("Invalid ARK slot length");
            budget.charge(sizeof(Slots::value_type)+4*sizeof(void*));budget.charge(documentCheckedProduct072((size_t)pathLength,4));budget.charge(documentCheckedProduct072((size_t)hashLength,4));
            if(!slot.row())throw std::runtime_error("Missing ARK slot");auto name=slot.text(0,documentCheckedProduct072((size_t)pathLength,2),true),hash=slot.text(1,64,true);if(!digest(hash)||!payloads.count(hash)||!slots.emplace(std::move(name),std::move(hash)).second)throw std::runtime_error("Invalid ARK slot reference");
        }
        if(slot.row())throw std::runtime_error("ARK slot census changed");
        auto manifestSlot=slots.find("/");if(manifestSlot==slots.end())throw std::runtime_error("Missing ARK document manifest");auto manifestPayload=payloads.find(manifestSlot->second);if(manifestPayload==payloads.end()||manifestPayload->second.kind!=0)throw std::runtime_error("Missing typed ARK manifest");budget.charge(2*(sizeof(std::string)+64+4*sizeof(void*)));std::set<std::string> usedSlots{"/"},usedPayloads{manifestPayload->first};V manifest=decode(manifestPayload->second.bytes,nullptr,nullptr,&budget);V candidate=assemble(manifest,slots,payloads,usedSlots,usedPayloads,budget,0);
        if(candidate.t!=V::Obj||usedSlots.size()!=slots.size()||usedPayloads.size()!=payloads.size())throw std::runtime_error("Unreferenced ARK container data");
        if(jsonSize(candidate)!=(size_t)logical)throw std::runtime_error("ARK document logical size mismatch");
        auto encoded=encode(candidate,nullptr,&budget);budget.charge(encoded.size());if(containerDigest(encoded)!=rootHash)throw std::runtime_error("ARK document root checksum mismatch");
        DocumentDerivedPayloads restored;std::string discarded;DocumentContainerReadStats read;
        if(version>=2)try{
            // Rows are either per-entry results (before 051A) or the chunks of one pack.
            size_t totalDerived=0,packRows=0;std::string pack,manifest;Statement rows(db,"SELECT key,hash,bytes FROM ark_derived ORDER BY key");
            while(rows.row()){
                if(restored.size()+packRows>=maxDerivedRows)throw std::runtime_error("ARK derived row capacity exceeded");
                auto key=rows.text(0),hash=rows.text(1),bytes=rows.blob(2,maxDocumentDerivedEntryBytes);
                if(!digest(hash)||bytes.empty()||containerDigest(bytes)!=hash)throw std::runtime_error("ARK derived payload checksum/identity mismatch");
                if(key.rfind(derivedPackRowPrefix,0)==0){
                    const size_t prefix=sizeof derivedPackRowPrefix-1;const auto name=key.substr(prefix,64);
                    if(packRows==0)manifest=name;
                    if(key.size()!=prefix+69||!digest(name)||name!=manifest||key!=packRowKey(manifest,packRows))throw std::runtime_error("ARK derived pack rows are inconsistent");
                    bounded(pack.size(),bytes.size(),maxDocumentDerivedStoredBytes,"ARK derived pack exceeds its 128 MiB stored capacity");budget.charge(bytes.size());pack+=bytes;++packRows;
                }else{
                    bounded(totalDerived,bytes.size(),maxDocumentDerivedBytes,"ARK derived section exceeds its 256 MiB decoded capacity");totalDerived+=bytes.size();
                    if(!digest(key))throw std::runtime_error("ARK derived payload checksum/identity mismatch");budget.charge(2*sizeof(DocumentDerivedPayload));restored.push_back({std::move(key),std::move(bytes)});
                }
            }
            if(packRows&&!restored.empty())throw std::runtime_error("ARK derived section mixes pack and entry rows");
            if(packRows){
                const auto frameBytes=ZSTD_getFrameContentSize(pack.data(),pack.size());if(frameBytes==ZSTD_CONTENTSIZE_ERROR||frameBytes==ZSTD_CONTENTSIZE_UNKNOWN||frameBytes>std::numeric_limits<size_t>::max())throw std::runtime_error("ARK derived pack has no bounded decoded size");
                // Decode produces a body, expanded strings, entry/index vectors
                // and codec workspace. This is optional: pressure discards the
                // cache, never the strictly validated authoring above.
                budget.charge(1024*1024);budget.charge(documentCheckedProduct072((size_t)frameBytes,6));
                DerivedPackReport unpacked;restored=decodeDerivedPack(pack,&unpacked);
                if(derivedPackManifest(restored,restored.size())!=manifest)throw std::runtime_error("ARK derived pack manifest mismatch");
                read.derivedEncoding="zstd_pack";read.derivedStoredBytes=pack.size();read.derivedDecodedBytes=unpacked.decodedBytes;
            }else if(!restored.empty()){read.derivedEncoding="entries";read.derivedStoredBytes=read.derivedDecodedBytes=totalDerived;}
        }catch(const std::exception& failure){restored.clear();discarded=failure.what();read=DocumentContainerReadStats{};}
        read.storageVersion=version;read.estimatedReadBytes=budget.used;read.readMemoryBudgetBytes=budget.limit;db.exec("COMMIT");root=std::move(candidate);if(derived)*derived=std::move(restored);if(derivedDiscardedReason)*derivedDiscardedReason=std::move(discarded);if(readStats)*readStats=std::move(read);return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
} // namespace ark
