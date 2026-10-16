#include "ark/agent.hpp"
#include "ark/document_container.hpp"
#include "ark/native_backend.hpp"
#include "ark/serialize.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs=std::filesystem;
using V=arkjson::Value;
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
const V& field(const V& value,const char* key){const auto* found=value.find(key);if(!found)throw std::runtime_error(std::string("Missing field: ")+key);return *found;}
V json(const std::string& text){V value;check(arkjson::parse(text,value),"Public synthetic fixture JSON parses");return value;}
std::string utf8(const fs::path& path){auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
std::string read(const fs::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
struct Workspace {
    fs::path path;
    Workspace(){for(int i=0;i<100;++i){auto candidate=fs::temp_directory_path()/("ark-persisted-geometry-031b-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(i));if(fs::create_directory(candidate)){path=fs::absolute(candidate);return;}}throw std::runtime_error("Cannot create synthetic test workspace");}
    ~Workspace(){std::error_code error;fs::remove_all(path,error);}
};
V accepted(const V& response){check(response.find("isError")&&!response.find("isError")->b,response.dump());return field(response,"structuredContent");}
void healthy(const ark::Document& document){check(document.parameterError.empty(),document.parameterError);for(const auto& result:document.lastResults)check(result.ok,result.error);check(document.evalBodies.size()==document.features.size(),"Synthetic active features must retain every evaluated body");}

void authoringAndMalformedDerived(){
    Workspace workspace;ark::Document source;ark::Feature box;box.type=ark::FeatureType::Box;source.addFeature(box);source.recompute();healthy(source);
    const auto canonical=ark::documentToJson(source);
    source.derivedGeometryStatus=json(R"({"status":"staged_for_native_validation","entries_staged":1})");
    check(ark::documentToJson(source)==canonical,"Transport status must never enter authoring or history JSON");
    check(source.authoringCopy().derivedGeometryStatus.isNull(),"Authoring copies must not inherit derived-result provenance");
    const auto text=workspace.path/"explicit.arkjson";check(ark::saveDocument(source,utf8(text).c_str()),"Explicit authoring JSON save succeeds");
    check(read(text)==canonical&&!ark::documentContainerFile(utf8(text).c_str()),"Explicit JSON export remains byte-identical authoring");
    ark::Document textLoaded;std::string error;check(ark::loadDocument(utf8(text).c_str(),textLoaded,&error),error);check(textLoaded.derivedGeometryStatus.isNull(),"Text authoring has no staged geometry receipt");
    ark::DocumentDerivedPayloads malformed{{std::string(64,'a'),"not a native result envelope"}};
    auto broken=workspace.path/"malformed-derived.ark";check(ark::saveDocumentContainer(json(canonical),utf8(broken).c_str(),&error,nullptr,&malformed),error);
    ark::Document loaded;check(ark::loadDocument(utf8(broken).c_str(),loaded,&error),error);
    check(ark::documentToJson(loaded)==canonical,"Malformed optional geometry must preserve valid authoring/history");
    check(field(loaded.derivedGeometryStatus,"status").str=="discarded","Malformed native envelope must expose an explicit discarded receipt");
    loaded.recompute();healthy(loaded);
    ark::AgentSession session;session.suppressFailureCapture();V args=V::makeObj();args.set("path",V::makeStr(utf8(broken)));const auto report=accepted(session.call("doc_load",args));
    check(field(field(report,"derived_geometry"),"status").str=="discarded","doc_load must expose discarded geometry without rejecting valid authoring");
    check(field(accepted(session.call("doc_info",V::makeObj())),"derived_geometry").dump()==loaded.derivedGeometryStatus.dump(),"doc_info must retain the transport receipt");
    const auto stable=ark::documentToJson(loaded);const auto receipt=loaded.derivedGeometryStatus.dump();auto invalid=json(canonical);invalid.set("features",V::makeStr("invalid authoring"));
    const auto invalidPath=workspace.path/"invalid-authoring.ark";check(ark::saveDocumentContainer(invalid,utf8(invalidPath).c_str(),&error,nullptr,&malformed),error);
    check(!ark::loadDocument(utf8(invalidPath).c_str(),loaded,&error)&&ark::documentToJson(loaded)==stable&&loaded.derivedGeometryStatus.dump()==receipt,"Invalid authoring load must reject atomically before staging derived results");
}

#if defined(ARK_CONTRACTS_NATIVE_TESTS) && defined(_WIN32)
struct Environment {
    std::string name,previous;bool existed;
    Environment(const char* key,const std::string& value):name(key){const auto* old=std::getenv(key);existed=old!=nullptr;if(old)previous=old;set(value);}
    void set(const std::string& value){
#ifdef _WIN32
        check(_putenv_s(name.c_str(),value.c_str())==0,"Cannot set synthetic native environment");
#else
        check((value.empty()?unsetenv(name.c_str()):setenv(name.c_str(),value.c_str(),1))==0,"Cannot set synthetic native environment");
#endif
    }
    ~Environment(){
#ifdef _WIN32
        _putenv_s(name.c_str(),existed?previous.c_str():"");
#else
        if(existed)setenv(name.c_str(),previous.c_str(),1);else unsetenv(name.c_str());
#endif
    }
};
double restores(){return field(ark::nativeKernelInfo(),"native_document_restores").num;}
void volume(const ark::Document& document,double expected){healthy(document);const auto& result=document.evalBodies.front().nativeData;const auto& report=field(result,"report");check(field(report,"brep_valid").b,"Restored exact body must pass kernel validation");check(std::abs(field(report,"volume_mm3").num-expected)<1e-8,"Restored exact volume must match the current recipe");check(!field(result,"brep_base64").str.empty(),"Restored body retains production B-rep");}
void nativeRoundtrip(){
    Workspace workspace;const auto firstCache=workspace.path/"first-cache",secondCache=workspace.path/"empty-load-cache",thirdCache=workspace.path/"empty-edit-cache";fs::create_directory(firstCache);fs::create_directory(secondCache);fs::create_directory(thirdCache);
    Environment cache("ARK_NATIVE_CACHE_DIRECTORY",utf8(firstCache)),enabled("ARK_NATIVE_RESULT_CACHE","1"),cpu("ARK_CPU_THREADS","1"),workers("ARK_FEATURE_THREADS","1"),threads("ARK_NATIVE_THREADS","1");
    ark::AgentSession source;source.suppressFailureCapture();accepted(source.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"Width","expr":"3"},{"op":"feature.add","type":"native_solid","name":"Synthetic exact stock","native":{"op":"box"},"p":{"width":"Width","depth":4,"height":5}}]})")));volume(source.document,60);
    const auto canonical=ark::documentToJson(source.document);const auto topology=field(field(source.document.evalBodies.front().nativeData,"topology"),"brep_sha256").str;
    size_t evicted=0;for(const auto& item:fs::directory_iterator(firstCache))if(item.is_regular_file()&&item.path().extension()==".arknative"){check(fs::remove(item.path()),"Synthetic local cache eviction succeeds");++evicted;}
    check(evicted>0&&fs::is_empty(firstCache),"Eviction-before-save must remove the original isolated native cache entry");
    const auto native=workspace.path/"evaluated.ark";check(ark::saveDocument(source.document,utf8(native).c_str()),"Saving exact evaluated document succeeds");
    V root;ark::DocumentDerivedPayloads embedded;std::string error,discarded;check(ark::loadDocumentContainer(utf8(native).c_str(),root,&error,&embedded,&discarded),error);check(discarded.empty()&&!embedded.empty(),"Binary document actually embeds native result bytes");
    const size_t payloadOffset=std::string("ARK-NATIVE-CACHE1\n").size()+130;check(embedded.front().bytes.size()>payloadOffset,"Saved native result retains its bounded envelope");
    const auto savedResult=json(embedded.front().bytes.substr(payloadOffset));check(!field(savedResult,"brep_base64").str.empty()&&!savedResult.find("mesh"),"Embedded native result must retain exact B-rep without duplicate display tessellation");
    check(root.dump()==json(canonical).dump(),"Evaluated geometry must remain outside the authored root and history");
    cache.set(utf8(secondCache));check(fs::is_empty(secondCache),"Reload comparison starts with an empty isolated native cache");const double before=restores();
    ark::Document loaded;check(ark::loadDocument(utf8(native).c_str(),loaded,&error),error);check(field(loaded.derivedGeometryStatus,"entries_staged").num>0,"Loading stages embedded native geometry for exact request validation");
    check(field(loaded.derivedGeometryStatus,"validation").str=="on_matching_native_request","Transport receipt must not claim completed geometry validation");
    loaded.recompute();volume(loaded,60);
    if(restores()<=before){V diagnostic=ark::nativeKernelInfo();diagnostic.set("stage",loaded.derivedGeometryStatus);diagnostic.set("source_key",field(source.document.evalBodies.front().nativeData,"native_cache_key"));diagnostic.set("embedded_key",V::makeStr(embedded.front().key));if(const auto* key=loaded.evalBodies.front().nativeData.find("native_cache_key"))diagnostic.set("loaded_key",*key);throw std::runtime_error("Fresh isolated cache must reuse a worker-validated embedded native result: "+diagnostic.dump());}
    check(loaded.evalBodies.front().baked&&loaded.evalBodies.front().baked->triCount()>0,"Native snapshot validation must regenerate an actual display mesh");
    check(field(field(loaded.evalBodies.front().nativeData,"topology"),"brep_sha256").str==topology,"Validated reload must retain exact B-rep topology custody");
    check(ark::documentToJson(loaded)==canonical,"Native validation/reuse must preserve complete authoring and journal JSON");
    cache.set(utf8(thirdCache));const double beforeEdit=restores();loaded.setParam("Width","7");loaded.recompute();volume(loaded,140);check(restores()==beforeEdit,"Changed explicit input must not consume an embedded result for a different request");
    auto damaged=embedded;check(!damaged.front().bytes.empty(),"Synthetic embedded result contains bytes");damaged.front().bytes.back()='!';
    const auto corrupt=workspace.path/"corrupt-native-result.ark";check(ark::saveDocumentContainer(root,utf8(corrupt).c_str(),&error,nullptr,&damaged),error);
    const auto fourthCache=workspace.path/"empty-corrupt-cache";fs::create_directory(fourthCache);cache.set(utf8(fourthCache));const double beforeCorrupt=restores();
    ark::Document regenerated;check(ark::loadDocument(utf8(corrupt).c_str(),regenerated,&error),error);check(field(regenerated.derivedGeometryStatus,"status").str=="discarded","Corrupt embedded native result must expose a discard receipt");
    regenerated.recompute();volume(regenerated,60);check(restores()==beforeCorrupt&&ark::documentToJson(regenerated)==canonical,"Corrupt derived payload must rebuild exact geometry from unchanged authoring/history");
    const auto text=workspace.path/"authoring.arkjson";check(ark::saveDocument(source.document,utf8(text).c_str())&&read(text)==canonical,"Native evaluated state must not enter explicit authoring JSON exports");
    ark::AgentSession inspected;inspected.suppressFailureCapture();V args=V::makeObj();args.set("path",V::makeStr(utf8(native)));const auto report=accepted(inspected.call("doc_load",args));
    check(field(field(report,"derived_geometry"),"entries_staged").num>0,"Agent doc_load exposes saved geometry staging separately from evaluation counts");
    V design=V::makeObj();design.set("detail",V::makeStr("design"));check(accepted(inspected.call("doc_info",design)).find("derived_geometry"),"Design doc_info exposes the transport receipt");
}
void nativeChainRoundtrip(){
    Workspace workspace;Environment cache("ARK_NATIVE_CACHE_DIRECTORY",utf8(workspace.path/"source-cache")),enabled("ARK_NATIVE_RESULT_CACHE","1"),cpu("ARK_CPU_THREADS","1"),workers("ARK_FEATURE_THREADS","1"),threads("ARK_NATIVE_THREADS","1");
    ark::AgentSession source;source.suppressFailureCapture();accepted(source.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"native_solid","native":{"op":"box"},"p":{"width":20,"depth":15,"height":6}},{"op":"feature.add","type":"native_solid","native":{"op":"box"},"p":{"width":2,"depth":3,"height":8}},{"op":"feature.add","type":"native_solid","inputs":[1,2],"native":{"op":"cut"}},{"op":"feature.add","type":"native_solid","inputs":[3],"native":{"op":"transform"},"p":{"tx":30,"rz":25}}]})")));
    check(source.document.evalBodies.size()==4,"Synthetic dependency chain has four exact bodies");
    const auto authored=ark::documentToJson(source.document),file= utf8(workspace.path/"chain.ark");check(ark::saveDocument(source.document,file.c_str()),"Chained native geometry saves");
    cache.set(utf8(workspace.path/"empty-chain-cache"));const double before=restores();ark::Document loaded;std::string error;check(ark::loadDocument(file.c_str(),loaded,&error),error);loaded.recompute();
    for(const auto& result:loaded.lastResults)check(result.ok,result.error);
    check(loaded.evalBodies.size()==4&&ark::documentToJson(loaded)==authored,"Chained restore preserves bodies and complete authoring");
    check(restores()==before+4,"All four dependent results must restore without request identity drift: "+ark::nativeKernelInfo().dump());
    for(size_t i=0;i<4;++i){const auto& prior=source.document.evalBodies[i].nativeData;const auto& current=loaded.evalBodies[i].nativeData;
        check(field(prior,"brep_base64").str==field(current,"brep_base64").str,"Dependent restore retains exact serialized B-rep");
        check(field(prior,"topology").dump()==field(current,"topology").dump(),"Validated topology retains request identity through a dependency chain");
    }
}
void nativeDocumentCustody(){
    Workspace workspace;Environment cache("ARK_NATIVE_CACHE_DIRECTORY",utf8(workspace.path/"source-cache")),enabled("ARK_NATIVE_RESULT_CACHE","1"),cpu("ARK_CPU_THREADS","2"),workers("ARK_FEATURE_THREADS","2"),threads("ARK_NATIVE_THREADS","1");
    ark::Document source;source.setParam("Width","3");
    for(const char* width:{"Width","7","9","11"}){ark::Feature feature;feature.type=ark::FeatureType::NativeSolid;feature.nativeSpec=json(R"({"op":"box"})");feature.p={{"width",width},{"depth","4"},{"height","5"}};source.addFeature(std::move(feature));}source.recompute();healthy(source);
    const auto binary=utf8(workspace.path/"pending.ark"),text=utf8(workspace.path/"legacy.arkjson");check(ark::saveDocument(source,binary.c_str())&&ark::saveDocument(source,text.c_str()),"Synthetic custody fixtures save");
    std::string error;ark::Document pending,legacy;check(ark::loadDocument(binary.c_str(),pending,&error),error);
    check(pending.pendingNativeGeometry&&!pending.authoringCopy().pendingNativeGeometry,"Authoring copy excludes document-owned portable context");
    check(ark::loadDocument(text.c_str(),legacy,&error)&&!legacy.pendingNativeGeometry,"Legacy load has no portable context");
    cache.set(utf8(workspace.path/"legacy-after-pending"));auto before=restores();legacy.recompute();healthy(legacy);check(restores()==before,"Loading A without evaluating cannot leak saved results into legacy B");
    cache.set(utf8(workspace.path/"pending-own-evaluation"));before=restores();pending.recompute();healthy(pending);check(restores()==before+4,"Owner restores all four independent results across evaluation threads");
    check(pending.featureWorkers==source.featureWorkers&&(pending.featureWorkers==1||pending.parallelFeatures>0),"Portable context preserves normal feature-worker scheduling");
    ark::Document invalid;check(ark::loadDocument(binary.c_str(),invalid,&error),error);invalid.setParam("Width","UndefinedParameter");
    cache.set(utf8(workspace.path/"invalid-candidate"));before=restores();invalid.recompute();check(!invalid.parameterError.empty(),"Invalid candidate fails before native evaluation");check(restores()==before,"Invalid candidate does not restore unrelated results");
    ark::Document afterFailure;check(ark::loadDocument(text.c_str(),afterFailure,&error),error);afterFailure.recompute();healthy(afterFailure);check(restores()==before,"Failed candidate evaluation cannot leak pending entries into another document");
    const auto foreign=ark::nativeWorkerRequest(json(R"({"operation":"model_solid","request":{"op":"box","width":3,"depth":4,"height":5}})"));
    check(field(field(foreign,"report"),"brep_valid").b&&restores()==before,"Evaluation scope restores prior TLS context before direct worker calls");
}
#endif
}
int main(){try{authoringAndMalformedDerived();
#if defined(ARK_CONTRACTS_NATIVE_TESTS) && defined(_WIN32)
    nativeRoundtrip();nativeChainRoundtrip();nativeDocumentCustody();
#endif
    std::cout<<"031B persisted native geometry transport, authoring isolation and atomic load passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
