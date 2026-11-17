#include "../src/native_admission_078b.hpp"
#include "ark/core.hpp"
#include "ark/native_backend.hpp"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using V=arkjson::Value;using namespace ark;namespace fs=std::filesystem;
static int checks=0;
static fs::path workerUnderTest;
static fs::path selfExecutable;
static void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
struct Env {const char* name;std::string old;Env(const char* key,const std::string& value):name(key),old(std::getenv(key)?std::getenv(key):""){set(value);}void set(const std::string& value){
#ifdef _WIN32
_putenv_s(name,value.c_str());
#else
if(value.empty())unsetenv(name);else setenv(name,value.c_str(),1);
#endif
}~Env(){set(old);}};
template<class F>static void eventually(F predicate,const char* why){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!predicate()){if(std::chrono::steady_clock::now()>end)throw std::runtime_error(why);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
static void fifoCore(){Env cpu("ARK_CPU_THREADS","1");std::promise<void> began,go;auto gate=go.get_future().share();std::vector<int> order;
    ThreadPool::get().submit([&]{began.set_value();gate.wait();});began.get_future().wait();
    for(int i=0;i<4;++i)ThreadPool::get().submit([&,i]{order.push_back(i);});go.set_value();ThreadPool::get().waitAll();check(order==std::vector<int>({0,1,2,3}),"Generic work queue must not starve older dependency work behind later submissions");
    std::atomic<int> sum=0;ThreadPool::get().submit([&]{parallel_for(0,16,[&](i64 begin,i64 end){sum+=int(end-begin);});});ThreadPool::get().waitAll();check(sum==16,"Nested parallel work deadlocked or lost work");
}
static void admission(){NativeAdmission078B broker;std::atomic<bool> cancel=false;
    auto held=std::make_unique<NativeAdmission078B::Lease>(broker.acquire(4,4,2,{}));std::promise<void> admitted,release;auto gate=release.get_future().share();
    auto first=std::async(std::launch::async,[&]{auto lease=broker.acquire(4,4,2,{});admitted.set_value();gate.wait();});eventually([&]{return broker.stats().waiting==1;},"First waiter missing");
    auto second=std::async(std::launch::async,[&]{auto lease=broker.acquire(1,4,2,{});return lease.threads();});eventually([&]{return broker.stats().waiting==2;},"Second waiter missing");held.reset();
    check(admitted.get_future().wait_for(std::chrono::seconds(5))==std::future_status::ready,"Older full-budget request was starved");check(second.wait_for(std::chrono::milliseconds(10))==std::future_status::timeout,"Later request bypassed full-budget FIFO owner");release.set_value();first.get();check(second.get()==1,"Queued request lost budget");
    held=std::make_unique<NativeAdmission078B::Lease>(broker.acquire(4,4,2,{}));
    auto cancelled=std::async(std::launch::async,[&]{try{auto lease=broker.acquire(4,4,2,[&]{return cancel.load();});return false;}catch(const std::exception& e){return std::string(e.what()).find("NATIVE_CANCELLED")!=std::string::npos;}});
    eventually([&]{return broker.stats().waiting==1;},"Cancellation waiter missing");auto following=std::async(std::launch::async,[&]{auto lease=broker.acquire(1,4,2,{});return true;});eventually([&]{return broker.stats().waiting==2;},"Follower missing");cancel=true;check(cancelled.get(),"Queued cancellation did not exit explicitly");held.reset();check(following.get(),"Cancelled head stranded the queue");
    bool threw=false;try{auto lease=broker.acquire(1,4,2,[]{throw std::runtime_error("predicate");return false;});}catch(...){threw=true;}check(threw&&broker.stats().waiting==0,"Throwing cancellation predicate leaked queue ownership");
    std::vector<std::future<void>> work;for(int i=0;i<8;++i)work.push_back(std::async(std::launch::async,[&]{for(int j=0;j<3;++j){auto lease=broker.acquire(2,4,4,{});std::this_thread::sleep_for(std::chrono::milliseconds(2));}}));for(auto& job:work)job.get();
    const auto stats=broker.stats();check(stats.activeThreads==0&&stats.activeWorkers==0&&stats.waiting==0&&stats.peakThreads<=4&&stats.peakWorkers<=2&&stats.cancelled==1,"Concurrent callers exceeded aggregate CPU allowance or leaked permits");
}
#if defined(_WIN32) && defined(ARK_RESOURCE_SCHEDULING_NATIVE_TESTS)
static const V& field(const V& value,const char* key){const auto* result=value.find(key);if(!result)throw std::runtime_error(std::string("Missing ")+key);return *result;}
static V box(int width){V r=V::makeObj(),m=V::makeObj();m.set("op",V::makeStr("box"));m.set("width",V::makeNum(width));m.set("depth",V::makeNum(4));m.set("height",V::makeNum(5));r.set("operation",V::makeStr("model_solid"));r.set("request",std::move(m));return r;}
static bool correct(const V& result,int width){const auto& report=field(result,"report");return field(report,"brep_valid").b&&std::abs(field(report,"volume_mm3").num-width*20)<1e-8;}
static std::wstring quoted(const fs::path& path){std::wstring out=L"\"";size_t slashes=0;for(auto c:path.wstring()){if(c==L'\\'){++slashes;continue;}out.append(c==L'\"'?slashes*2+1:slashes,L'\\');out+=c;slashes=0;}out.append(slashes*2,L'\\');return out+L"\"";}
static void lateShutdown(const fs::path& root,bool cache=false){
    const auto marker=root/(cache?"late-shutdown-cache.txt":"late-shutdown.txt");std::wstring command=quoted(selfExecutable)+(cache?L" --shutdown-cache-probe ":L" --shutdown-probe ")+quoted(fs::absolute(workerUnderTest))+L" "+quoted(marker);STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION child{};
    check(CreateProcessW(selfExecutable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=0,"Cannot start shutdown regression child");CloseHandle(child.hThread);
    const auto waited=WaitForSingleObject(child.hProcess,30000);if(waited!=WAIT_OBJECT_0)TerminateProcess(child.hProcess,99);DWORD exit=99;GetExitCodeProcess(child.hProcess,&exit);CloseHandle(child.hProcess);check(waited==WAIT_OBJECT_0&&exit==0,"Late native use during ThreadPool shutdown hung or crashed");
    std::ifstream input(marker);std::string line;int rows=0;while(std::getline(input,line)){check(line=="ok","Queued shutdown work lost native geometry");++rows;}check(rows==(cache?6:4),"Shutdown failed to drain all queued native requests");
}
static int shutdownProbe(const char* worker,const char* marker,bool cache=false){
    _putenv_s("ARK_CPU_THREADS","1");_putenv_s("ARK_FEATURE_THREADS","1");_putenv_s("ARK_NATIVE_THREADS","1");_putenv_s("ARK_NATIVE_RESULT_CACHE",cache?"1":"0");_putenv_s("ARK_NATIVE_CACHE_DIRECTORY",(std::string(marker)+".cache").c_str());_putenv_s("ARK_OCCT_WORKER",worker);
    // First native use deliberately happens only after main returns, during
    // the lazy core pool's static teardown. Native ownership must outlive it.
    // Repeated cache-on requests cover producer hashing, authenticated disk
    // reads, decoded retention and an actual RAM hit during the same drain.
    for(int i=0;i<(cache?6:4);++i)ThreadPool::get().submit([i,cache,path=std::string(marker)]{std::this_thread::sleep_for(std::chrono::milliseconds(20));const int width=5+(cache?i%2:i);bool ok=false;try{ok=correct(nativeWorkerRequest(box(width)),width);if(cache&&i==5){const auto info=nativeKernelInfo();ok=ok&&info.find("native_memory_cache")&&info.find("native_body_cache")&&info.find("native_worker_pool");}}catch(...){}std::ofstream out(path,std::ios::app);out<<(ok?"ok":"failed")<<'\n';});return 0;
}
static int holdWorker(const char* result){const fs::path directory(std::getenv("ARK_TEST_NATIVE_HOLD_DIR"));{std::ofstream ready(directory/"ready");ready<<"ready";}const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(!fs::exists(directory/"release")){if(std::chrono::steady_clock::now()>deadline)return 98;std::this_thread::sleep_for(std::chrono::milliseconds(5));}std::ofstream out(result);out<<"{\"synthetic_hold_worker\":true}";return 0;}
static int inheritanceProbe(const char* worker,const char* folder,const char* self){
    Env cpu("ARK_CPU_THREADS","1"),workers("ARK_FEATURE_THREADS","1"),native("ARK_NATIVE_THREADS","1"),disk("ARK_NATIVE_RESULT_CACHE","0"),path("ARK_OCCT_WORKER",worker);const fs::path directory(folder);SECURITY_ATTRIBUTES security{sizeof security,nullptr,TRUE};
    auto sentinel=[&](const char* name){const auto file=directory/name;HANDLE handle=CreateFileW(file.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);check(handle!=INVALID_HANDLE_VALUE,"Cannot create inheritable sentinel");return std::pair{file,handle};};
    auto first=sentinel("resident-inheritance.txt");try{(void)nativeKernelInfo();}catch(...){CloseHandle(first.second);throw;}CloseHandle(first.second);check(DeleteFileW(first.first.c_str())!=0,"Resident inherited an unrelated parent file handle");
    auto second=sentinel("oneshot-inheritance.txt");Env mode("ARK_NATIVE_ONESHOT","1"),hold("ARK_TEST_NATIVE_HOLD_DIR",directory.string());path.set(fs::absolute(self).string());
    auto request=std::async(std::launch::async,[]{V r=V::makeObj();r.set("operation",V::makeStr("synthetic_hold"));return nativeWorkerRequest(r);});
    try{eventually([&]{return fs::exists(directory/"ready");},"One-shot fixture did not start");}catch(...){CloseHandle(second.second);std::ofstream release(directory/"release");release<<"release";throw;}
    CloseHandle(second.second);const bool removed=DeleteFileW(second.first.c_str())!=0;{std::ofstream release(directory/"release");release<<"release";}const auto result=request.get();check(field(result,"synthetic_hold_worker").b&&removed,"One-shot worker inherited an unrelated file handle");return 0;
}
static void inheritanceChild(const fs::path& root){
    const auto directory=root/"inheritance";fs::create_directory(directory);std::wstring command=quoted(selfExecutable)+L" --inheritance-probe "+quoted(fs::absolute(workerUnderTest))+L" "+quoted(directory);STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION child{};
    check(CreateProcessW(selfExecutable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=0,"Cannot start inherited-handle regression child");CloseHandle(child.hThread);const auto waited=WaitForSingleObject(child.hProcess,30000);if(waited!=WAIT_OBJECT_0)TerminateProcess(child.hProcess,99);DWORD exit=99;GetExitCodeProcess(child.hProcess,&exit);CloseHandle(child.hProcess);check(waited==WAIT_OBJECT_0&&exit==0,"Native inherited-handle isolation failed");
}
static int relocationProbe(const char* original,const char* relocated,const char* marker){
    const auto cache=(fs::path(marker).parent_path()/"private-cache").string();Env cpu("ARK_CPU_THREADS","4"),workers("ARK_FEATURE_THREADS","4"),native("ARK_NATIVE_THREADS","2"),disk("ARK_NATIVE_RESULT_CACHE","1"),directory("ARK_NATIVE_CACHE_DIRECTORY",cache),worker("ARK_OCCT_WORKER",original);
    const auto first=nativeWorkerRequest(box(9));check(correct(first,9),"Original producer failed");const auto starts=field(nativeKernelInfo(),"total_worker_process_starts").num;
    worker.set(relocated);const auto changed=nativeWorkerRequest(box(9));check(correct(changed,9),"Changed producer failed exact operation");
    check(field(field(first,"native_cache_provenance"),"producer_sha256").str!=field(field(changed,"native_cache_provenance"),"producer_sha256").str,"Different producer bytes retained the old identity");check(field(nativeKernelInfo(),"total_worker_process_starts").num>starts,"Changed producer reused stale resident");
    worker.set(original);workers.set("1");check(correct(nativeWorkerRequest(box(10)),10),"Original producer failed after contraction");const auto state=nativeKernelInfo();check(field(field(state,"native_worker_pool"),"resident_processes").num==1,"Pool contraction retained idle foreign producer");
    std::ofstream out(marker);out<<"ok\n";return 0;
}
static void relocatedChild(const fs::path& root,const fs::path& source){
    const auto marker=root/"relocation.txt";const auto relocated=root/source.filename();
    // Harmless PE overlay changes the producer bytes without modifying any
    // application binary. Custody pins remain intact until this child exits.
    {std::ofstream overlay(relocated,std::ios::binary|std::ios::app);overlay<<"ARK078B isolated producer identity fixture\n";}
    std::wstring command=quoted(selfExecutable)+L" --relocation-probe "+quoted(source)+L" "+quoted(relocated)+L" "+quoted(marker);STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION child{};
    check(CreateProcessW(selfExecutable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=0,"Cannot start producer regression child");CloseHandle(child.hThread);const auto waited=WaitForSingleObject(child.hProcess,30000);if(waited!=WAIT_OBJECT_0)TerminateProcess(child.hProcess,99);DWORD exit=99;GetExitCodeProcess(child.hProcess,&exit);CloseHandle(child.hProcess);check(waited==WAIT_OBJECT_0&&exit==0,"Producer child hung or rejected identity/state isolation");std::ifstream input(marker);std::string line;std::getline(input,line);check(line=="ok","Producer child did not complete checks");
}
static void residentReuse(){Env cpu("ARK_CPU_THREADS","4"),workers("ARK_FEATURE_THREADS","4"),native("ARK_NATIVE_THREADS","2"),disk("ARK_NATIVE_RESULT_CACHE","0");
    const auto initial=nativeKernelInfo();const auto before=field(initial,"total_worker_process_starts").num;
    for(int i=0;i<12;++i){auto result=std::async(std::launch::async,[i]{return nativeWorkerRequest(box(10+i));}).get();check(correct(result,10+i),"Cross-thread reuse mixed exact responses");}
    auto state=nativeKernelInfo();check(field(state,"total_worker_process_starts").num==before,"Sequential short-lived caller threads created one native process each");
    std::vector<std::future<V>> jobs;for(int i=0;i<8;++i)jobs.push_back(std::async(std::launch::async,[i]{return nativeWorkerRequest(box(40+i));}));for(int i=0;i<8;++i)check(correct(jobs[i].get(),40+i),"Concurrent native leases mixed state/responses");
    state=nativeKernelInfo();const auto& pool=field(state,"native_worker_pool"),&budget=field(state,"native_admission");check(field(pool,"resident_processes").num<=2&&field(pool,"peak_resident_processes").num<=2&&field(budget,"peak_threads").num<=4,"Native resident pool exceeded actual request/CPU concurrency");
    bool rejected=false;try{nativeWorkerRequest(box(0));}catch(...){rejected=true;}check(rejected&&correct(nativeWorkerRequest(box(3)),3),"Failed worker poisoned a later lease");
    const auto beforeIdle=field(nativeKernelInfo(),"total_worker_process_starts").num;std::this_thread::sleep_for(std::chrono::milliseconds(20100));state=nativeKernelInfo();check(field(state,"total_worker_process_starts").num>beforeIdle,"Idle resident was reused at the native expiry boundary instead of proactively recycled");
    // A changed executable path must not reuse the old process even when all
    // producer bytes are identical. This also exercises private fixture cleanup.
    const auto root=fs::temp_directory_path()/("ark-native-pool-078b-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directory(root);
    struct Cleanup {fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}} cleanup{root};
    lateShutdown(root);lateShutdown(root,true);inheritanceChild(root);
    check(!workerUnderTest.empty(),"Pass the tested OCCT worker path as the first argument");
    const auto source=fs::absolute(workerUnderTest);
    for(const auto& file:fs::directory_iterator(source.parent_path()))if(file.path()==source||file.path().extension()==".dll")fs::copy_file(file.path(),root/file.path().filename());
    relocatedChild(root,source);
    std::error_code removed;fs::remove_all(root,removed);check(!removed&&!fs::exists(root),"Evicted worker kept its private executable/DLL fixture locked");
}
#endif
int main(int argc,char** argv){try{
#if defined(_WIN32) && defined(ARK_RESOURCE_SCHEDULING_NATIVE_TESTS)
if(argc==4&&std::string(argv[1])=="--shutdown-probe")return shutdownProbe(argv[2],argv[3]);
if(argc==4&&std::string(argv[1])=="--shutdown-cache-probe")return shutdownProbe(argv[2],argv[3],true);
if(argc==4&&std::string(argv[1])=="--inheritance-probe")return inheritanceProbe(argv[2],argv[3],argv[0]);
if(argc==5&&std::string(argv[1])=="--relocation-probe")return relocationProbe(argv[2],argv[3],argv[4]);
if(argc==3&&std::getenv("ARK_TEST_NATIVE_HOLD_DIR"))return holdWorker(argv[2]);
#endif
selfExecutable=fs::absolute(argv[0]);if(argc>1)workerUnderTest=argv[1];fifoCore();admission();
#if defined(_WIN32) && defined(ARK_RESOURCE_SCHEDULING_NATIVE_TESTS)
residentReuse();
#endif
std::cout<<checks<<" shared scheduling/CPU cancellation/worker reuse checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
