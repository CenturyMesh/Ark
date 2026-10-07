#include "ark/compute.hpp"
#include <chrono>
#include <cstdlib>
#include <mutex>
#ifdef ARK_CUDA_RUNTIME
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <cuda.h>
#include <nvrtc.h>
#endif
namespace ark {
namespace {
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point begin){return std::chrono::duration<double,std::milli>(Clock::now()-begin).count();}
void cpu(const std::vector<f32>& in,std::vector<f32>& out,const Mat4& m){
    out.resize(in.size());for(size_t i=0;i<in.size();i+=3){
        double x=in[i],y=in[i+1],z=in[i+2];
        for(int r=0;r<3;++r)out[i+r]=(f32)(m.m[r]*x+m.m[4+r]*y+m.m[8+r]*z+m.m[12+r]);
    }
}
struct Compute {
    std::mutex mutex;bool attempted=false,available=false;std::string device,reason="Not initialized",last="CPU";
    double initMs=0,lastMs=0;size_t threshold=1048576;
    bool calibrated[3]={false,false,false},preferGpu[3]={false,false,false};
    double autoCpuMs[3]={0,0,0},autoGpuMs[3]={0,0,0};
#ifdef ARK_CUDA_RUNTIME
    HMODULE driver=nullptr,rtc=nullptr;CUcontext context=nullptr;CUmodule module=nullptr;CUfunction kernel=nullptr;
    CUdeviceptr input=0,output=0,matrix=0;size_t capacity=0;
#define DECLARE(name) decltype(&name) name##_ = nullptr;
    DECLARE(cuInit) DECLARE(cuDeviceGet) DECLARE(cuDeviceGetName) DECLARE(cuDevicePrimaryCtxRetain)
    DECLARE(cuCtxPushCurrent) DECLARE(cuCtxPopCurrent) DECLARE(cuModuleLoadData) DECLARE(cuModuleGetFunction)
    DECLARE(cuMemAlloc) DECLARE(cuMemFree) DECLARE(cuMemcpyHtoD) DECLARE(cuMemcpyDtoH)
    DECLARE(cuLaunchKernel) DECLARE(cuCtxSynchronize)
    DECLARE(nvrtcCreateProgram) DECLARE(nvrtcCompileProgram) DECLARE(nvrtcGetPTXSize) DECLARE(nvrtcGetPTX)
    DECLARE(nvrtcDestroyProgram) DECLARE(nvrtcGetProgramLogSize) DECLARE(nvrtcGetProgramLog)
#undef DECLARE
    bool initCuda(){
        driver=LoadLibraryW(L"nvcuda.dll");if(!driver){reason="NVIDIA driver unavailable";return false;}
        const char* root=std::getenv("CUDA_PATH");if(!root){reason="CUDA_PATH is unset (NVRTC required)";return false;}
        std::error_code ec;auto bin=std::filesystem::path(root)/"bin";
        for(auto& item:std::filesystem::directory_iterator(bin,ec)){
            auto name=item.path().filename().string();
            if(name.rfind("nvrtc64_",0)==0&&item.path().extension()==".dll"){rtc=LoadLibraryExW(item.path().c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(rtc)break;}
        }
        if(!rtc){reason="NVRTC runtime unavailable";return false;}
#define STR2(x) #x
#define STR(x) STR2(x)
#define LOAD(name,lib) if(!(name##_ = reinterpret_cast<decltype(name##_)>((void*)GetProcAddress(lib,STR(name))))){reason="Missing GPU runtime symbol " STR(name);return false;}
        LOAD(cuInit,driver) LOAD(cuDeviceGet,driver) LOAD(cuDeviceGetName,driver) LOAD(cuDevicePrimaryCtxRetain,driver)
        LOAD(cuCtxPushCurrent,driver) LOAD(cuCtxPopCurrent,driver) LOAD(cuModuleLoadData,driver) LOAD(cuModuleGetFunction,driver)
        LOAD(cuMemAlloc,driver) LOAD(cuMemFree,driver) LOAD(cuMemcpyHtoD,driver) LOAD(cuMemcpyDtoH,driver)
        LOAD(cuLaunchKernel,driver) LOAD(cuCtxSynchronize,driver)
        LOAD(nvrtcCreateProgram,rtc) LOAD(nvrtcCompileProgram,rtc) LOAD(nvrtcGetPTXSize,rtc) LOAD(nvrtcGetPTX,rtc)
        LOAD(nvrtcDestroyProgram,rtc) LOAD(nvrtcGetProgramLogSize,rtc) LOAD(nvrtcGetProgramLog,rtc)
#undef LOAD
#undef STR
#undef STR2
        CUdevice dev=0;if(cuInit_(0)||cuDeviceGet_(&dev,0)||cuDevicePrimaryCtxRetain_(&context,dev)){reason="CUDA device initialization failed";return false;}
        char name[256]{};cuDeviceGetName_(name,sizeof name,dev);device=name;
        const char* code=R"(extern "C" __global__ void transform(const float* src,float* dst,const double* m,unsigned long long n){
          unsigned long long i=(unsigned long long)blockIdx.x*blockDim.x+threadIdx.x;if(i>=n)return;
          double x=src[3*i],y=src[3*i+1],z=src[3*i+2];
          for(int r=0;r<3;++r)dst[3*i+r]=(float)(m[r]*x+m[4+r]*y+m[8+r]*z+m[12+r]);
        })";
        nvrtcProgram program=nullptr;if(nvrtcCreateProgram_(&program,code,"ark_transform.cu",0,nullptr,nullptr)){reason="NVRTC program creation failed";return false;}
        const char* options[]={"--gpu-architecture=compute_52","--fmad=false"};
        auto compiled=nvrtcCompileProgram_(program,2,options);
        if(compiled){size_t size=0;nvrtcGetProgramLogSize_(program,&size);std::string log(size,'\0');nvrtcGetProgramLog_(program,log.data());nvrtcDestroyProgram_(&program);reason="NVRTC: "+log;return false;}
        size_t size=0;nvrtcGetPTXSize_(program,&size);std::string ptx(size,'\0');nvrtcGetPTX_(program,ptx.data());nvrtcDestroyProgram_(&program);
        if(cuCtxPushCurrent_(context)){reason="Cannot activate CUDA context";return false;}
        bool success=!cuModuleLoadData_(&module,ptx.c_str())&&!cuModuleGetFunction_(&kernel,module,"transform")&&!cuMemAlloc_(&matrix,16*sizeof(double));
        CUcontext popped;cuCtxPopCurrent_(&popped);reason=success?"Ready":"CUDA PTX/module load failed";return success;
    }
    bool gpu(const std::vector<f32>& in,std::vector<f32>& out,const Mat4& m){
        if(cuCtxPushCurrent_(context))return false;
        struct Guard {Compute* c;~Guard(){CUcontext p;c->cuCtxPopCurrent_(&p);}} guard{this};
        size_t bytes=in.size()*sizeof(float);
        if(bytes>capacity){
            if(input)cuMemFree_(input);
            if(output)cuMemFree_(output);
            input=output=0;capacity=0;
            if(cuMemAlloc_(&input,bytes)||cuMemAlloc_(&output,bytes))return false;
            capacity=bytes;
        }
        out.resize(in.size());unsigned long long count=in.size()/3;
        if(cuMemcpyHtoD_(input,in.data(),bytes)||cuMemcpyHtoD_(matrix,m.m,sizeof m.m))return false;
        void* arguments[]={&input,&output,&matrix,&count};
        if(cuLaunchKernel_(kernel,(unsigned)((count+255)/256),1,1,256,1,1,0,nullptr,arguments,nullptr)||cuCtxSynchronize_())return false;
        return cuMemcpyDtoH_(out.data(),output,bytes)==CUDA_SUCCESS;
    }
#endif
    void init(){
        if(attempted)return;
        attempted=true;auto start=Clock::now();
        if(std::getenv("ARK_DISABLE_GPU")){reason="Disabled by ARK_DISABLE_GPU";return;}
#ifdef ARK_CUDA_RUNTIME
        try{available=initCuda();}catch(const std::exception& e){reason=e.what();}
#else
        reason="CPU build (CUDA headers/runtime support not configured)";
#endif
        initMs=ms(start);
    }
};
Compute& state(){static Compute c;return c;}
}
bool transformPacked(const std::vector<f32>& source,std::vector<f32>& output,const Mat4& m,ComputeMode mode){
    if(source.size()%3)return false;
    if(&source==&output){auto copy=source;return transformPacked(copy,output,m,mode);}
    auto& c=state();std::lock_guard<std::mutex> lock(c.mutex);
    if(mode!=ComputeMode::CPU&&(mode==ComputeMode::GPU||source.size()/3>=c.threshold))c.init();
    auto begin=Clock::now();bool used=false;
#ifdef ARK_CUDA_RUNTIME
    const size_t vertices=source.size()/3;const int bucket=vertices<=2097152?0:vertices<=4194304?1:2;
    if(c.available&&mode==ComputeMode::Auto&&vertices>=c.threshold&&!c.calibrated[bucket]){
        // Measure complete transfers on the actual device/size class, not just
        // kernel time. Keep the CPU result as a parity control and fallback.
        std::vector<f32> reference,trial;cpu(source,reference,m);
        bool gpuOkay=c.gpu(source,trial,m);
        if(gpuOkay){
            for(size_t i=0;i<trial.size();++i)if(trial[i]!=reference[i]){gpuOkay=false;break;}
        }
        if(gpuOkay){
            std::vector<double> cpuTimes,gpuTimes;
            for(int sample=0;sample<3;++sample){
                auto start=Clock::now();cpu(source,reference,m);cpuTimes.push_back(ms(start));
                start=Clock::now();if(!c.gpu(source,trial,m)){gpuOkay=false;break;}gpuTimes.push_back(ms(start));
            }
            if(gpuOkay){std::sort(cpuTimes.begin(),cpuTimes.end());std::sort(gpuTimes.begin(),gpuTimes.end());c.autoCpuMs[bucket]=cpuTimes[1];c.autoGpuMs[bucket]=gpuTimes[1];}
        }
        c.calibrated[bucket]=true;c.preferGpu[bucket]=gpuOkay&&c.autoGpuMs[bucket]<c.autoCpuMs[bucket]*.95;
        used=c.preferGpu[bucket];output=used?std::move(trial):std::move(reference);
        c.last=used?"CUDA":"CPU";c.lastMs=ms(begin);return true;
    }
    if(c.available&&mode!=ComputeMode::CPU&&!source.empty()&&(mode==ComputeMode::GPU||(vertices>=c.threshold&&c.preferGpu[bucket]))){
        used=c.gpu(source,output,m);if(!used){c.available=false;c.reason="CUDA operation failed; CPU fallback enabled";}
    }
#endif
    if(mode==ComputeMode::GPU&&!used)return false;
    if(!used)cpu(source,output,m);
    c.last=used?"CUDA":"CPU";c.lastMs=ms(begin);return true;
}
arkjson::Value computeInfo(){
    auto& c=state();std::lock_guard<std::mutex> lock(c.mutex);c.init();using V=arkjson::Value;V r=V::makeObj();
    r.set("available",V::makeBool(c.available));r.set("device",V::makeStr(c.device));r.set("reason",V::makeStr(c.reason));
    r.set("last_backend",V::makeStr(c.last));r.set("last_ms",V::makeNum(c.lastMs));r.set("initialization_ms",V::makeNum(c.initMs));
    r.set("auto_threshold_vertices",V::makeNum(c.threshold));r.set("operation",V::makeStr("packed mesh positions / f64 matrix arithmetic / f32 output"));
    r.set("auto_policy",V::makeStr("Calibrate complete CPU/GPU transfers per size bucket; GPU needs exact parity and at least 5 percent measured benefit"));
    V profiles=V::makeArr();for(int i=0;i<3;++i){V row=V::makeObj();row.set("bucket",V::makeNum(i));row.set("calibrated",V::makeBool(c.calibrated[i]));if(c.calibrated[i]){row.set("selected",V::makeStr(c.preferGpu[i]?"CUDA":"CPU"));row.set("cpu_median_ms",V::makeNum(c.autoCpuMs[i]));row.set("gpu_median_ms",V::makeNum(c.autoGpuMs[i]));}profiles.arr.push_back(std::move(row));}r.set("auto_profiles",std::move(profiles));return r;
}
arkjson::Value benchmarkCompute(size_t count,int repeats){
    using V=arkjson::Value;V r=computeInfo();if(count<1||count>4000000||repeats<1||repeats>20){r.set("error",V::makeStr("Invalid benchmark bounds"));return r;}
    std::vector<f32> source(count*3),reference,output;for(size_t i=0;i<source.size();++i)source[i]=(f32)((i%8191)*.037);
    Mat4 matrix=Mat4::translation({421.25,-215.75,19.125})*Mat4::rotZ(.713)*Mat4::rotY(-.29);
    std::vector<double> cpuTimes,gpuTimes;
    transformPacked(source,reference,matrix,ComputeMode::CPU);bool gpu=transformPacked(source,output,matrix,ComputeMode::GPU);
    f64 error=0;if(gpu)for(size_t i=0;i<output.size();++i)error=std::max(error,std::fabs((f64)output[i]-reference[i]));
    for(int i=0;i<repeats;++i){auto t=Clock::now();transformPacked(source,reference,matrix,ComputeMode::CPU);cpuTimes.push_back(ms(t));
        if(gpu){t=Clock::now();gpu=transformPacked(source,output,matrix,ComputeMode::GPU);if(gpu)gpuTimes.push_back(ms(t));}}
    std::sort(cpuTimes.begin(),cpuTimes.end());std::sort(gpuTimes.begin(),gpuTimes.end());
    transformPacked(source,output,matrix,ComputeMode::Auto);f64 autoError=0;
    for(size_t i=0;i<output.size();++i)autoError=std::max(autoError,std::fabs((f64)output[i]-reference[i]));
    r=computeInfo();r.set("auto_max_abs_error_mm",V::makeNum(autoError));r.set("vertices",V::makeNum(count));r.set("cpu_median_ms",V::makeNum(cpuTimes[cpuTimes.size()/2]));
    r.set("gpu_executed",V::makeBool(gpu));r.set("includes_transfer_and_sync",V::makeBool(true));
    if(gpu){double gpuMs=gpuTimes[gpuTimes.size()/2];r.set("gpu_median_ms",V::makeNum(gpuMs));r.set("speedup",V::makeNum(cpuTimes[cpuTimes.size()/2]/gpuMs));r.set("max_abs_error_mm",V::makeNum(error));}
    return r;
}
}
