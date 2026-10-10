#include "ark/step_export.hpp"
#include "ark/native_backend.hpp"
#include "ark/notch.hpp"
#include <filesystem>
#include <fstream>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <cerrno>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#endif
namespace ark {
namespace fs=std::filesystem;using V=arkjson::Value;
namespace {
thread_local unsigned long long nativeRequests=0,nativeStarts=0;
std::atomic<unsigned long long> totalNativeRequests{0},totalNativeStarts{0};
thread_local int nativeThreadOverride=0;
int effectiveNativeBudget(){if(nativeThreadOverride)return nativeThreadOverride;if(const char* value=std::getenv("ARK_NATIVE_THREADS")){int n=std::atoi(value);if(n>=1&&n<=64)return n;}return (int)std::min(8u,std::max(1u,std::thread::hardware_concurrency()));}
fs::path utf8path(const std::string& value){return fs::u8path(value);}
std::string utf8(const fs::path& path){auto s=path.u8string();return std::string(s.begin(),s.end());}
std::string read(const fs::path& path){std::ifstream f(path,std::ios::binary);if(!f)return {};f.seekg(0,std::ios::end);auto n=f.tellg();if(n<0||n>256*1024*1024)throw std::runtime_error("Native worker result exceeds 256 MiB");f.seekg(0);return std::string(std::istreambuf_iterator<char>(f),{});}
fs::path executableDirectory(){
#ifdef _WIN32
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);return fs::path(path).parent_path();
#else
    char path[32768]{};auto n=readlink("/proc/self/exe",path,sizeof(path)-1);if(n>0)return fs::path(std::string(path,(size_t)n)).parent_path();return fs::current_path();
#endif
}
fs::path projectRoot(){auto p=executableDirectory();for(int i=0;i<6&&!p.empty();++i,p=p.parent_path())if(fs::exists(p/"CMakeLists.txt")&&fs::exists(p/"include/ark"))return p;return fs::path(ARK_SOURCE_DIR);}
fs::path workerPath(){
    fs::path path;
#ifdef _WIN32
    if(auto value=_wgetenv(L"ARK_OCCT_WORKER");value&&*value)path=value;
    const char* filename="ark-occt-worker.exe";
#else
    if(auto value=std::getenv("ARK_OCCT_WORKER");value&&*value)path=value;
    const char* filename="ark-occt-worker";
#endif
    if(path.empty()&&fs::exists(executableDirectory()/filename))path=executableDirectory()/filename;
    if(path.empty()){V settings;if(arkjson::parse(read(projectRoot()/".ark-local.json"),settings))if(auto p=settings.find("occt_worker");p&&p->t==V::Str)path=utf8path(p->str);}
    if(path.empty()||!fs::is_regular_file(path))throw std::runtime_error("Native OCCT 8 worker unavailable. Build ARK with scripts/build.ps1 or configure ARK_OCCT_WORKER.");return fs::absolute(path);
}
struct Temporary{
    fs::path directory;
    Temporary(){static std::atomic<unsigned long long> serial{0};for(int attempt=0;attempt<128;++attempt){auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();auto p=fs::temp_directory_path()/("ark-native-"+std::to_string(stamp)+"-"+std::to_string(serial++));if(fs::create_directory(p)){directory=p;return;}}throw std::runtime_error("Cannot create native worker workspace");}
    ~Temporary(){std::error_code ec;for(auto name:{"request.json","result.json","worker.log"})fs::remove(directory/name,ec);fs::remove(directory,ec);}
};
#ifdef _WIN32
struct Handle{HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
std::vector<wchar_t> workerEnvironment(){
    if(nativeThreadOverride<=0)return {};
    std::vector<std::wstring> entries;wchar_t* block=GetEnvironmentStringsW();if(!block)throw std::runtime_error("Cannot read worker environment");
    for(const wchar_t* p=block;*p;p+=wcslen(p)+1)if(_wcsnicmp(p,L"ARK_NATIVE_THREADS=",19)!=0)entries.emplace_back(p);FreeEnvironmentStringsW(block);
    entries.push_back(L"ARK_NATIVE_THREADS="+std::to_wstring(nativeThreadOverride));std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
    std::vector<wchar_t> result;for(const auto& entry:entries){result.insert(result.end(),entry.begin(),entry.end());result.push_back(0);}result.push_back(0);return result;
}
std::wstring quote(const std::wstring& value){std::wstring out=L"\"";size_t slash=0;for(auto c:value){if(c==L'\\'){++slash;continue;}out.append(c==L'\"'?slash*2+1:slash,L'\\');out+=c;slash=0;}out.append(slash*2,L'\\');return out+L"\"";}
unsigned execute(const fs::path& worker,const Temporary& temp){
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    Handle output{CreateFileW((temp.directory/"worker.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
    Handle input{CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)},job{CreateJobObjectW(nullptr,nullptr)};
    if(output.value==INVALID_HANDLE_VALUE||input.value==INVALID_HANDLE_VALUE||!job.value)throw std::runtime_error("Cannot initialize native worker handles");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof limits))throw std::runtime_error("Cannot bound native worker lifetime");
    STARTUPINFOW startup{};startup.cb=sizeof startup;startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdInput=input.value;startup.hStdOutput=output.value;startup.hStdError=output.value;
    std::wstring command=quote(worker.wstring())+L" "+quote((temp.directory/"request.json").wstring())+L" "+quote((temp.directory/"result.json").wstring());PROCESS_INFORMATION info{};
    auto environment=workerEnvironment();if(!CreateProcessW(worker.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT,environment.empty()?nullptr:environment.data(),worker.parent_path().c_str(),&startup,&info))throw std::runtime_error("Cannot launch native OCCT worker");
    Handle process{info.hProcess},thread{info.hThread};if(!AssignProcessToJobObject(job.value,process.value)){TerminateProcess(process.value,1);throw std::runtime_error("Cannot assign native worker watchdog");}ResumeThread(thread.value);
    if(WaitForSingleObject(process.value,30000)!=WAIT_OBJECT_0){TerminateJobObject(job.value,1);WaitForSingleObject(process.value,5000);throw std::runtime_error("Native OCCT operation exceeded 30-second worker limit");}
    DWORD code=1;GetExitCodeProcess(process.value,&code);return code;
}
// One isolated resident worker per calling thread. Named events carry only
// completion; request/result files retain the existing bounded JSON protocol.
// Closing the job kills the worker, including on a timeout or host shutdown.
class ResidentWorker {
    Temporary temp;
    Handle output,input,job,process,requestEvent,doneEvent;
    fs::path executable;
    fs::file_time_type executableTime{};
    std::string threadSetting;
    unsigned requests=0;
    int launchBudget=0;
public:
    bool matches(const fs::path& path)const{const char* value=std::getenv("ARK_NATIVE_THREADS");return launchBudget==effectiveNativeBudget()&&requests<256&&threadSetting==(value?value:"")&&executable==path&&executableTime==fs::last_write_time(path)&&process.value&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT;}
    explicit ResidentWorker(const fs::path& worker):executable(worker),executableTime(fs::last_write_time(worker)){
        const char* value=std::getenv("ARK_NATIVE_THREADS");threadSetting=value?value:"";
        launchBudget=effectiveNativeBudget();
        const auto prefix=L"Local\\"+temp.directory.filename().wstring();
        requestEvent.value=CreateEventW(nullptr,FALSE,FALSE,(prefix+L"-request").c_str());doneEvent.value=CreateEventW(nullptr,FALSE,FALSE,(prefix+L"-done").c_str());
        SECURITY_ATTRIBUTES security{sizeof security,nullptr,TRUE};
        output.value=CreateFileW((temp.directory/"worker.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        input.value=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);job.value=CreateJobObjectW(nullptr,nullptr);
        if(!requestEvent.value||!doneEvent.value||!job.value||output.value==INVALID_HANDLE_VALUE||input.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot initialize resident worker");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;if(!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof limits))throw std::runtime_error("Cannot bound resident worker");
        STARTUPINFOW startup{};startup.cb=sizeof startup;startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdInput=input.value;startup.hStdOutput=output.value;startup.hStdError=output.value;
        std::wstring command=quote(worker.wstring())+L" --server "+quote(temp.directory.wstring());PROCESS_INFORMATION info{};
        auto environment=workerEnvironment();if(!CreateProcessW(worker.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT,environment.empty()?nullptr:environment.data(),worker.parent_path().c_str(),&startup,&info))throw std::runtime_error("Cannot launch resident worker");
        process.value=info.hProcess;Handle thread{info.hThread};if(!AssignProcessToJobObject(job.value,process.value)){TerminateProcess(process.value,1);throw std::runtime_error("Cannot assign resident worker watchdog");}ResumeThread(thread.value);
    }
    ~ResidentWorker(){if(job.value&&process.value){TerminateJobObject(job.value,0);WaitForSingleObject(process.value,5000);}}
    V call(const V& request){
        ++requests;
        // Truncate the diagnostic log while idle so resident operation is bounded.
        SetFilePointer(output.value,0,nullptr,FILE_BEGIN);SetEndOfFile(output.value);
        {std::ofstream file(temp.directory/"request.json",std::ios::binary|std::ios::trunc);file<<request.dump();if(!file)throw std::runtime_error("Cannot write resident request");}
        std::error_code ec;fs::remove(temp.directory/"result.json",ec);ResetEvent(doneEvent.value);if(!SetEvent(requestEvent.value))throw std::runtime_error("Cannot signal resident worker");
        HANDLE handles[]{doneEvent.value,process.value};DWORD wait=WaitForMultipleObjects(2,handles,FALSE,30000);
        if(wait!=WAIT_OBJECT_0){TerminateJobObject(job.value,1);WaitForSingleObject(process.value,5000);throw std::runtime_error(wait==WAIT_TIMEOUT?"Native OCCT operation exceeded 30-second worker limit":"Resident OCCT worker exited");}
        V result;if(!arkjson::parse(read(temp.directory/"result.json"),result)||result.t!=V::Obj)throw std::runtime_error("Invalid resident worker result");
        if(auto error=result.find("error"))throw std::runtime_error("OCCT: "+error->asStr());return result;
    }
};
#else
unsigned execute(const fs::path& worker,const Temporary& temp){
    auto request=(temp.directory/"request.json").string(),response=(temp.directory/"result.json").string(),binary=worker.string();auto log=temp.directory/"worker.log";
    pid_t pid=fork();if(pid<0)throw std::runtime_error("Cannot fork native worker");
    if(pid==0){setsid();int fd=open(log.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);if(fd>=0){dup2(fd,STDOUT_FILENO);dup2(fd,STDERR_FILENO);close(fd);}execl(binary.c_str(),binary.c_str(),request.c_str(),response.c_str(),(char*)nullptr);_exit(127);}
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);int status=0;
    for(;;){auto waited=waitpid(pid,&status,WNOHANG);if(waited==pid)break;
        if(waited<0&&errno!=EINTR){kill(-pid,SIGKILL);kill(pid,SIGKILL);throw std::runtime_error("Cannot collect native worker exit status");}
        if(std::chrono::steady_clock::now()>end){kill(-pid,SIGKILL);kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){}throw std::runtime_error("Native OCCT operation exceeded 30-second worker limit");}
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }return WIFEXITED(status)?(unsigned)WEXITSTATUS(status):255;
}
#endif
std::string stepPath(const std::string& value){auto path=fs::absolute(utf8path(value));auto extension=path.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return(char)std::tolower(c);});if(extension!=".step"&&extension!=".stp")throw std::runtime_error("STEP output must use .step or .stp");return utf8(path);}
}
V nativeWorkerRequest(const V& request){
    ++nativeRequests;++totalNativeRequests;
#ifdef _WIN32
    if(const auto mode=std::getenv("ARK_NATIVE_ONESHOT");!mode||std::string(mode)!="1"){
        static thread_local std::unique_ptr<ResidentWorker> resident;
        auto worker=workerPath();if(!resident||!resident->matches(worker)){resident.reset();resident=std::make_unique<ResidentWorker>(worker);++nativeStarts;++totalNativeStarts;}
        try{return resident->call(request);}catch(...){resident.reset();throw;}
    }
#endif
    auto worker=workerPath();Temporary temp;{std::ofstream file(temp.directory/"request.json",std::ios::binary);file<<request.dump();if(!file)throw std::runtime_error("Cannot write native request");}
    ++nativeStarts;++totalNativeStarts;unsigned code=execute(worker,temp);V response;bool parsed=arkjson::parse(read(temp.directory/"result.json"),response)&&response.t==V::Obj;
    if(code||!parsed||response.find("error")){
        std::string error=parsed&&response.find("error")?response.find("error")->asStr():read(temp.directory/"worker.log");if(error.empty())error="Native worker failed (exit "+std::to_string(code)+")";if(error.size()>1200)error=error.substr(error.size()-1200);throw std::runtime_error("OCCT: "+error);
    }return response;
}
int setNativeThreadBudget(int threads){if(threads<0||threads>64)throw std::runtime_error("Native thread budget must be 0..64");int old=nativeThreadOverride;nativeThreadOverride=threads;return old;}
V nativeModelRequest(const V& model){V request=V::makeObj();request.set("operation",V::makeStr("model"));request.set("request",model);return nativeWorkerRequest(request);}
V nativeKernelInfo(){V request=V::makeObj();request.set("operation",V::makeStr("kernel_info"));V result=nativeWorkerRequest(request);result.set("worker_requests",V::makeNum((double)nativeRequests));result.set("worker_process_starts",V::makeNum((double)nativeStarts));result.set("total_worker_requests",V::makeNum((double)totalNativeRequests.load()));result.set("total_worker_process_starts",V::makeNum((double)totalNativeStarts.load()));
#ifdef _WIN32
const char* mode=std::getenv("ARK_NATIVE_ONESHOT");result.set("worker_transport",V::makeStr(mode&&std::string(mode)=="1"?"one_shot":"resident_events"));
#else
result.set("worker_transport",V::makeStr("one_shot"));
#endif
return result;}
V exportNativeBrep(const std::string& brep,const std::string& path){V request=V::makeObj();request.set("operation",V::makeStr("export_brep"));request.set("brep_base64",V::makeStr(brep));request.set("path",V::makeStr(stepPath(path)));return nativeWorkerRequest(request);}
V exportNotchStep(const V& request,const std::string& path){V work=V::makeObj();work.set("operation",V::makeStr("export_notch"));work.set("plan",planNotchedBlank(request));work.set("path",V::makeStr(stepPath(path)));return nativeWorkerRequest(work);}
V importSourceTubeStep(const std::string& path){
    auto source=fs::absolute(utf8path(path));if(!fs::exists(source))throw std::runtime_error("Source STEP does not exist");static std::atomic<unsigned long long> serial{0};auto cache=projectRoot()/"out/native-cache"/(std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial++));fs::create_directories(cache);
    V request=V::makeObj();request.set("operation",V::makeStr("import_tube"));request.set("source_path",V::makeStr(utf8(source)));request.set("cache_directory",V::makeStr(utf8(cache)));return nativeWorkerRequest(request);
}
V exportSourceTubeStep(const V& package,const std::string& path){V request=V::makeObj();request.set("operation",V::makeStr("export_tube"));request.set("package",package);request.set("path",V::makeStr(stepPath(path)));return nativeWorkerRequest(request);}
Mesh nativeResultMesh(const V& result){
    auto source=result.find("mesh");if(!source)throw std::runtime_error("Native result has no mesh");auto pos=source->find("pos"),idx=source->find("idx");if(!pos||!idx||pos->t!=V::Arr||idx->t!=V::Arr||pos->arr.empty()||idx->arr.empty()||pos->arr.size()%3||idx->arr.size()%3||pos->arr.size()>6000000||idx->arr.size()>6000000)throw std::runtime_error("Invalid native mesh buffers");
    Mesh mesh;for(auto& value:pos->arr){if(value.t!=V::Num||!std::isfinite(value.num)||std::abs(value.num)>1e8)throw std::runtime_error("Invalid native mesh coordinate");mesh.pos.push_back((f32)value.num);}for(auto& value:idx->arr){if(value.t!=V::Num||value.num<0||value.num!=std::floor(value.num)||value.num>=mesh.vertCount())throw std::runtime_error("Invalid native mesh index");mesh.idx.push_back((u32)value.num);}mesh.computeNormals();return mesh;
}
}
