#include "ark/document_container.hpp"
#include "ark/iteration_history.hpp"
#include "ark/serialize.hpp"
#include "sqlite3.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
namespace fs=std::filesystem;
using V=arkjson::Value;
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
std::string utf8(const fs::path& path){auto value=path.u8string();return std::string(reinterpret_cast<const char*>(value.data()),value.size());}
std::string bytes(const fs::path& path){std::ifstream input(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});}
void write(const fs::path& path,const std::string& value){std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(value.data(),(std::streamsize)value.size());output.close();check((bool)output,"test file write");}
V json(const std::string& value){V result;check(arkjson::parse(value,result),"test JSON parses");return result;}
V& member(V& value,const char* key){for(auto& item:value.obj)if(item.first==key)return item.second;throw std::runtime_error(key);}
std::string canonical(const V& value){std::string result,error;check(ark::encodeDocumentValue(value,result,&error),error);return result;}
struct Workspace {
    fs::path path=fs::temp_directory_path()/("ark-container-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Workspace(){fs::create_directories(path);}
    ~Workspace(){std::error_code error;fs::remove_all(path,error);}
};
struct Sqlite {
    sqlite3* db=nullptr;
    explicit Sqlite(const fs::path& path){check(sqlite3_open(utf8(path).c_str(),&db)==SQLITE_OK,"test SQLite opens");}
    ~Sqlite(){sqlite3_close(db);}
    void exec(const char* sql){char* error=nullptr;int result=sqlite3_exec(db,sql,nullptr,nullptr,&error);std::string message=error?error:"test SQLite statement";sqlite3_free(error);check(result==SQLITE_OK,message);}
    int64_t scalar(const char* sql){sqlite3_stmt* stmt=nullptr;check(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)==SQLITE_OK,"test scalar prepares");check(sqlite3_step(stmt)==SQLITE_ROW,"test scalar row");auto n=sqlite3_column_int64(stmt,0);sqlite3_finalize(stmt);return n;}
};
V fixture(size_t sourceBytes=0){
    V root=json(R"({"format":"ark","version":1,"units":"mm","next_feature_id":1,"params":[{"name":"length","expr":"10"}],"sketches":[],"features":[]})");
    if(sourceBytes){std::string source(sourceBytes,'A');for(size_t i=0;i<source.size();++i)source[i]=(char)('A'+i%26);V items=V::makeArr();for(int i=0;i<2;++i){V feature=V::makeObj();feature.set("id",V::makeNum(i+1));feature.set("source",V::makeStr(source));items.arr.push_back(std::move(feature));}root.set("features",std::move(items));}
    return root;
}
#include "container_fault_vfs.inl"
void codecTests(){
    V root=json(R"({"z":[null,true,false,0,1,-1,9007199254740991,-9007199254740991,0.125,1e-20],"a":"Portuguese: a\u00e7o; binary: \u0000\n"})");
    root.set("negative_zero",V::makeNum(-0.0));std::string encoded=canonical(root),error;V restored;check(ark::decodeDocumentValue(encoded,restored,&error),error);check(canonical(restored)==encoded,"typed codec roundtrip retains finite numbers/strings");check(std::signbit(restored.find("negative_zero")->num),"typed codec preserves negative zero");
    std::reverse(root.obj.begin(),root.obj.end());check(canonical(root)==encoded,"codec is deterministic across object insertion order");
    V stable=V::makeStr("unchanged");for(size_t n=0;n<encoded.size();++n){V destination=stable;check(!ark::decodeDocumentValue(encoded.substr(0,n),destination,&error)&&destination.str==stable.str,"every codec truncation fails atomically");}
    check(!ark::decodeDocumentValue(encoded+"x",stable,&error),"trailing codec bytes reject");std::string unsupported=encoded;unsupported[4]=2;check(!ark::decodeDocumentValue(unsupported,stable,&error),"unknown codec version rejects");unsupported=std::string("ARKB\1",5)+char(127);check(!ark::decodeDocumentValue(unsupported,stable,&error),"unknown value tag rejects");
    unsupported=std::string("ARKB\1",5)+char(7)+std::string(10,char(255));check(!ark::decodeDocumentValue(unsupported,stable,&error),"array size overflow rejects before allocation");
    std::string output="unchanged";check(!ark::encodeDocumentValue(V::makeNum(std::numeric_limits<double>::infinity()),output,&error)&&output=="unchanged","nonfinite number encoding rejects atomically");
    V deep;for(int i=0;i<64;++i){V parent=V::makeArr();parent.arr.push_back(std::move(deep));deep=std::move(parent);}check(!ark::encodeDocumentValue(deep,output,&error),"codec nesting is bounded");
    V large=V::makeStr(std::string(17u*1024u*1024u,'x'));check(ark::encodeDocumentValue(large,output,&error),"Resident codec values are not capped at legacy16MiB");check(ark::decodeDocumentValue(output,restored,&error,32u*1024u*1024u)&&restored.str==large.str,"Large typed value roundtrip");V pressure=V::makeStr("retained");check(!ark::decodeDocumentValue(output,pressure,&error,1024)&&pressure.str=="retained"&&error.find("DOCUMENT_READ_RESOURCE_PRESSURE")!=std::string::npos,"Explicit read-memory pressure rejects before string allocation and preserves destination");
    std::string many("ARKB\1",5);many+=char(7);size_t count=1024*1024;for(size_t n=count;;){const unsigned char c=(unsigned char)(n&127);n>>=7;many+=(char)(c|(n?128:0));if(!n)break;}many.append(count,'\0');check(!ark::decodeDocumentValue(many,pressure,&error,4096)&&pressure.str=="retained","Many tiny items are charged before array reserve");
}
void containerTests(const fs::path& executable){
    Workspace workspace;auto path=workspace.path/"document.ark";std::string error;V root=fixture(128*1024),loaded;ark::DocumentContainerWriteStats first,second,changed;
    check(ark::saveDocumentContainer(root,utf8(path).c_str(),&error,&first),error);check(first.stagedReplacement&&first.generation==1&&ark::documentContainerFile(utf8(path).c_str()),"initial save publishes owned binary container");
    {Sqlite sql(path);sqlite3_stmt* query=nullptr;check(sqlite3_prepare_v2(sql.db,"SELECT hash,kind,bytes FROM ark_payload",-1,&query,nullptr)==SQLITE_OK,"Portable digest cross-check prepares");size_t rows=0;while(sqlite3_step(query)==SQLITE_ROW){const std::string hash(reinterpret_cast<const char*>(sqlite3_column_text(query,0)));std::string payload(1,(char)sqlite3_column_int(query,1));payload.append(reinterpret_cast<const char*>(sqlite3_column_blob(query,2)),(size_t)sqlite3_column_bytes(query,2));check(ark::iterationSha256(payload)==hash,"Accelerated payload digest matches portable SHA-256 byte for byte");++rows;}sqlite3_finalize(query);check(rows>1,"Digest cross-check covers typed and external string payloads");check(sqlite3_prepare_v2(sql.db,"SELECT root_hash FROM ark_meta",-1,&query,nullptr)==SQLITE_OK&&sqlite3_step(query)==SQLITE_ROW,"Root digest cross-check reads");const std::string rootHash(reinterpret_cast<const char*>(sqlite3_column_text(query,0)));sqlite3_finalize(query);check(rootHash==ark::iterationSha256(canonical(root)),"Accelerated root digest retains existing canonical container identity");}
    check(ark::loadDocumentContainer(utf8(path).c_str(),loaded,&error)&&canonical(loaded)==canonical(root),error);
    {Sqlite sql(path);check(sql.scalar("SELECT count(*) FROM ark_payload WHERE kind=1")==1,"repeated large source strings use one content-addressed payload");check(sql.scalar("PRAGMA application_id")==ark::documentContainerApplicationId,"owned SQLite application magic");}
    check(ark::saveDocumentContainer(root,utf8(path).c_str(),&error,&second),error);check(!second.stagedReplacement&&second.generation==2&&second.payloadsInserted==0&&second.slotsChanged==0,"unchanged save reuses every immutable payload and slot");
    member(root,"params").arr[0].set("expr",V::makeStr("20"));check(ark::saveDocumentContainer(root,utf8(path).c_str(),&error,&changed),error);check(changed.payloadsInserted==1&&changed.slotsChanged==1&&changed.payloadBytesInserted<1024,"one parameter edit only replaces its bounded chunk");
    check(!fs::exists(utf8(path)+"-journal")&&!fs::exists(utf8(path)+"-wal")&&!fs::exists(utf8(path)+"-shm"),"closed document is one portable file");
    auto prior=bytes(path);V next=root;member(next,"params").arr[0].set("expr",V::makeStr("30"));
    {Sqlite reader(path);reader.exec("BEGIN");check(reader.scalar("SELECT generation FROM ark_meta")==3,"reader observes current save");check(!ark::saveDocumentContainer(next,utf8(path).c_str(),&error),"commit blocked by another reader fails");reader.exec("ROLLBACK");}
    check(bytes(path)==prior&&ark::loadDocumentContainer(utf8(path).c_str(),loaded,&error)&&canonical(loaded)==canonical(root),"failed commit rolls back inserted payloads and preserves previous file");
    std::string command='"'+utf8(executable)+"\" --crash-write \""+utf8(path)+'"';
#ifdef _WIN32
    command='"'+command+'"';
#endif
    int crash=std::system(command.c_str());check(crash!=0&&fs::exists(utf8(path)+"-journal"),"test writer terminates before commit with a recovery journal");check(ark::loadDocumentContainer(utf8(path).c_str(),loaded,&error)&&canonical(loaded)==canonical(root),"hot rollback journal recovers previous document after interrupted process: "+error);
    auto corrupted=workspace.path/"checksum.ark";fs::copy_file(path,corrupted);{Sqlite sql(corrupted);sql.exec("UPDATE ark_payload SET bytes=zeroblob(length(bytes)) WHERE kind=1");}V destination=fixture();auto stable=canonical(destination);check(!ark::loadDocumentContainer(utf8(corrupted).c_str(),destination,&error)&&canonical(destination)==stable,"checksum mismatch rejects without changing destination");
    auto truncated=workspace.path/"truncated.ark";write(truncated,bytes(path).substr(0,bytes(path).size()-33));check(!ark::loadDocumentContainer(utf8(truncated).c_str(),destination,&error),"truncated SQLite pages reject");
    auto unknown=workspace.path/"foreign.ark";{Sqlite sql(unknown);sql.exec("CREATE TABLE foreign_data(value TEXT)");}auto foreignBytes=bytes(unknown);check(!ark::loadDocumentContainer(utf8(unknown).c_str(),destination,&error)&&!ark::saveDocumentContainer(root,utf8(unknown).c_str(),&error)&&bytes(unknown)==foreignBytes,"unowned SQLite file rejects on both read/write and stays intact");
    auto version=workspace.path/"future.ark";fs::copy_file(path,version);{Sqlite sql(version);sql.exec("PRAGMA user_version=4");}auto versionBytes=bytes(version);check(!ark::loadDocumentContainer(utf8(version).c_str(),destination,&error)&&!ark::saveDocumentContainer(root,utf8(version).c_str(),&error)&&bytes(version)==versionBytes,"unknown physical version rejects without replacing file");
    auto incomplete=workspace.path/"incomplete-v2.ark";fs::copy_file(path,incomplete);{Sqlite sql(incomplete);sql.exec("PRAGMA user_version=2");}auto incompleteBytes=bytes(incomplete);check(!ark::loadDocumentContainer(utf8(incomplete).c_str(),destination,&error)&&!ark::saveDocumentContainer(root,utf8(incomplete).c_str(),&error)&&bytes(incomplete)==incompleteBytes,"incomplete v2 schema rejects without replacing file");
    auto schema=workspace.path/"schema.ark";fs::copy_file(path,schema);{Sqlite sql(schema);sql.exec("CREATE TRIGGER unwanted AFTER UPDATE ON ark_meta BEGIN SELECT 1; END");}check(!ark::loadDocumentContainer(utf8(schema).c_str(),destination,&error)&&!ark::saveDocumentContainer(root,utf8(schema).c_str(),&error),"extra schema objects are never executed");
    auto oversized=workspace.path/"oversized.ark";write(oversized,"SQLite format 3\0");fs::resize_file(oversized,256u*1024u*1024u+1);check(!ark::loadDocumentContainer(utf8(oversized).c_str(),destination,&error),"Misaligned/truncated pages reject without a physical file quota");
    V huge=root;huge.set("large_source",V::makeStr(std::string(5u*1024u*1024u,'x')));prior=bytes(path);
    {Sqlite reader(path);reader.exec("BEGIN");check(reader.scalar("SELECT generation FROM ark_meta")>=1,"Migration reader retains previous save");check(!ark::saveDocumentContainer(huge,utf8(path).c_str(),&error),"Blocked capacity-schema migration rejects");reader.exec("ROLLBACK");}
    {Sqlite sql(path);check(sql.scalar("PRAGMA user_version")==1,"Failed migration rolls back physical version");}
    check(bytes(path)==prior&&ark::loadDocumentContainer(utf8(path).c_str(),loaded,&error)&&canonical(loaded)==canonical(root),"Failed schema migration preserves every previous byte and authored value");
    ark::DocumentContainerWriteStats upgraded;check(ark::saveDocumentContainer(huge,utf8(path).c_str(),&error,&upgraded)&&upgraded.storageVersion==3&&upgraded.capacitySchemaUpgraded,"Large source conditionally upgrades physical storage to3");
    check(ark::loadDocumentContainer(utf8(path).c_str(),loaded,&error)&&canonical(loaded)==canonical(huge),"v3 preserves source beyond old4MiB");
    {Sqlite sql(path);check(sql.scalar("PRAGMA user_version")==3,"v3 persisted identity");}
    check(ark::saveDocumentContainer(root,utf8(path).c_str(),&error,&upgraded)&&upgraded.storageVersion==3&&!upgraded.capacitySchemaUpgraded,"Smaller later source never silently downgrades v3");
}
void persistenceIntegration(){
    Workspace workspace;std::string error;ark::Document doc;doc.setParam("length","42");auto legacy=workspace.path/"legacy.ark",text=workspace.path/"export.arkjson",native=workspace.path/"native.ark";write(legacy,ark::documentToJson(doc));ark::Document loaded;check(ark::loadDocument(utf8(legacy).c_str(),loaded,&error)&&loaded.getParam("length")==42,"legacy .ark JSON remains readable");check(ark::saveDocument(loaded,utf8(legacy).c_str())&&ark::documentContainerFile(utf8(legacy).c_str()),"explicit save migrates legacy .ark through binary stage");check(ark::saveDocument(doc,utf8(native).c_str())&&ark::loadDocument(utf8(native).c_str(),loaded,&error)&&ark::documentToJson(doc)==ark::documentToJson(loaded),"authored document native persistence roundtrip");check(ark::saveDocument(doc,utf8(text).c_str())&&!ark::documentContainerFile(utf8(text).c_str())&&json(bytes(text)).find("format")->str=="ark","arkjson remains explicit text export");
    auto jsonExport=workspace.path/"export.JSON",renamed=workspace.path/"signature.data";
    check(ark::saveDocument(doc,utf8(jsonExport).c_str())&&!ark::documentContainerFile(utf8(jsonExport).c_str()),"JSON extension is a case-insensitive explicit text export");
    fs::copy_file(native,renamed);check(ark::loadDocument(utf8(renamed).c_str(),loaded,&error)&&loaded.getParam("length")==42,"binary load uses signature rather than filename extension");
    const auto stable=ark::documentToJson(loaded);V valid=json(stable);
    for(const char* defect:{"units","version","features"}){
        V invalid=valid;if(std::string(defect)=="units")invalid.set("units",V::makeStr("cm"));else if(std::string(defect)=="version")invalid.set("version",V::makeNum(999));else invalid.set("features",V::makeStr("invalid array"));
        auto rejected=workspace.path/(std::string("invalid-")+defect+".ark");check(ark::saveDocumentContainer(invalid,utf8(rejected).c_str(),&error),error);
        ark::Document fromText=loaded;std::string textError,binaryError;
        check(!ark::documentFromJson(invalid.dump(),fromText,&textError)&&!ark::loadDocument(utf8(rejected).c_str(),loaded,&binaryError),"Binary value load and JSON load reject the same malformed authoring");
        check(textError==binaryError&&ark::documentToJson(fromText)==stable&&ark::documentToJson(loaded)==stable,"Direct binary value path retains exact authoring rejection and atomicity");
    }
}

void scalableContainer(){
    Workspace workspace;std::string error;V root=fixture(),loaded;const auto file=workspace.path/"rows.ark";
    V rows=V::makeArr();for(int i=0;i<8300;++i)rows.arr.push_back(V::makeNum(i));root.set("retained_rows",std::move(rows));ark::DocumentContainerWriteStats written;
    check(ark::saveDocumentContainer(root,utf8(file).c_str(),&error,&written)&&written.storageVersion==3,"More than8192 resident slots remain writable");
    check(ark::loadDocumentContainer(utf8(file).c_str(),loaded,&error)&&canonical(loaded)==canonical(root),"Every slot survives large census roundtrip");
    for(int mode=0;mode<3;++mode){V historyRoot=fixture(),history=V::makeObj(),nodes=V::makeArr();const int count=mode==0?513:mode==1?1:3;for(int i=0;i<count;++i){V node=V::makeObj(),delta=V::makeArr();node.set("id",V::makeStr("unchanged-synthetic-id-"+std::to_string(i)));node.set("parent_id",V::makeStr(i?"unchanged-synthetic-id-"+std::to_string(i-1):"synthetic-baseline"));if(mode)delta.arr.resize(mode==1?65537:50000);node.set("delta",std::move(delta));nodes.arr.push_back(std::move(node));}history.set("baseline_id",V::makeStr("synthetic-baseline"));history.set("head_id",V::makeStr("unchanged-synthetic-id-"+std::to_string(count-1)));history.set("nodes",std::move(nodes));historyRoot.set("iteration_history",std::move(history));const auto historyFile=workspace.path/("history-capacity-"+std::to_string(mode)+".ark");check(historyRoot.dump().size()<4u*1024u*1024u,"History storage compatibility fixture must isolate old counts from byte size");check(ark::saveDocumentContainer(historyRoot,utf8(historyFile).c_str(),&error,&written)&&written.storageVersion==3,"Old history-count/replay limits require the new reader marker, not rejection");check(ark::loadDocumentContainer(utf8(historyFile).c_str(),loaded,&error)&&canonical(loaded)==canonical(historyRoot),"Capacity signalling never rewrites journal ids/deltas");}
    for(int count:{3,4}){V branched=fixture(),history=V::makeObj(),nodes=V::makeArr();for(int i=0;i<count;++i){V node=V::makeObj(),delta=V::makeArr();delta.arr.resize(i?1:60000);node.set("id",V::makeStr("branch-"+std::to_string(i)));node.set("parent_id",V::makeStr(i?"branch-0":"baseline"));node.set("delta",std::move(delta));nodes.arr.push_back(std::move(node));}history.set("baseline_id",V::makeStr("baseline"));history.set("head_id",V::makeStr("branch-"+std::to_string(count-1)));history.set("nodes",std::move(nodes));branched.set("iteration_history",std::move(history));const auto target=workspace.path/("branch-replay-"+std::to_string(count)+".ark");check(branched.dump().size()<4u*1024u*1024u&&60000+count-1<131072,"Branch fixture isolates repeated ancestor replay from simple delta/byte counts");check(ark::saveDocumentContainer(branched,utf8(target).c_str(),&error,&written)&&written.storageVersion==(count==3?1:3),"Legacy compatibility includes ancestor replay on each branch switch");check(ark::loadDocumentContainer(utf8(target).c_str(),loaded,&error)&&canonical(loaded)==canonical(branched),"Branch compatibility marker retains every original id and delta");}
    const auto shared=workspace.path/"shared.ark";V repeated=fixture(),refs=V::makeArr();for(int i=0;i<400;++i)refs.arr.push_back(V::makeStr(std::string(4096,'r')));repeated.set("references",std::move(refs));check(ark::saveDocumentContainer(repeated,utf8(shared).c_str(),&error),error);
    V stable=V::makeStr("unchanged");check(!ark::loadDocumentContainer(utf8(shared).c_str(),stable,&error,nullptr,nullptr,nullptr,512*1024)&&stable.str=="unchanged"&&error.find("DOCUMENT_READ_RESOURCE_PRESSURE")!=std::string::npos,"Repeated payload references consume expansion memory before allocation");
    ark::DocumentContainerReadStats read;check(ark::loadDocumentContainer(utf8(shared).c_str(),loaded,&error,nullptr,nullptr,&read,32u*1024u*1024u)&&canonical(loaded)==canonical(repeated)&&read.estimatedReadBytes>fs::file_size(shared),"Budgeted expansion retains exact shared source");
    const auto padded=workspace.path/"large-physical.ark";fs::copy_file(shared,padded);fs::resize_file(padded,260u*1024u*1024u);check(ark::loadDocumentContainer(utf8(padded).c_str(),loaded,&error)&&canonical(loaded)==canonical(repeated),"Reader uses required SQLite rows rather than allocating/rejecting the whole physical file");
    const auto v2=workspace.path/"v2-upgrade.ark";ark::DocumentDerivedPayloads derived{{std::string(64,'a'),"synthetic cache"}};check(ark::saveDocumentContainer(fixture(),utf8(v2).c_str(),&error,&written,&derived)&&written.storageVersion==2,"Small derived source stays legacyv2");
    V large=fixture();large.set("source",V::makeStr(std::string(5u*1024u*1024u,'z')));check(ark::saveDocumentContainer(large,utf8(v2).c_str(),&error,&written,&derived)&&written.storageVersion==3,"Existing v2 upgrades metadata atomically");ark::DocumentDerivedPayloads restored;check(ark::loadDocumentContainer(utf8(v2).c_str(),loaded,&error,&restored)&&canonical(loaded)==canonical(large)&&restored.size()==1&&restored[0].bytes==derived[0].bytes,"v3 preserves exact optional derived bytes alongside full authoring");
}
int crashWrite(const fs::path& path){Sqlite sql(path);sql.exec("PRAGMA cache_size=1");sql.exec("PRAGMA synchronous=FULL");sql.exec("BEGIN IMMEDIATE");sql.exec("UPDATE ark_meta SET generation=generation+10000");sql.exec("UPDATE ark_payload SET bytes=zeroblob(length(bytes))");check(sqlite3_db_cacheflush(sql.db)==SQLITE_OK,"crash test forces dirty pages to disk");std::_Exit(17);}
void atomicText(const fs::path& path,const std::string& value){auto staged=path;staged+=".tmp";write(staged,value);
#ifdef _WIN32
    check(MoveFileExW(staged.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"benchmark JSON replaces atomically");
#else
    fs::rename(staged,path);
#endif
}
template<class Action> double median(Action action){std::vector<double> times;for(int i=0;i<7;++i){auto start=std::chrono::steady_clock::now();action(i);auto stop=std::chrono::steady_clock::now();times.push_back(std::chrono::duration<double,std::milli>(stop-start).count());}std::sort(times.begin(),times.end());return times[times.size()/2];}
void benchmark(){
    Workspace workspace;V results=V::makeArr();for(size_t sourceBytes:{size_t(0),size_t(768*1024)}){V root=fixture(sourceBytes);auto native=workspace.path/(sourceBytes?"large.ark":"small.ark"),legacy=workspace.path/(sourceBytes?"large.json":"small.json");std::string error;ark::DocumentContainerWriteStats first,last;auto start=std::chrono::steady_clock::now();check(ark::saveDocumentContainer(root,utf8(native).c_str(),&error,&first),error);auto stop=std::chrono::steady_clock::now();double initial=std::chrono::duration<double,std::milli>(stop-start).count();atomicText(legacy,root.dump());V row=V::makeObj();row.set("scenario",V::makeStr(sourceBytes?"large_shared_source":"small_parameters"));row.set("logical_json_bytes",V::makeNum(root.dump().size()));row.set("container_initial_ms",V::makeNum(initial));row.set("container_incremental_ms",V::makeNum(median([&](int i){member(root,"params").arr[0].set("expr",V::makeStr(std::to_string(100+i)));check(ark::saveDocumentContainer(root,utf8(native).c_str(),&error,&last),error);})));row.set("json_save_ms",V::makeNum(median([&](int i){member(root,"params").arr[0].set("expr",V::makeStr(std::to_string(200+i)));atomicText(legacy,root.dump());})));row.set("container_load_ms",V::makeNum(median([&](int){V loaded;check(ark::loadDocumentContainer(utf8(native).c_str(),loaded,&error),error);})));row.set("json_load_ms",V::makeNum(median([&](int){V loaded;check(arkjson::parse(bytes(legacy),loaded),"benchmark JSON parses");})));row.set("physical_container_bytes",V::makeNum(fs::file_size(native)));row.set("incremental_payload_bytes",V::makeNum(last.payloadBytesInserted));row.set("incremental_payload_rows",V::makeNum(last.payloadsInserted));row.set("incremental_changed_slots",V::makeNum(last.slotsChanged));results.arr.push_back(std::move(row));}
    V report=V::makeObj();report.set("benchmark",V::makeStr("ARK container v1 storage and codec; median of 7; warm filesystem; no geometry evaluation or journal semantic validation"));report.set("json_durability",V::makeStr("existing ofstream flush plus atomic replace"));report.set("container_durability",V::makeStr("SQLite synchronous FULL rollback transaction"));report.set("results",std::move(results));std::cout<<report.dump()<<'\n';
}
}
int main(int argc,char** argv){try{if(argc==3&&std::string(argv[1])=="--crash-write")return crashWrite(fs::path(reinterpret_cast<const char8_t*>(argv[2])));if(argc==2&&std::string(argv[1])=="--benchmark"){benchmark();return 0;}codecTests();containerTests(fs::absolute(fs::path(reinterpret_cast<const char8_t*>(argv[0]))));diskFullTests();persistenceIntegration();scalableContainer();std::cout<<"document container tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
