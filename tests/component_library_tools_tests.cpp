#include "ark/agent.hpp"
#include "ark/component_library.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace ark;using V=arkjson::Value;namespace fs=std::filesystem;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
V parse(const char* text){V value;check(arkjson::parse(text,value),"test JSON parses");return value;}
std::string utf8(const fs::path& path){const auto bytes=path.u8string();return std::string(bytes.begin(),bytes.end());}
V accepted(const V& response){if(response.find("isError")->b)throw std::runtime_error(response.find("structuredContent")->dump());return *response.find("structuredContent");}
V call(AgentSession& session,const char* tool,V args=V::makeObj()){return accepted(session.call(tool,args));}
V rooted(const fs::path& root){V args=V::makeObj();args.set("root",V::makeStr(utf8(root)));return args;}
V metadata(const char* name){return parse((std::string("{\"name\":\"")+name+"\",\"category\":\"mechanical.fixture\",\"provenance\":\"authored exact integration fixture\",\"license\":\"test-owned\",\"standard\":\"caller-declared test description\"}").c_str());}
void write(const fs::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary|std::ios::trunc);check((bool)file,"write test file");file<<bytes;}
std::string read(const fs::path& path){std::ifstream file(path,std::ios::binary);check((bool)file,"read test file");return std::string(std::istreambuf_iterator<char>(file),{});}
void core(const fs::path& root){
    AgentSession session;const auto state=documentToJson(session.document);
    auto onboarding=call(session,"agent_onboard",parse(R"({"topic":"components"})"));check(onboarding.find("tools")&&onboarding.find("rules")&&session.revision==0,"agents discover the local-first component reuse workflow without mutation");
    auto info=call(session,"library_info",rooted(root));check(info.t==V::Obj,"SQLite library info is discoverable");
    auto search=call(session,"library_search",rooted(root));check(search.find("components")&&search.find("components")->arr.empty(),"empty indexed library search");
    auto sources=call(session,"library_sources");check(sources.find("sources")&&!sources.find("sources")->arr.empty(),"source catalog exposed without network fetch");
    V insert=rooted(root);insert.set("id",V::makeStr("sha256:missing:body:0"));check(session.call("library_insert",insert).find("isError")->b,"insertion requires revision");
    insert.set("expected_revision",V::makeNum(1));check(session.call("library_insert",insert).find("isError")->b&&documentToJson(session.document)==state&&session.revision==0,"stale insertion fails before changing CAD");
    auto malformed=rooted(root);malformed.set("sql",V::makeStr("SELECT *"));check(session.call("library_search",malformed).find("isError")->b,"tools expose no arbitrary SQL");
    bool insertTool=false,readTool=false;for(const auto& tool:AgentSession::tools().arr){if(tool.find("name")->str=="library_insert")insertTool=!tool.find("annotations")->find("readOnlyHint")->b;if(tool.find("name")->str=="library_search")readTool=tool.find("annotations")->find("readOnlyHint")->b;}check(insertTool&&readTool,"library tool mutation/read annotations");
}
#ifdef ARK_COMPONENT_LIBRARY_NATIVE_TESTS
void exact(const fs::path& directory,const fs::path& root){
    AgentSession author;call(author,"doc_apply",parse(R"({"commands":[{"op":"feature.add","type":"native_solid","name":"Exact fixture","native":{"op":"box"},"p":{"width":20,"depth":10,"height":5}}]})"));
    const auto source=directory/"fixture.step";V exportArgs=V::makeObj();exportArgs.set("id",V::makeNum(1));exportArgs.set("path",V::makeStr(utf8(source)));call(author,"export_step",exportArgs);
    AgentSession session;V ingest=rooted(root);ingest.set("path",V::makeStr(utf8(source)));ingest.set("metadata",metadata("Exact library fixture"));auto record=call(session,"library_ingest",ingest);const auto id=record.find("id")->str;check(record.find("geometry_verified")->b&&!record.find("manufacturing_qualified")->b&&record.find("metadata_authority")->str=="declared","exact library validation and metadata authority remain distinct");
    auto duplicate=call(session,"library_ingest",ingest);check(duplicate.find("id")->str==id&&duplicate.find("revision")->num==record.find("revision")->num,"identical ingestion is idempotent");
    auto query=rooted(root);query.set("text",V::makeStr("Exact library"));auto search=call(session,"library_search",query);check(search.find("components")->arr.size()==1,"ingested exact component is indexed and searchable");
    auto get=rooted(root);get.set("id",V::makeStr(id));auto compact=call(session,"library_get",get);check(!compact.find("native_data")&&compact.find("native_evidence"),"get avoids B-rep buffers and retains native evidence");
    AgentSession existing;call(existing,"doc_apply",parse(R"({"commands":[{"op":"feature.add","type":"native_solid","name":"Existing base","native":{"op":"box"},"p":{"width":50,"depth":30,"height":2}}]})"));const auto existingState=documentToJson(existing.document);V preserve=rooted(root);preserve.set("id",V::makeStr(id));preserve.set("expected_revision",V::makeNum(1));auto introduced=call(existing,"library_insert",preserve);auto assembly=call(existing,"assembly_info");check(assembly.find("instances")->arr.size()==2&&existing.document.features.size()==2&&introduced.find("instance_id")->num==2&&introduced.find("preserved_output_instances")->arr.size()==1,"first assembly insertion preserves original visible base and correct ids");call(existing,"doc_undo");check(existing.document.assembly.empty()&&documentToJson(existing.document)==existingState,"one undo restores original unassembled CAD after library insertion");
    V insert=rooted(root);insert.set("id",V::makeStr(id));insert.set("expected_revision",V::makeNum(0));insert.set("instance_name",V::makeStr("First fixture"));auto inserted=call(session,"library_insert",insert);check(session.document.features.size()==1&&session.document.assembly.size()==1&&session.revision==1&&!inserted.find("reused_feature")->b,"first insertion authors exact definition and rigid instance once");
    const auto& feature=session.document.features.front();check(featureOperation(feature)=="imported_brep"&&feature.manufacturingRoute=="purchased","source component retains explicit imported/purchased scope");auto report=call(session,"native_inspect",parse(R"({"id":1})"));check(std::abs(report.find("report")->find("volume_mm3")->num-1000)<1e-8&&report.find("report")->find("source_selection_verified")->b,"inserted exact volume independently matches original STEP");
    const auto before=documentToJson(session.document);V package;check(arkjson::parse(before,package)&&package.find("version")->num==24,"portable source component saves document version24");
    V corrupted=package,storedFeatures=*corrupted.find("features"),storedNative=*storedFeatures.arr[0].find("native");storedNative.set("component_id",V::makeStr(id+"-wrong"));storedFeatures.arr[0].set("native",storedNative);corrupted.set("features",storedFeatures);Document rejectedDocument;std::string identityError;check(!documentFromJson(corrupted.dump(),rejectedDocument,&identityError),"saved component id cannot diverge from source hash and selected bodies");
    auto stale=session.call("library_insert",insert);check(stale.find("isError")->b&&session.revision==1&&documentToJson(session.document)==before,"stale library insertion leaves CAD unchanged");
    insert.set("expected_revision",V::makeNum(1));insert.set("instance_name",V::makeStr("Second fixture"));insert.set("p",parse(R"({"tx":"20+10","rz":"90"})"));auto reused=call(session,"library_insert",insert);check(reused.find("reused_feature")->b&&session.document.features.size()==1&&session.document.assembly.size()==2&&session.revision==2,"second insertion reuses exact definition and supports placement expressions");
    std::vector<AssemblyPlacement> placements;std::string error;check(resolveAssembly(session.document,placements,error),"inserted instances resolve");bool placed=false;for(const auto& p:placements)if(p.name=="Second fixture")placed=std::abs(p.world.m[12]-30)<1e-9;check(placed,"second instance has evaluated rigid placement");
    const auto saved=directory/"library-part.ark";V path=V::makeObj();path.set("path",V::makeStr(utf8(saved)));call(session,"doc_save",path);AgentSession reopened;call(reopened,"doc_load",path);check(reopened.document.features.size()==1&&reopened.document.assembly.size()==2,"saved library definitions/instances reopen exactly");
    exportArgs.set("path",V::makeStr(utf8(directory/"reopened.step")));auto exported=call(reopened,"export_step",exportArgs);check(exported.find("brep_valid")->b&&exported.find("step_roundtrip_difference_mm3")->num<1e-6,"reopened library geometry exports with independent exact readback");
    // The library owns a snapshot; changing the original caller file does not
    // alter the saved reusable geometry or silently rebind it to new bytes.
    write(source,read(source)+"\n");exportArgs.set("path",V::makeStr(utf8(directory/"original-changed.step")));call(reopened,"export_step",exportArgs);
    ComponentLibrary database(ComponentLibraryConfig{utf8(root)});const auto owned=fs::u8path(database.resolveStep(id));const auto original=read(owned);
    // Missing library storage still permits portable document reconstruction
    // from the embedded, hash-checked original STEP and selected exact B-rep.
    const auto moved=directory/"owned-source.moved";fs::rename(owned,moved);AgentSession portable;call(portable,"doc_load",path);exportArgs.set("path",V::makeStr(utf8(directory/"portable.step")));call(portable,"export_step",exportArgs);fs::rename(moved,owned);
    write(owned,original+"\n");const auto stable=documentToJson(reopened.document);const auto stableRevision=reopened.revision;exportArgs.set("path",V::makeStr(utf8(directory/"must-not-export.step")));check(reopened.call("export_step",exportArgs).find("isError")->b&&!fs::exists(directory/"must-not-export.step"),"changed immutable library source rejects export");
    auto mutation=reopened.call("doc_apply",parse(R"({"commands":[{"op":"param.set","name":"would_commit","expr":5}]})"));check(mutation.find("isError")->b&&reopened.revision==stableRevision&&documentToJson(reopened.document)==stable,"changed source is rejected even with cached geometry and rolls back edits");write(owned,original);
    V update=rooted(root);update.set("id",V::makeStr(id));update.set("expected_revision",V::makeNum(record.find("revision")->num));auto meta=metadata("Exact library fixture");meta.set("description",V::makeStr("explicitly revised description"));update.set("metadata",meta);auto updated=call(session,"library_update",update);check(updated.find("revision")->num==record.find("revision")->num+1&&session.revision==2,"library metadata revisions are independent of CAD revisions");check(session.call("library_update",update).find("isError")->b,"stale metadata revision rejects");
    // Full multi-solid sources are explicit rather than silently selecting the
    // first electrical/mechanical subpart.
    call(author,"doc_apply",parse(R"({"commands":[{"op":"feature.add","type":"native_solid","name":"Two bodies","inputs":[1],"native":{"op":"linear_pattern"},"p":{"count":2,"dx":30,"dy":0,"dz":0,"fuse":0}}]})"));const auto multi=directory/"multi.step";exportArgs.set("id",V::makeNum(2));exportArgs.set("path",V::makeStr(utf8(multi)));call(author,"export_step",exportArgs);
    ingest.set("path",V::makeStr(utf8(multi)));ingest.set("metadata",metadata("Explicit two-body source"));check(session.call("library_ingest",ingest).find("isError")->b,"ambiguous multi-solid source rejects default selection");ingest.set("all_bodies",V::makeBool(true));auto all=call(session,"library_ingest",ingest);check(all.find("body_index")->num==-2&&all.find("id")->str.find(":body:all")!=std::string::npos,"explicit complete-source compound has a distinct immutable id");
    AgentSession multiple;insert.set("id",*all.find("id"));insert.set("expected_revision",V::makeNum(0));auto multiInsert=call(multiple,"library_insert",insert);check(multiInsert.find("feature_id")->num==1,"whole source inserts as one reusable compound definition");auto native=call(multiple,"native_inspect",parse(R"({"id":1})"));check(native.find("report")->find("solids")->num==2&&std::abs(native.find("report")->find("volume_mm3")->num-2000)<1e-8,"full multi-solid selection retains both exact bodies");
    exportArgs.set("id",V::makeNum(1));exportArgs.set("path",V::makeStr(utf8(directory/"multi-returned.step")));auto multiExport=call(multiple,"export_step",exportArgs);check(multiExport.find("solids")->num==2&&multiExport.find("step_roundtrip_difference_mm3")->num<1e-6,"multi-solid source roundtrips through exact STEP export");
}
#endif
}
int main(){try{const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();const auto candidate=fs::temp_directory_path()/("ark-library-tools-test-"+std::to_string(stamp));check(fs::create_directory(candidate),"private test directory created");const auto root=candidate/"library";core(root);
#ifdef ARK_COMPONENT_LIBRARY_NATIVE_TESTS
exact(candidate,root);
#endif
std::cout<<"component library tools and source CAD tests passed\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
