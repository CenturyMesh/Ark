#include "ark/resource_policy_071d.hpp"
#include "ark/evaluation_scheduler_070a.hpp"
#include "ark/agent.hpp"
#include "ark/native_backend.hpp"
#include "ark/iteration_history.hpp"
#include "ark/serialize.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace {
using namespace ark;using V=arkjson::Value;namespace fs=std::filesystem;
constexpr std::uint64_t MiB=1024ull*1024ull,GiB=1024ull*MiB;
size_t checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F action,const char* why){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,why);}
const V& field(const V& value,const char* key){const auto* item=value.find(key);if(!item)throw std::runtime_error(std::string("Missing ")+key);return *item;}
V json(const char* source){V value;if(!arkjson::parse(source,value))throw std::runtime_error("Invalid resource fixture");return value;}
struct Environment {
    const char* name;std::string previous;
    Environment(const char* key,const std::string& value):name(key),previous(std::getenv(key)?std::getenv(key):""){set(value);}
    void set(const std::string& value){
#ifdef _WIN32
        _putenv_s(name,value.c_str());
#else
        if(value.empty())unsetenv(name);else setenv(name,value.c_str(),1);
#endif
    }
    ~Environment(){set(previous);}
};
ResourceHardware071D hardware(unsigned gib,unsigned logical=32,unsigned physical=24){return {std::uint64_t(gib)*GiB,std::uint64_t(gib)*GiB*3/4,logical,physical};}
void policyMatrix(){
    ResourceOverrides071D automatic;
    for(unsigned gib:{2u,4u,8u,16u,32u,64u}){
        const auto p=selectResourcePolicy071D(hardware(gib),automatic,96);
        check(p.decodedTargetBytes==std::uint64_t(gib)*GiB/16,"Automatic decoded target must scale with total RAM");
        check(p.payloadTargetBytes==std::uint64_t(gib)*GiB/64,"Automatic scheduler target must scale with total RAM");
        check(p.decodedLimitBytes==p.decodedTargetBytes&&p.payloadLimitBytes==p.payloadTargetBytes,"Healthy free RAM should permit nominal targets");
        check(p.featureWorkers<=p.cpuThreads&&p.featureWorkers*p.nativeThreadsPerWorker<=p.cpuThreads,"Automatic policy oversubscribes its CPU allowance");
    }
    const auto large=selectResourcePolicy071D(hardware(64),automatic,96);
    check(large.featureWorkers==16&&large.nativeThreadsPerWorker==2&&large.ownerNativeThreads==8,"32 logical/24 physical cores should select16 workers,2 perworker native threads and8 solo-owner threads");
    check(large.decodedLimitBytes==4*GiB&&large.payloadLimitBytes==GiB&&large.decodedEntryLimit==2048,"64GiB capacities and entry limit");
    const auto fourChains=selectResourcePolicy071D(hardware(64),automatic,96,0,4);
    check(fourChains.featureWorkers==4&&fourChains.nativeThreadsPerWorker==8,"Four deep dirty chains must not lose native CPU allowance to a larger hardware worker ceiling");
    const auto oneChain=selectResourcePolicy071D(hardware(64),automatic,96,0,1);
    check(oneChain.featureWorkers==1&&oneChain.nativeThreadsPerWorker==8&&oneChain.ownerNativeThreads==8,"A serial dependency chain must retain solo native allowance");
    const auto sixteen=selectResourcePolicy071D(hardware(16,8,4),automatic,96);
    check(sixteen.decodedLimitBytes==GiB&&sixteen.payloadLimitBytes==256*MiB&&sixteen.featureWorkers==4,"16GiB/4-core policy must remain bounded without assuming16GiB minimum");
    const auto many=selectResourcePolicy071D(hardware(1024,128,64),automatic,96);
    check(many.decodedTargetBytes==16*GiB&&many.payloadTargetBytes==4*GiB&&many.cpuThreads==32&&many.featureWorkers==16,"Large hardware auto ceilings must remain explicit");
    auto low=hardware(2,4,2);low.availableRamBytes=300*MiB;auto p=selectResourcePolicy071D(low,automatic,96);
    check(p.featureWorkers==1&&p.workersPressureLimited&&p.decodedPressureLimited&&p.payloadPressureLimited,"Low free RAM must constrain workers and both memory tiers");
    low.availableRamBytes=100*MiB;p=selectResourcePolicy071D(low,automatic,96);check(p.decodedLimitBytes==0&&p.payloadLimitBytes==0&&p.featureWorkers==1,"Under reserve must disable extra retained/admitted payloads");
    auto pressure=hardware(64);pressure.availableRamBytes=2*GiB;p=selectResourcePolicy071D(pressure,automatic,96,4*GiB);
    check(p.decodedLimitBytes==GiB&&p.payloadLimitBytes==0&&p.featureWorkers==1,"Pressure must reclaim decoded allocations and stop parallel payload admission");
    pressure.availableRamBytes=5*GiB;const auto after=selectResourcePolicy071D(pressure,automatic,96,GiB);check(after.decodedLimitBytes==GiB,"Accounting for retained cache must avoid a self-eviction feedback loop");
    ResourceHardware071D unknown;unknown.logicalThreads=32;unknown.physicalCores=24;p=selectResourcePolicy071D(unknown,automatic,96);
    check(!p.memoryKnown&&p.decodedLimitBytes==256*MiB&&p.payloadLimitBytes==128*MiB&&p.featureWorkers==4,"Unavailable memory telemetry needs conservative explicit fallbacks");
    p=selectResourcePolicy071D({512*MiB,400*MiB,2,1},automatic,96);check(p.decodedTargetBytes==32*MiB&&p.payloadTargetBytes==8*MiB&&p.featureWorkers==1,"Sub-GiB machines must not receive16GiB assumptions");
    ResourceOverrides071D explicitValues;explicitValues.cpuThreads=32;explicitValues.featureWorkers=64;explicitValues.nativeThreads=64;explicitValues.decodedCacheBytes=8*GiB;explicitValues.payloadBytes=2*GiB;
    p=selectResourcePolicy071D(hardware(64),explicitValues,96);check(p.featureWorkers==32&&p.nativeThreadsPerWorker==1&&p.ownerNativeThreads==32,"Explicit64 workers must clamp to actual32-CPU allowance");
    check(p.decodedLimitBytes==8*GiB&&p.payloadLimitBytes==2*GiB&&p.decodedOverride&&p.payloadOverride&&p.featureOverride,"Explicit memory ceilings above1GiB must be accepted when headroom permits");
    p=selectResourcePolicy071D(hardware(64),explicitValues,7);check(p.featureWorkers==7&&p.nativeThreadsPerWorker==4,"Actual dirty task count must bound requested workers");
    p=selectResourcePolicy071D(hardware(64),explicitValues,1);check(p.featureWorkers==1&&p.nativeThreadsPerWorker==32,"Single dirty feature needs no pool and retains native CPU allowance");
    explicitValues.decodedCacheBytes=0;explicitValues.payloadBytes=0;p=selectResourcePolicy071D(hardware(64),explicitValues,96);check(p.decodedLimitBytes==0&&p.payloadLimitBytes==0&&p.featureWorkers==1,"Explicit zero memory tiers must retain owner evaluation");
    automatic.nativeCacheEnabled=false;p=selectResourcePolicy071D(hardware(64),automatic,96);check(p.decodedLimitBytes==0&&p.payloadLimitBytes==GiB,"Global native cache disable must release decoded tier without disabling geometry workers");automatic.nativeCacheEnabled=true;
    explicitValues.featureWorkers=0;rejects([&]{selectResourcePolicy071D(hardware(64),explicitValues);},"Invalid pure-policy override must reject");
    for(std::uint64_t mb:{128ull,512ull,2048ull,8192ull,16384ull,32768ull,65536ull,262144ull})for(unsigned logical:{1u,2u,4u,8u,16u,32u,64u})for(unsigned freePercent:{0u,5u,25u,50u,100u}){
        ResourceHardware071D h{mb*MiB,mb*MiB*freePercent/100,logical,std::max(1u,logical/2)};const auto q=selectResourcePolicy071D(h,automatic,768);
        check(q.featureWorkers>=1&&q.featureWorkers<=q.cpuThreads&&q.featureWorkers*q.nativeThreadsPerWorker<=q.cpuThreads,"Matrix CPU bounds");
        check(q.decodedLimitBytes<=q.decodedTargetBytes&&q.payloadLimitBytes<=q.payloadTargetBytes&&q.reserveBytes<=h.totalRamBytes/2,"Matrix RAM bounds");
        check(q.decodedEntryLimit>=128&&q.decodedEntryLimit<=4096&&q.ownerNativeThreads<=q.cpuThreads,"Matrix entry/owner bounds");
    }
    const auto receipt=resourcePolicyReceipt071D(large);check(field(receipt,"feature_workers").num==16&&!field(receipt,"reasons").arr.empty()&&field(field(receipt,"hardware"),"memory_known").b,"Receipt must expose selected policy, hardware and reasons");
}
void overridesAndProbe(){
    Environment cpu("ARK_CPU_THREADS","32"),workers("ARK_FEATURE_THREADS","64"),native("ARK_NATIVE_THREADS","8"),ram("ARK_NATIVE_MEMORY_CACHE_MAX_MB","4096"),payload("ARK_FEATURE_PAYLOAD_MAX_MB","1024"),cache("ARK_NATIVE_RESULT_CACHE","1");
    auto o=resourceOverrides071D();check(o.featureWorkers==64&&o.decodedCacheBytes==4*GiB&&o.payloadBytes==GiB,"Larger explicit worker and RAM controls must parse");
    workers.set("65");rejects([]{resourceOverrides071D();},"Feature worker override beyond64 must reject");workers.set("16");
    cpu.set("0");rejects([]{resourceOverrides071D();},"CPU zero must retain explicit rejection");cpu.set("32");
    native.set("wrong");rejects([]{resourceOverrides071D();},"Invalid native allowance must reject");native.set("8");
    payload.set("65537");rejects([]{resourceOverrides071D();},"Oversized payload override must reject");payload.set("1024");
    ram.set("wrong");o=resourceOverrides071D();check(!o.decodedCacheBytes&&!o.warnings.empty(),"Invalid legacy decoded cap must retain fallback behavior and expose warning");
    ram.set("65536");o=resourceOverrides071D();check(o.decodedCacheBytes==64*GiB,"Explicit decoded cap can exceed old1GiB ceiling");
    cache.set("0");check(!resourceOverrides071D().nativeCacheEnabled,"Global cache disable");
    // Linux budgets use MemAvailable: page cache left by a build stays usable for new work.
    const auto parsed=memAvailableBytes071D("MemTotal:       16374668 kB\nMemFree:          412300 kB\nMemAvailable:   13120044 kB\nBuffers:          120000 kB\n");
    check(parsed&&*parsed==13120044ull*1024,"MemAvailable must count reclaimable page cache, not only free pages");
    check(!memAvailableBytes071D("MemTotal: 1 kB\nMemFree: 1 kB\n")&&!memAvailableBytes071D("MemAvailable: lots kB\n")&&!memAvailableBytes071D("MemAvailable: 12 MB\n"),"Missing or malformed MemAvailable must keep the sysconf fallback");
    const auto h=probeResourceHardware071D();check(h.logicalThreads>=1,"Live hardware probe must identify available CPU threads");
#ifdef _WIN32
    check(h.physicalCores>=1&&h.physicalCores<=h.logicalThreads&&h.totalRamBytes>0&&h.availableRamBytes<=h.totalRamBytes,"Windows physical topology and live memory probe");
#elif defined(__linux__)
    check(h.totalRamBytes>0&&h.availableRamBytes>0&&h.availableRamBytes<=h.totalRamBytes,"Linux live memory probe");
#endif
}

#ifdef ARK_RESOURCE_POLICY_NATIVE_TESTS
V apply(AgentSession& session,const V& commands){V args=V::makeObj();args.set("commands",commands);args.set("expected_revision",V::makeNum(session.revision));const auto r=session.call("doc_apply",args);if(field(r,"isError").b)throw std::runtime_error(r.dump());return field(r,"structuredContent");}
V ownerChain(){V commands=json(R"([{"op":"feature.add","type":"native_solid","name":"Base","native":{"op":"box"},"p":{"width":120,"depth":20,"height":10}}])");for(int i=0;i<16;++i){auto hole=json(R"({"op":"feature.add","type":"native_solid","native":{"op":"hole","cutter":{"kind":"circle","radius":1,"depth":12,"frame":[1,0,0,0,0,1,0,0,0,0,1,0,3,10,-1,1]}}})");hole.set("name",V::makeStr("Hole "+std::to_string(i)));V inputs=V::makeArr();inputs.arr.push_back(V::makeNum(i+1));hole.set("inputs",inputs);auto native=field(hole,"native"),cutter=field(native,"cutter"),frame=field(cutter,"frame");frame.arr[12]=V::makeNum(3+6*i);cutter.set("frame",frame);native.set("cutter",cutter);hole.set("native",native);commands.arr.push_back(hole);}return commands;}
void ownerAndWorkerPolicy(){
    Environment cpu("ARK_CPU_THREADS","32"),workers("ARK_FEATURE_THREADS","4"),native("ARK_NATIVE_THREADS","8"),payload("ARK_FEATURE_PAYLOAD_MAX_MB","1024");
    AgentSession four,sixteen;for(auto* s:{&four,&sixteen}){s->suppressIterationHistory();s->suppressFailureCapture();}
    const auto commands=ownerChain();apply(four,commands);const auto a=evaluationSchedulerDiagnostics070A();workers.set("16");apply(sixteen,commands);const auto b=evaluationSchedulerDiagnostics070A();
    check(four.document.parallelFeatures==0&&sixteen.document.parallelFeatures==0,"Serial native chain must remain owner-evaluated");
    check(a.peakOwnerNativeThreads==a.resourcePolicy.ownerNativeThreads&&b.peakOwnerNativeThreads==b.resourcePolicy.ownerNativeThreads&&a.peakOwnerNativeThreads==b.peakOwnerNativeThreads,"Raising worker ceiling must not throttle owner-only native operations");
    check(b.resourcePolicy.eligibleTasks==17&&sixteen.document.featureWorkers<=16,"Native policy must reflect actual eligible work");
    check(b.resourcePolicy.estimatedParallelWidth==1&&sixteen.document.featureWorkers==1&&sixteen.document.nativeThreadsPerWorker==b.resourcePolicy.ownerNativeThreads,"Serial-chain width must determine actual worker/native split");
    check(four.document.evalBodies.size()==sixteen.document.evalBodies.size(),"Owner-chain count changed");
    for(size_t i=0;i<four.document.evalBodies.size();++i){const auto& x=four.document.evalBodies[i];const auto& y=sixteen.document.evalBodies[i];check(x.nativeData.find("brep_base64")->str==y.nativeData.find("brep_base64")->str&&x.nativeData.find("topology")->dump()==y.nativeData.find("topology")->dump(),"Owner allowance change altered exact B-rep or topology");}
    const double actual=field(*sixteen.document.evalBodies.back().nativeData.find("report"),"volume_mm3").num;check(std::abs(actual-(120*20*10-16*std::acos(-1.)*10))<1e-6,"Independent drilled-chain analytic volume");
    payload.set("0");AgentSession owner;owner.suppressIterationHistory();owner.suppressFailureCapture();apply(owner,commands);check(owner.document.featureWorkers==1&&owner.document.parallelFeatures==0,"Zero snapshot admission must preserve valid owner geometry");
    check(owner.document.evalBodies.back().nativeData.find("brep_base64")->str==sixteen.document.evalBodies.back().nativeData.find("brep_base64")->str,"Owner fallback changed final exact geometry");
    payload.set("1024");V branches=V::makeArr();
    for(int branch=0;branch<4;++branch)for(size_t i=0;i<commands.arr.size();++i){auto command=commands.arr[i];command.set("name",V::makeStr("Branch "+std::to_string(branch)+" / "+field(command,"name").str));if(i){V inputs=V::makeArr();inputs.arr.push_back(V::makeNum(branch*17+i));command.set("inputs",inputs);}branches.arr.push_back(command);}
    AgentSession narrow;narrow.suppressIterationHistory();narrow.suppressFailureCapture();apply(narrow,branches);const auto c=evaluationSchedulerDiagnostics070A();
    check(c.resourcePolicy.eligibleTasks==68&&c.resourcePolicy.estimatedParallelWidth==4,"Four deep chains must have width4, not68 simultaneously runnable operations");
    check(narrow.document.featureWorkers<=4&&narrow.document.nativeThreadsPerWorker==std::min(8u,c.resourcePolicy.cpuThreads/narrow.document.featureWorkers),"Four-chain native allowance was throttled by requested16 worker ceiling");
    for(size_t branch=0;branch<4;++branch)check(narrow.document.evalBodies[branch*17+16].nativeData.find("brep_base64")->str==sixteen.document.evalBodies.back().nativeData.find("brep_base64")->str,"Four-chain scheduling changed final exact B-rep");
}
struct Workspace {fs::path parent,root;Workspace(){parent=fs::weakly_canonical(fs::temp_directory_path());root=parent/("ark-resource-071d-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(fs::create_directory(root),"Create isolated native cache");}~Workspace(){std::error_code error;const auto path=fs::weakly_canonical(root,error);if(!error&&path.parent_path()==parent&&!fs::is_symlink(root,error))fs::remove_all(path,error);}};
std::string utf8(const fs::path& path){const auto bytes=path.u8string();return {bytes.begin(),bytes.end()};}
V request(const char* operation,double value){auto result=json(R"({"operation":"model_solid","request":{}})");V model=V::makeObj();model.set("op",V::makeStr(operation));if(std::string(operation)=="sphere")model.set("radius",V::makeNum(value));else{model.set("width",V::makeNum(value));model.set("depth",V::makeNum(4));model.set("height",V::makeNum(5));}result.set("request",model);return result;}
V memory(){return field(nativeKernelInfo(),"native_memory_cache");}
void cachePolicyIntegration(){
    Workspace workspace;Environment directory("ARK_NATIVE_CACHE_DIRECTORY",utf8(workspace.root)),ram("ARK_NATIVE_MEMORY_CACHE_MAX_MB","0"),enabled("ARK_NATIVE_RESULT_CACHE","1");(void)memory();ram.set("1024");
    const auto starting=memory();check(field(starting,"accounted_retained_bytes").num==0,"Raising memory ceiling must not preallocate cache payloads");
    const auto& selected=field(starting,"resource_policy");check(field(starting,"capacity_bytes").num==field(selected,"decoded_limit_bytes").num&&field(selected,"decoded_target_bytes").num==double(GiB),"Native RAM cache must use shared selected policy");
    for(int i=0;i<140;++i){const auto command=request("box",10+i);const auto cold=nativeWorkerRequest(command),hot=nativeWorkerRequest(command);check(cold.dump()==hot.dump(),"Adaptive cache changed exact native result");}
    auto usage=memory();check(field(usage,"accounted_retained_bytes").num<=field(usage,"capacity_bytes").num,"Adaptive cache exceeds actual allowance");
#ifdef _WIN32
    if(field(starting,"capacity_bytes").num>=512*MiB)check(field(usage,"entries").num==140&&field(usage,"entry_limit").num>128,"Larger cache must retain more than old128 small entries");
#else
    // Persistent native reuse is Windows-only until the ELF dependency closure has custody,
    // and the decoded RAM tier is filled only from verified disk hits.
    check(field(usage,"entries").num==0&&field(usage,"accounted_retained_bytes").num==0,"Without persistent native reuse no decoded entries are retained");
#endif
    ram.set("1");usage=memory();check(field(usage,"accounted_retained_bytes").num<=MiB,"Lower selected ceiling must trim retained entries promptly");
    ram.set("0");usage=memory();check(field(usage,"entries").num==0&&field(usage,"accounted_retained_bytes").num==0,"Disabled cache must release retained allocations");
    ram.set("");usage=memory();check(!field(field(usage,"resource_policy"),"decoded_override").b&&field(usage,"accounted_retained_bytes").num==0,"Returning toAuto must select hardware policy without allocating");
    const auto kernel=nativeKernelInfo();check(field(kernel,"resource_policy").find("owner_native_threads")&&!field(field(kernel,"resource_policy"),"reasons").arr.empty(),"Native kernel diagnostics must explain CPU/RAM choices");
}
#endif
}
int main(){try{policyMatrix();overridesAndProbe();
#ifdef ARK_RESOURCE_POLICY_NATIVE_TESTS
ownerAndWorkerPolicy();cachePolicyIntegration();
#endif
std::cout<<"071D resource policy: "<<checks<<" proportional RAM, pressure, override, topology, cache and owner/native geometry checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
