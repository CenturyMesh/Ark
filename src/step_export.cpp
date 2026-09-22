#include "ark/step_export.hpp"
#include "ark/notch.hpp"
#include <filesystem>
#include <fstream>
#include <atomic>
#include <cstdlib>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace ark {
namespace fs=std::filesystem;using V=arkjson::Value;
namespace {
std::string read(const fs::path& path){std::ifstream f(path,std::ios::binary);return std::string((std::istreambuf_iterator<char>(f)),{});}
fs::path utf8path(const std::string& s){return fs::path(reinterpret_cast<const char8_t*>(s.c_str()));}
#ifdef _WIN32
struct Handle{HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Temporary {fs::path directory;~Temporary(){if(directory.empty())return;std::error_code ec;for(auto name:{L"plan.json",L"export.log"})fs::remove(directory/name,ec);fs::remove(directory,ec);}};
std::wstring quote(const std::wstring& s){
    std::wstring out=L"\"";size_t slashes=0;
    for(auto c:s){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){out.append(slashes*2+1,L'\\');out+=c;}else{out.append(slashes,L'\\');out+=c;}slashes=0;}
    out.append(slashes*2,L'\\');out+=L'\"';return out;
}
fs::path root(){
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);fs::path p=fs::path(executable).parent_path();
    for(int i=0;i<5&&!p.empty();++i,p=p.parent_path())if(fs::exists(p/L"scripts/export_notch_step.py"))return p;
    return fs::path(ARK_SOURCE_DIR);
}
fs::path python(const fs::path& directory){
    if(auto configured=_wgetenv(L"ARK_OCCT_PYTHON");configured&&*configured)return fs::path(configured);
    V settings;if(arkjson::parse(read(directory/L".ark-local.json"),settings))if(auto p=settings.find("occt_python");p&&p->t==V::Str)return utf8path(p->str);
    auto local=directory/L".venv-occt/Scripts/python.exe";if(fs::exists(local))return local;
    wchar_t found[32768]{};if(SearchPathW(nullptr,L"python.exe",nullptr,32768,found,nullptr))return fs::path(found);
    throw std::runtime_error("Configure ARK_OCCT_PYTHON or .ark-local.json with a Python containing the optional OCCT requirements");
}
void executeWorker(const V& inputData,const char* scriptName,const fs::path& destination){
    auto directory=root(),interpreter=python(directory),script=directory/"scripts"/scriptName;
    if(!fs::exists(interpreter)||!fs::exists(script))throw std::runtime_error("STEP backend unavailable. Configure the OCCT Python path and retain the OCCT bridge scripts");
    static std::atomic<unsigned> counter{0};Temporary temporary;
    for(int attempt=0;attempt<64;++attempt){auto candidate=fs::temp_directory_path()/(L"ark-step-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(counter++));if(fs::create_directory(candidate)){temporary.directory=candidate;break;}}
    if(temporary.directory.empty())throw std::runtime_error("Unable to create STEP export workspace");
    auto input=temporary.directory/L"plan.json",log=temporary.directory/L"export.log";
    {std::ofstream f(input,std::ios::binary);f<<inputData.dump();if(!f)throw std::runtime_error("Cannot write STEP plan");}
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    Handle output{CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
    Handle nullInput{CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    Handle job{CreateJobObjectW(nullptr,nullptr)};
    if(output.value==INVALID_HANDLE_VALUE||nullInput.value==INVALID_HANDLE_VALUE||!job.value)throw std::runtime_error("Cannot initialize STEP worker");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof limits))throw std::runtime_error("Cannot bound STEP worker lifetime");
    STARTUPINFOW startup{};startup.cb=sizeof startup;startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdInput=nullInput.value;startup.hStdOutput=output.value;startup.hStdError=output.value;
    std::wstring command=quote(interpreter.wstring())+L" "+quote(script.wstring())+L" "+quote(input.wstring())+L" "+quote(destination.wstring());
    PROCESS_INFORMATION info{};
    if(!CreateProcessW(interpreter.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,directory.c_str(),&startup,&info))throw std::runtime_error("Cannot launch configured STEP Python backend");
    Handle process{info.hProcess},thread{info.hThread};
    if(!AssignProcessToJobObject(job.value,process.value)){TerminateProcess(process.value,1);throw std::runtime_error("Cannot attach STEP worker watchdog");}
    ResumeThread(thread.value);DWORD waited=WaitForSingleObject(process.value,30000);
    if(waited!=WAIT_OBJECT_0){TerminateJobObject(job.value,1);WaitForSingleObject(process.value,5000);throw std::runtime_error("OCCT operation exceeded the 30-second worker limit");}
    DWORD code=1;GetExitCodeProcess(process.value,&code);
    if(code){auto message=read(log);auto end=message.find_last_not_of("\r\n ");if(end!=std::string::npos)message.resize(end+1);auto line=message.find_last_of('\n');if(line!=std::string::npos)message=message.substr(line+1);if(message.size()>1000)message=message.substr(message.size()-1000);
        if(message.find("ModuleNotFoundError")!=std::string::npos)message+="; configure the OCCT Python requirements";
        throw std::runtime_error("OCCT worker: "+message);
    }
}
fs::path stepDestination(const std::string& path){
    if(path.empty())throw std::runtime_error("Empty STEP path");
    fs::path destination=fs::absolute(utf8path(path));auto extension=destination.extension().wstring();for(auto& c:extension)if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';if(extension!=L".step"&&extension!=L".stp")throw std::runtime_error("STEP output must use .step or .stp");
    return destination;
}
V receipt(const fs::path& destination,const std::string& path){
    auto file=destination;file.replace_extension(L".validation.json");V result;
    if(!arkjson::parse(read(file),result)||!result.find("brep_valid")||!result.find("brep_valid")->b)throw std::runtime_error("STEP backend returned no valid B-rep receipt");
    result.set("path",V::makeStr(path));result.set("export_pose",V::makeStr("straight"));return result;
}
#endif
}
V exportNotchStep(const V& request,const std::string& path){
    auto plan=planNotchedBlank(request);
#ifdef _WIN32
    auto destination=stepDestination(path);executeWorker(plan,"export_notch_step.py",destination);return receipt(destination,path);
#else
    (void)plan;(void)path;throw std::runtime_error("Integrated STEP worker requires Windows; use the standalone exporter on this platform");
#endif
}
V importSourceTubeStep(const std::string& path){
#ifdef _WIN32
    auto source=fs::absolute(utf8path(path));if(!fs::exists(source))throw std::runtime_error("Source STEP does not exist");
    static std::atomic<unsigned> serial{0};auto cache=root()/"out"/"source-cache"/(std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(serial++));fs::create_directories(cache);
    V request=V::makeObj();auto name=source.u8string();request.set("source_path",V::makeStr(std::string(name.begin(),name.end())));executeWorker(request,"source_tube.py",cache);
    V package;if(!arkjson::parse(read(cache/"package.json"),package))throw std::runtime_error("Source importer produced no verified package");return package;
#else
    (void)path;throw std::runtime_error("Integrated source STEP import requires the Windows OCCT worker");
#endif
}
V exportSourceTubeStep(const V& package,const std::string& path){
#ifdef _WIN32
    auto destination=stepDestination(path);V request=V::makeObj();request.set("operation",V::makeStr("export"));request.set("package",package);executeWorker(request,"source_tube.py",destination);return receipt(destination,path);
#else
    (void)package;(void)path;throw std::runtime_error("Integrated source STEP export requires the Windows OCCT worker");
#endif
}
}
