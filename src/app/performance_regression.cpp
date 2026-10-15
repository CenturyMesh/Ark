#include "ark/agent.hpp"
#include "ark/document_container.hpp"
#include "ark/iteration_history.hpp"
#include "ark/serialize.hpp"
#include "ark/version.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace {
using V=arkjson::Value;namespace fs=std::filesystem;
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
std::string utf8(const fs::path& p){auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
fs::path path(const char* s){return fs::path(reinterpret_cast<const char8_t*>(s));}
template<class Action>double timed(Action action){auto start=std::chrono::steady_clock::now();action();return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
void recompute(ark::Document& document){for(const auto& result:document.recompute())check(result.ok,result.error);}
template<class Action>V measure(int count,Action action){std::vector<double> samples;V raw=V::makeArr();for(int i=0;i<count;++i){auto ms=timed([&]{action(i);});samples.push_back(ms);raw.arr.push_back(V::makeNum(ms));}std::sort(samples.begin(),samples.end());V value=V::makeObj();value.set("samples_ms",std::move(raw));value.set("median_ms",V::makeNum(samples[samples.size()/2]));value.set("p95_ms",V::makeNum(samples[(size_t)std::ceil(.95*samples.size())-1]));return value;}
ark::Document fixture(int count){ark::Document document;document.setParam("driver","10");for(int i=0;i<count;++i){ark::Feature f;f.type=ark::FeatureType::Box;f.name="Synthetic box "+std::to_string(i);f.p={{"dx","driver+"+std::to_string(i%7)},{"dy",std::to_string(10+i%5)},{"dz",std::to_string(10+i%3)}};document.addFeature(f);}return document;}
V opened(const fs::path& p){ark::Document document;std::string error;double ms=timed([&]{check(ark::loadDocument(utf8(p).c_str(),document,&error),error);recompute(document);});V out=V::makeObj();out.set("load_recompute_ms",V::makeNum(ms));out.set("feature_count",V::makeNum(document.features.size()));out.set("authoring_sha256",V::makeStr(ark::iterationSha256(ark::documentToJson(document))));return out;}
}
int main(int argc,char** argv){try{
    if(argc==3&&std::string(argv[1])=="--open-once"){std::cout<<opened(path(argv[2])).dump()<<'\n';return 0;}
    if(argc!=3&&argc!=5)throw std::runtime_error("Usage: ark-performance-regression --out-dir DIRECTORY [--samples 21]; or --open-once FILE");
    check(std::string(argv[1])=="--out-dir","Expected --out-dir");int samples=21;if(argc==5){check(std::string(argv[3])=="--samples","Expected --samples");samples=std::stoi(argv[4]);check(samples>=3&&samples<=101,"Samples must be 3..101");}
    fs::path directory=path(argv[2]);fs::create_directories(directory);V rows=V::makeArr();
    for(int size:{16,128,512}){
        auto document=fixture(size);auto text=ark::documentToJson(document);auto file=directory/("synthetic-"+std::to_string(size)+".ark");check(!fs::exists(file),"Benchmark refuses to replace existing fixtures; use an empty output directory");check(ark::saveDocument(document,utf8(file).c_str()),"Initial synthetic fixture save failed");
        V row=V::makeObj();row.set("feature_count",V::makeNum(size));row.set("fixture_file",V::makeStr(file.filename().string()));row.set("fixture_sha256",V::makeStr(ark::iterationSha256(text)));row.set("logical_bytes",V::makeNum(text.size()));row.set("first_process_local_open",opened(file));
        row.set("warm_open",measure(samples,[&](int){auto result=opened(file);check(result.find("feature_count")->num==size,"Open readback count differs");}));
        recompute(document);ark::AgentSession session;session.document=document.authoringCopy();recompute(session.document);
        row.set("interaction_doc_info",measure(samples,[&](int){auto result=session.call("doc_info",V::makeObj());check(!result.find("isError")->b,"doc_info interaction failed");}));
        row.set("recompute_changed_parameter",measure(samples,[&](int i){document.setParam("driver",std::to_string(11+i));recompute(document);check(document.recomputeEvaluated==(size_t)size,"Changed parameter must evaluate every fixture feature");}));
        std::string error;ark::DocumentContainerWriteStats stats;
        row.set("incremental_save",measure(samples,[&](int i){document.setParam("driver",std::to_string(100+i));V root;check(arkjson::parse(ark::documentToJson(document),root),"Fixture serializes");check(ark::saveDocumentContainer(root,utf8(file).c_str(),&error,&stats),error);check(stats.slotsChanged==1,"Incremental save changes only the driver parameter slot");}));
        row.set("last_incremental_payload_bytes",V::makeNum(stats.payloadBytesInserted));row.set("last_incremental_changed_slots",V::makeNum(stats.slotsChanged));
        document=fixture(size);check(ark::saveDocument(document,utf8(file).c_str()),"Restore equivalent fixture for fresh-process open");rows.arr.push_back(std::move(row));
    }
    V report=V::makeObj();report.set("schema",V::makeStr("ark.performance.v1"));report.set("version",V::makeStr(ARK_APP_VERSION));report.set("iteration_code",V::makeStr(ARK_ITERATION_CODE));report.set("workload",V::makeStr("synthetic-analytic-boxes-v1-seed-19020"));report.set("samples",V::makeNum(samples));report.set("interaction_scope",V::makeStr("Synchronous doc_info API roundtrip; no viewport, input dispatch, GPU or display latency."));report.set("open_scope",V::makeStr("Deserialize and full recompute; first process-local and warm filesystem. No OS page-cache eviction claimed."));report.set("results",std::move(rows));std::cout<<report.dump()<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
