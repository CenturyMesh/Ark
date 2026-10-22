#include "ark/agent.hpp"
#include "ark/native_backend.hpp"
#include <chrono>
#include <map>
#include <mutex>
#include <thread>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ark;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Setting {
    const char* name;std::string previous;
    Setting(const char* key,const char* value):name(key),previous(std::getenv(key)?std::getenv(key):""){set(value);}
    void set(const char* value){
#ifdef _WIN32
        _putenv_s(name,value);
#else
        if(*value)setenv(name,value,1);else unsetenv(name);
#endif
    }
    ~Setting(){set(previous.c_str());}
};
Document fixture(){
    Document d;Feature base;base.type=FeatureType::Box;base.p={{"dx","2"},{"dy","3"},{"dz","4"}};d.addFeature(base);
    for(int i=0;i<12;++i){Feature owner;owner.type=FeatureType::Transform;owner.inputs={1};owner.p={{"tx",std::to_string(i)}};d.addFeature(owner);Feature independent=base;independent.p["dx"]=std::to_string(i+3);d.addFeature(independent);}
    return d;
}
void healthy(const Document& d){check(d.evalBodies.size()==d.features.size(),"Every feature must retain its ordered output");for(const auto& r:d.lastResults)check(r.ok,r.error.c_str());}
#ifdef ARK_CONTRACTS_NATIVE_TESTS
void nativePoolCancellation(){
    Setting cpu("ARK_CPU_THREADS","2"),workers("ARK_FEATURE_THREADS","2"),native("ARK_NATIVE_THREADS","1"),cache("ARK_NATIVE_RESULT_CACHE","0");
    Document document;for(int i=0;i<4;++i){Feature feature;feature.type=FeatureType::NativeSolid;feature.name="Cancellation probe "+std::to_string(i);feature.nativeSpec=arkjson::Value::makeObj();feature.nativeSpec.set("op",arkjson::Value::makeStr("box"));feature.p={{"width",std::to_string(11+i)},{"depth","17"},{"height","23"}};document.addFeature(feature);}
    const auto owner=std::this_thread::get_id();std::mutex mutex;std::map<std::thread::id,unsigned> polls;bool wrongLimit=false;
    const auto before=nativeKernelInfo().find("total_worker_process_starts")->num;const auto started=std::chrono::steady_clock::now();
    {
        NativeRequestControlScope control([&]{
            std::lock_guard lock(mutex);const auto thread=std::this_thread::get_id();
            if(thread==owner)return false;
            wrongLimit|=nativeRequestLimitMs()!=120000;
            // The first poll is before native dispatch; the second is inside
            // the real process/event wait after the isolated worker was started.
            return ++polls[thread]>=2;
        },120000);
        document.recompute();
    }
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    const auto after=nativeKernelInfo().find("total_worker_process_starts")->num;
    check(document.parallelFeatures>0&&!polls.empty()&&!wrongLimit,"native pool tasks inherit request cancellation and allowance");
    check(after>before,"cancellation exercised real isolated native worker processes");
    check(elapsed<5000,"pooled native cancellation must not wait for its 120-second watchdog");
    for(const auto& result:document.lastResults)check(!result.ok&&result.error.find("NATIVE_CANCELLED")!=std::string::npos,"each cancelled native job remains an explicit failure");
    check(nativeRequestLimitMs()==30000,"owner request allowance restored after scope");
    const auto savedPolls=polls;document.recompute();healthy(document);
    check(polls==savedPolls&&document.parallelFeatures>0,"retry on reused pool threads has no stale cancellation policy");
    std::cout<<"real pooled native cancellation "<<elapsed<<" ms; processes started="<<after-before<<"; retry passed\n";
}
#endif
void run(){
    Setting cpu("ARK_CPU_THREADS","2"),workers("ARK_FEATURE_THREADS","1"),native("ARK_NATIVE_THREADS","1");
    Document serial=fixture();serial.recompute();healthy(serial);workers.set("2");Document parallel=fixture();parallel.recompute();healthy(parallel);
    check(parallel.parallelFeatures>0,"Independent jobs must run past intervening owner-only rows");check(parallel.peakParallelFeatures<=2,"Look-ahead must honor the configured CPU/feature bound");
    for(size_t i=0;i<serial.evalBodies.size();++i){const auto& a=serial.evalBodies[i];const auto& b=parallel.evalBodies[i];check(a.id==b.id,"Publication preserves document order");const auto aa=a.bbox(),bb=b.bbox();check((aa.mn-bb.mn).len()<1e-12&&(aa.mx-bb.mx).len()<1e-12,"Look-ahead must preserve evaluated bounds");}
    const auto old=parallel.evaluationCache;parallel.recompute();healthy(parallel);check(parallel.recomputeEvaluated==0,"Stable authoring still reuses every cached result");for(const auto& [id,entry]:old)check(parallel.evaluationCache.at(id).body==entry.body,"A cache hit preserves immutable owners");
    AgentSession session;session.document=fixture();session.document.recompute();const auto before=documentToJson(session.document);const auto revision=session.revision;arkjson::Value args;check(arkjson::parse(R"({"commands":[{"op":"feature.edit","id":1,"p":{"dx":-1}}]})",args),"fixture JSON");args.set("expected_revision",arkjson::Value::makeNum(revision));const auto rejected=session.call("doc_apply",args);check(rejected.find("isError")->b&&session.revision==revision&&documentToJson(session.document)==before,"Failed source edit remains atomic across staged independent jobs");
}
}
int main(){try{run();
#ifdef ARK_CONTRACTS_NATIVE_TESTS
 nativePoolCancellation();
#endif
std::cout<<"Bounded cross-row scheduling, serial parity, ordered publication, reuse and atomic rejection passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
