#include "ark/native_backend.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif
using V=arkjson::Value;namespace fs=std::filesystem;
static size_t checks=0;
static void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
static const V& field(const V& value,const char* key){auto p=value.find(key);if(!p)throw std::runtime_error(std::string("Missing field ")+key);return *p;}
static V json(const char* text){V out;check(arkjson::parse(text,out),"fixture JSON");return out;}
static V request(const char* op,double parameter){V result=V::makeObj(),model=V::makeObj();model.set("op",V::makeStr(op));if(std::string(op)=="sphere")model.set("radius",V::makeNum(parameter));else{model.set("width",V::makeNum(parameter));model.set("depth",V::makeNum(4));model.set("height",V::makeNum(5));}result.set("operation",V::makeStr("model_solid"));result.set("request",model);return result;}
#ifdef _WIN32
struct Env {std::wstring key,prior;bool existed=false;Env(const wchar_t* name,const std::wstring& value):key(name){if(const auto* old=_wgetenv(name)){existed=true;prior=old;}set(value);}void set(const std::wstring& value){check(!_wputenv_s(key.c_str(),value.c_str()),"set isolated environment");}~Env(){_wputenv_s(key.c_str(),existed?prior.c_str():L"");}};
struct Workspace {fs::path root;Workspace(){root=fs::temp_directory_path()/("ark-native-ram-070a-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(fs::create_directory(root),"create fresh synthetic workspace");root=fs::absolute(root);}~Workspace(){std::error_code ec;fs::remove_all(root,ec);}};
struct Budget {int prior=ark::setNativeThreadBudget(1);~Budget(){ark::setNativeThreadBudget(prior);}};
static V memory(){return field(ark::nativeKernelInfo(),"native_memory_cache");}
static V processMemory(){
    V out=V::makeObj();using GetMemory=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);
    const auto fn=reinterpret_cast<GetMemory>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));PROCESS_MEMORY_COUNTERS_EX info{};info.cb=sizeof info;
    check(fn&&fn(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&info),sizeof info),"measure actual host process memory");out.set("working_set_bytes",V::makeNum((double)info.WorkingSetSize));out.set("peak_working_set_bytes",V::makeNum((double)info.PeakWorkingSetSize));out.set("private_bytes",V::makeNum((double)info.PrivateUsage));return out;
}
static std::string read(const fs::path& path){std::ifstream stream(path,std::ios::binary);return {std::istreambuf_iterator<char>(stream),{}};}
static void write(const fs::path& path,const std::string& bytes){std::ofstream stream(path,std::ios::binary|std::ios::trunc);stream.write(bytes.data(),(std::streamsize)bytes.size());check((bool)stream,"write synthetic cache fixture");}
static V benchmark(const V& native,const std::string& expected){
    V out=V::makeObj(),samples=V::makeArr();std::vector<double> ordered;
    for(int i=0;i<9;++i){const auto start=std::chrono::steady_clock::now();const auto result=ark::nativeWorkerRequest(native);const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();check(result.dump()==expected,"RAM/disk strategy returns byte-identical complete native geometry, references and reports");samples.arr.push_back(V::makeNum(ms));ordered.push_back(ms);}
    std::sort(ordered.begin(),ordered.end());out.set("samples_ms",samples);out.set("median_ms",V::makeNum(ordered[ordered.size()/2]));out.set("host_memory",processMemory());return out;
}
static void run(){
    Workspace workspace;Env directory(L"ARK_NATIVE_CACHE_DIRECTORY",workspace.root.wstring()),disk(L"ARK_NATIVE_RESULT_CACHE",L"1"),ram(L"ARK_NATIVE_MEMORY_CACHE_MAX_MB",L"0");Budget budget;
    const auto native=request("sphere",50);const auto first=ark::nativeWorkerRequest(native);const auto expected=first.dump();check(field(field(first,"report"),"brep_valid").b,"actual kernel sphere is valid");
    const auto disabled=memory();check(field(disabled,"entries").num==0&&field(disabled,"accounted_retained_bytes").num==0&&field(disabled,"capacity_bytes").num==0,"RAM disabled means no retained allocations");
    const auto baseline=benchmark(native,expected);ram.set(L"64");const auto before=memory();const auto seed=ark::nativeWorkerRequest(native);const auto warm=memory();
    check(field(warm,"entries").num==1&&field(warm,"accounted_retained_bytes").num>0&&field(warm,"accounted_retained_bytes").num<=field(warm,"capacity_bytes").num,"validated disk parse enters bounded on-demand RAM storage");
    check(field(warm,"misses").num==field(before,"misses").num+1,"first decoded lookup is an observable RAM miss");
    const auto candidate=benchmark(native,expected);const auto after=memory();check(field(after,"hits").num==field(warm,"hits").num+9,"every warm repeated call avoids exactly one JSON parse");
    V edited=ark::nativeWorkerRequest(native);edited.set("brep_base64",V::makeStr("caller edit"));check(ark::nativeWorkerRequest(native).dump()==expected,"returned values cannot mutate cache-owned native evidence");
    const auto path=workspace.root/(field(first,"native_cache_key").str+".arknative");const auto originalBytes=read(path);const auto timestamp=fs::last_write_time(path);auto broken=originalBytes;broken.back()='!';write(path,broken);fs::last_write_time(path,timestamp);const auto hitBefore=field(memory(),"hits").num;
    const auto repaired=ark::nativeWorkerRequest(native);check(field(field(repaired,"report"),"brep_valid").b&&field(memory(),"hits").num==hitBefore&&read(path)!=broken,"same-length/time tampering cannot be masked by a decoded RAM entry");
    const auto path2=workspace.root/"replacement.tmp";write(path2,broken);check(MoveFileExW(path2.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)!=0,"replace synthetic cache file");fs::last_write_time(path,timestamp);check(field(field(ark::nativeWorkerRequest(native),"report"),"brep_valid").b&&read(path)!=broken,"replaced corrupted disk bytes are revalidated before RAM reuse");
    const auto workerBefore=field(ark::nativeKernelInfo(),"native_result_cache_misses").num;check(fs::remove(path),"remove synthetic derived file");(void)ark::nativeWorkerRequest(native);check(field(ark::nativeKernelInfo(),"native_result_cache_misses").num==workerBefore+1,"a missing disk entry cannot be resurrected solely from RAM");
    ram.set(L"1");(void)memory();const auto oversizedBefore=field(memory(),"oversized_skipped").num;(void)ark::nativeWorkerRequest(native);const auto smallUsage=memory();check(field(smallUsage,"accounted_retained_bytes").num<=1024*1024,"lowering cap promptly evicts retained allocations");check(field(smallUsage,"oversized_skipped").num>oversizedBefore,"large decoded entries bypass a smaller cap without losing exact results");
    ram.set(L"0");(void)memory();ram.set(L"1");double evictions=field(memory(),"evictions").num;
    for(int i=0;i<40;++i){const auto box=request("box",10+i);const auto cold=ark::nativeWorkerRequest(box);const auto hot=ark::nativeWorkerRequest(box);check(cold.dump()==hot.dump(),"bounded LRU preserves each exact box result");const auto usage=memory();check(field(usage,"accounted_retained_bytes").num<=field(usage,"capacity_bytes").num&&field(usage,"entries").num<=field(usage,"entry_limit").num,"retained allocation/count caps hold after every insertion");}
    const auto bounded=memory();check(field(bounded,"evictions").num>evictions,"capacity pressure evicts older parsed entries");
    const auto missBefore=field(bounded,"misses").num;const auto old=ark::nativeWorkerRequest(request("box",10));check(std::abs(field(field(old,"report"),"volume_mm3").num-200)<1e-8&&field(memory(),"misses").num==missBefore+1,"evicted entry reparses validated disk evidence correctly");
    const auto cacheHitsBefore=field(ark::nativeKernelInfo(),"native_result_cache_hits").num;const auto entriesBefore=field(memory(),"entries").num;for(int i=0;i<2;++i){bool failed=false;try{(void)ark::nativeWorkerRequest(request("box",0));}catch(const std::exception&){failed=true;}check(failed,"failed native operation always rejects");}check(field(ark::nativeKernelInfo(),"native_result_cache_hits").num==cacheHitsBefore&&field(memory(),"entries").num==entriesBefore,"failed native requests never populate RAM results");
    disk.set(L"0");const auto bypass=memory();check(field(bypass,"capacity_bytes").num==0&&field(bypass,"entries").num==0,"global native cache disable releases the RAM tier too");
    V report=V::makeObj();report.set("fixture",V::makeStr("native-sphere-radius50;9warm-identical-requests-per-strategy"));report.set("disk_verified_parse_each_hit",baseline);report.set("disk_verified_decoded_ram",candidate);report.set("retained_after_warm",after);report.set("bounded_after_pressure",bounded);report.set("memory_scope",V::makeStr("host process only; peak is cumulative high-water, excludes isolated OCCT worker processes; cache accounting is retained values, not total process RAM"));std::cout<<"NATIVE_RAM_CACHE_070A "<<report.dump()<<'\n';
}
#endif
int main(){try{
#ifdef _WIN32
run();
#endif
std::cout<<checks<<" native RAM cache checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
