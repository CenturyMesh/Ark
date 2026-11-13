#include "ark/compute.hpp"
#include "../src/compute_transform_070a.hpp"
#include "../src/view/gpu_cache_policy_070a.hpp"
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace ark;
static int checks=0;
static void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
int main(){try{
    constexpr size_t MiB=1024*1024;
    check(displayIdleBudget070(0,0)==32*MiB,"Unknown GPU needs conservative fallback");
    check(displayIdleBudget070(16*1024*1024,12*1024*1024)==256*MiB,"16GBGPU uses bounded256MB idle allowance");
    check(displayIdleBudget070(16*1024*1024,128*1024)==0,"Pressure releases all idle display storage");
    check(displayIdleBudget070(16*1024*1024,12*1024*1024,0)==0,"Explicit disable leaves no idle allowance");
    check(displayIdleBudget070(512*1024,384*1024)<=16*MiB,"Small adapters retain a small allowance");
    std::mt19937 generator(42);std::uniform_real_distribution<float> values(-100000,100000);
    for(size_t n:{0u,1u,2u,3u,4u,5u,7u,16u,31u,255u,4097u})for(int trial=0;trial<8;++trial){
        std::vector<float> input(n*3+2),reference(n*3+2,12345),actual=reference;for(auto& v:input)v=values(generator);
        auto matrix=Mat4::translation({values(generator),values(generator),values(generator)})*Mat4::rotY(trial*.17)*Mat4::rotZ(trial*.33);
        if(trial==7){matrix.m[0]=1e-20;matrix.m[5]=-1e10;matrix.m[10]=0;}
        packedTransformScalar070(input.data()+1,reference.data()+1,n,matrix);packedTransformCpu070(input.data()+1,actual.data()+1,n,matrix);
        check(std::memcmp(actual.data(),reference.data(),actual.size()*sizeof(float))==0,"SIMD changed scalar bits, tails, unaligned input or sentinels");
    }
    std::vector<float> input(12000);for(auto& value:input)value=values(generator);auto matrix=Mat4::rotZ(.317);
    std::vector<float> expected;check(transformPacked(input,expected,matrix,ComputeMode::CPU),"CPU transform failed");
    auto aliased=input;check(transformPacked(aliased,aliased,matrix,ComputeMode::CPU)&&aliased==expected,"Aliased vector transform changed");
    auto invalid=expected;check(!transformPacked(std::vector<float>{1},invalid,matrix,ComputeMode::CPU)&&invalid==expected,"Malformed input changed output");
    std::vector<std::future<bool>> jobs;for(int t=0;t<8;++t)jobs.push_back(std::async(std::launch::async,[&]{for(int n=0;n<20;++n){std::vector<float> output;if(!transformPacked(input,output,matrix,ComputeMode::CPU)||output!=expected)return false;}return true;}));
    for(auto& job:jobs)check(job.get(),"Concurrent CPU transform changed output");
    input.resize(1048576*3);for(size_t i=0;i<input.size();++i)input[i]=(float)((i%8191)*.037);
    expected.resize(input.size());std::vector<float> result(input.size());std::vector<double> scalarTimes,simdTimes;
    for(int n=0;n<7;++n){auto start=std::chrono::steady_clock::now();packedTransformScalar070(input.data(),expected.data(),input.size()/3,matrix);auto middle=std::chrono::steady_clock::now();packedTransformCpu070(input.data(),result.data(),input.size()/3,matrix);auto end=std::chrono::steady_clock::now();
        check(result==expected,"Benchmark scalar/SIMD differs");if(n>=2){scalarTimes.push_back(std::chrono::duration<double,std::milli>(middle-start).count());simdTimes.push_back(std::chrono::duration<double,std::milli>(end-middle).count());}}
    std::sort(scalarTimes.begin(),scalarTimes.end());std::sort(simdTimes.begin(),simdTimes.end());
    std::cout<<"COMPUTE_SIMD_070A scalar_median_ms="<<scalarTimes[2]<<" dispatched_median_ms="<<simdTimes[2]<<" backend="<<packedTransformCpuBackend070()<<" vertices=1048576 exact_parity=true\n";
    std::cout<<"Compute SIMD/VRAM checks passed: "<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
