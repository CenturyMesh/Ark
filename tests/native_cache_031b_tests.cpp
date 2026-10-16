#include "ark/native_backend.hpp"
#include "ark/iteration_history.hpp"
#include "ark/electronics.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
namespace fs=std::filesystem;
using V=arkjson::Value;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
const V& field(const V& value,const char* name){const auto* found=value.find(name);if(!found)throw std::runtime_error(std::string("Missing result field: ")+name);return *found;}
V box(double width){V request=V::makeObj(),model=V::makeObj();model.set("op",V::makeStr("box"));model.set("width",V::makeNum(width));model.set("depth",V::makeNum(4));model.set("height",V::makeNum(5));request.set("operation",V::makeStr("model_solid"));request.set("request",std::move(model));return request;}
void volume(const V& result,double expected){const auto& report=field(result,"report");check(field(report,"brep_valid").b,"Synthetic result must retain valid native B-rep");check(std::abs(field(report,"volume_mm3").num-expected)<1e-8,"Cache returned geometry for different explicit inputs");check(!field(result,"brep_base64").str.empty()&&!field(field(result,"topology"),"brep_sha256").str.empty(),"Cache result retains exact geometry and topology custody");}
struct Counters {double hits,misses;};
Counters counters(){const auto info=ark::nativeKernelInfo();return {field(info,"native_result_cache_hits").num,field(info,"native_result_cache_misses").num};}
std::set<fs::path> entries(const fs::path& directory){std::set<fs::path> result;if(fs::exists(directory))for(const auto& item:fs::directory_iterator(directory))if(item.path().extension()==".arknative")result.insert(item.path());return result;}
std::string read(const fs::path& path){std::ifstream stream(path,std::ios::binary);return {std::istreambuf_iterator<char>(stream),{}};}
void write(const fs::path& path,const std::string& data){std::ofstream stream(path,std::ios::binary|std::ios::trunc);stream.write(data.data(),(std::streamsize)data.size());check((bool)stream,"Synthetic cache fixture write failed");}
struct Temporary {
    fs::path directory;
    Temporary(){for(int attempt=0;attempt<100;++attempt){const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();auto path=fs::temp_directory_path()/("ark-native-cache-031b-test-"+std::to_string(stamp)+"-"+std::to_string(attempt));if(fs::create_directory(path)){directory=fs::absolute(path);return;}}throw std::runtime_error("Cannot create isolated synthetic cache directory");}
    ~Temporary(){std::error_code error;for(fs::directory_iterator it(directory,error),end;!error&&it!=end;it.increment(error)){if(it->is_regular_file(error))fs::remove(it->path(),error);}fs::remove(directory,error);}
};
#ifdef _WIN32
struct Environment {
    std::wstring name,previous;bool existed;
    Environment(const wchar_t* key,const std::wstring& value):name(key){const auto* old=_wgetenv(key);existed=old!=nullptr;if(old)previous=old;set(value);}
    void set(const std::wstring& value){if(_wputenv_s(name.c_str(),value.c_str()))throw std::runtime_error("Cannot set isolated cache environment");}
    ~Environment(){_wputenv_s(name.c_str(),existed?previous.c_str():L"");}
};
#endif
struct ThreadBudget {int previous=ark::setNativeThreadBudget(1);~ThreadBudget(){ark::setNativeThreadBudget(previous);}};
}

int main(){try{
#ifndef _WIN32
    std::cout<<"Persistent native cache remains disabled pending ELF dependency custody\n";
    return 0;
#else
    Temporary temporary;Environment directory(L"ARK_NATIVE_CACHE_DIRECTORY",temporary.directory.wstring()),enabled(L"ARK_NATIVE_RESULT_CACHE",L"1");ThreadBudget budget;
    const auto sentinel=temporary.directory/"unrelated.txt";write(sentinel,"synthetic unrelated cache file");
    const auto before=counters();const auto request=box(3);const auto first=ark::nativeWorkerRequest(request);volume(first,60);
    check(field(first,"native_cache_producer_local").b,"Test requires native worker with locally bundled dependencies");
    const auto initial=entries(temporary.directory);check(initial.size()==1,"First synthetic request publishes one isolated cache entry");
    const auto stored=read(*initial.begin());const std::string magic="ARK-NATIVE-CACHE1\n";const size_t payloadOffset=magic.size()+130;
    check(stored.size()>payloadOffset&&stored.compare(0,magic.size(),magic)==0,"Cache header retains explicit format identity");
    check(stored.substr(magic.size(),64)==initial.begin()->stem().string(),"Cache filename retains exact header key");
    check(stored.substr(magic.size()+65,64)==ark::iterationSha256(stored.substr(payloadOffset)),"Accelerated payload digest must equal portable SHA-256 for identical bytes");
    const auto firstStats=counters();check(firstStats.misses==before.misses+1&&firstStats.hits==before.hits,"First request must miss");
    const auto second=ark::nativeWorkerRequest(request);const auto secondStats=counters();
    check(first.dump()==second.dump(),"Repeated request must return byte-identical complete native result");check(secondStats.hits==firstStats.hits+1&&secondStats.misses==firstStats.misses,"Repeated request must hit without another miss");
    V reordered;check(arkjson::parse(R"({"request":{"height":5,"width":3,"depth":4,"op":"box"},"operation":"model_solid"})",reordered),"Reordered equivalent native request parses");
    const auto reorderedResult=ark::nativeWorkerRequest(reordered);const auto reorderedStats=counters();
    check(reorderedResult.dump()==first.dump()&&reorderedStats.hits==secondStats.hits+1&&reorderedStats.misses==secondStats.misses&&entries(temporary.directory)==initial,"Equivalent recursive JSON object key orders must share one exact native result identity");
    const auto changed=ark::nativeWorkerRequest(box(7));volume(changed,140);const auto changedStats=counters();
    check(changedStats.misses==reorderedStats.misses+1&&changedStats.hits==reorderedStats.hits&&entries(temporary.directory).size()==2,"Changed explicit dimension must miss and retain a separate result");
    const auto originalPath=*initial.begin();std::string corrupt=read(originalPath);check(corrupt.size()>150,"Synthetic cache entry has payload");corrupt.back()=corrupt.back()=='}'?'!':'}';write(originalPath,corrupt);
    const auto regenerated=ark::nativeWorkerRequest(request);volume(regenerated,60);const auto regeneratedStats=counters();
    check(regeneratedStats.misses==changedStats.misses+1&&regeneratedStats.hits==changedStats.hits&&read(originalPath)!=corrupt,"Corrupt payload must miss and be replaced by an actual native result");
    const auto afterRepair=ark::nativeWorkerRequest(request);check(afterRepair.dump()==regenerated.dump()&&counters().hits==regeneratedStats.hits+1,"Regenerated entry becomes reusable only after successful publication");
    const auto validEntries=entries(temporary.directory);const auto failedBefore=counters();
    for(int attempt=0;attempt<2;++attempt){bool rejected=false;try{(void)ark::nativeWorkerRequest(box(0));}catch(const std::exception&){rejected=true;}check(rejected,"Invalid synthetic geometry must reject every time");}
    const auto failedAfter=counters();check(failedAfter.hits==failedBefore.hits&&failedAfter.misses==failedBefore.misses+2&&entries(temporary.directory)==validEntries,"Failures must never create or reuse successful cache entries");
    enabled.set(L"0");const auto disabledBefore=counters();volume(ark::nativeWorkerRequest(request),60);volume(ark::nativeWorkerRequest(box(9)),180);const auto disabledAfter=counters();
    check(disabledAfter.hits==disabledBefore.hits&&disabledAfter.misses==disabledBefore.misses&&entries(temporary.directory)==validEntries,"Explicit disable must bypass both existing cache reads and new writes");
    enabled.set(L"1");ark::Feature cable;cable.type=ark::FeatureType::CableRoute;
    check(arkjson::parse(R"({"start":[0,0,0],"end":[10,0,0],"radius":0.1,"bend_radius":0.3,"obstacles":[{"name":"Clear block","min":[4,2,-1],"max":[6,3,1]}]})",cable.nativeSpec),"Synthetic cable recipe parses");
    V cableRequest=V::makeObj();cableRequest.set("operation",V::makeStr("model_electronics"));cableRequest.set("request",ark::electronicsRequest(cable,{}));
    const auto cableBefore=counters();const auto cableFirst=ark::nativeWorkerRequest(cableRequest);const auto cableCold=counters();
    check(cableCold.misses==cableBefore.misses+1&&cableCold.hits==cableBefore.hits,"New normalized cable request must miss");
    const auto cableRepeat=ark::nativeWorkerRequest(cableRequest);const auto cableWarm=counters();
    check(cableRepeat.dump()==cableFirst.dump()&&cableWarm.hits==cableCold.hits+1&&cableWarm.misses==cableCold.misses,"Repeated cable preserves complete native result on hit");
    check(field(field(cableRepeat,"electronics"),"exact_obstacle_checked").b,"Cached cable retains actual native obstacle check evidence");
    V endpoint=V::makeArr();for(double coordinate:{12.,0.,0.})endpoint.arr.push_back(V::makeNum(coordinate));cable.nativeSpec.set("end",endpoint);cableRequest.set("request",ark::electronicsRequest(cable,{}));
    const auto cableEdited=ark::nativeWorkerRequest(cableRequest);const auto cableChanged=counters();
    check(cableChanged.misses==cableWarm.misses+1&&cableChanged.hits==cableWarm.hits&&std::abs(field(field(cableEdited,"electronics"),"route_length_mm").num-12)<1e-7,"Edited explicit cable endpoint must miss and rebuild the actual route");
    check(read(sentinel)=="synthetic unrelated cache file","Cache operations must preserve unrelated files");
    V extrusion;check(arkjson::parse(R"({"operation":"model_solid","request":{"op":"extrude","depth":2,"profile":{"entities":[{"kind":"polyline","name":"outline","points":[[0,0],[3,0],[3,4],[0,4],[0,0]]}]}}})",extrusion),"Explicit normalized extrusion parses");
    const auto extrusionBefore=counters();const auto extrusionFirst=ark::nativeWorkerRequest(extrusion);volume(extrusionFirst,24);const auto extrusionCold=counters();
    check(extrusionCold.misses==extrusionBefore.misses+1,"Explicit-profile extrusion is eligible for exact request caching");
    const auto extrusionRepeat=ark::nativeWorkerRequest(extrusion);check(extrusionRepeat.dump()==extrusionFirst.dump()&&counters().hits==extrusionCold.hits+1,"Repeated extrusion preserves full native result on cache hit");
    V changedExtrusion=field(extrusion,"request");changedExtrusion.set("depth",V::makeNum(3));extrusion.set("request",changedExtrusion);const auto extrusionEdit=ark::nativeWorkerRequest(extrusion);volume(extrusionEdit,36);check(counters().misses==extrusionCold.misses+1,"Changed extrusion depth misses the saved request");
    std::cout<<"031B native cache: exact hits, explicit-input misses, corrupt regeneration, failure rejection and disabled bypass passed\n";
    return 0;
#endif
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
