#include "manufacturing_internal.hpp"
#include "ark/io.hpp"
#include "ark/native_backend.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_ZLIB
#define STBI_SUPPORT_ZLIB
#define STBI_NO_STDIO
#include "../third_party/stb/stb_image.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#endif
namespace ark::mfg {
namespace fs=std::filesystem;
namespace {
std::string bytes(const fs::path& path,size_t limit){std::ifstream f(path,std::ios::binary|std::ios::ate);require((bool)f,"cannot open file: "+path.string());auto n=f.tellg();require(n>=0&&(uint64_t)n<=limit,"input file exceeds capacity");std::string data((size_t)n,'\0');f.seekg(0);f.read(data.data(),(std::streamsize)data.size());require((bool)f||data.empty(),"incomplete file read");return data;}
uint32_t crc32(const std::string& value){uint32_t crc=~0u;for(unsigned char b:value){crc^=b;for(int j=0;j<8;++j)crc=(crc>>1)^((0u-(crc&1))&0xedb88320u);}return ~crc;}
uint32_t integer(const std::string& v,size_t at,size_t n){require(at<=v.size()&&n<=v.size()-at,"truncated ZIP structure");uint32_t x=0;for(size_t i=0;i<n;++i)x|=(uint32_t)(unsigned char)v[at+i]<<(i*8);return x;}
int run(const fs::path& exe,const std::vector<std::string>& args,const fs::path& log){auto control=captureNativeRequestControl();
#ifdef _WIN32
    auto wide=[](const std::string& s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);require(n>0,"invalid UTF-8 process argument");std::wstring w(n,L' ');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),w.data(),n);return w;};
    auto quote=[](const std::wstring& s){std::wstring out=L"\"";size_t slashes=0;for(wchar_t c:s){if(c==L'\\'){++slashes;continue;}if(c==L'\"')out.append(slashes*2+1,L'\\');else out.append(slashes,L'\\');slashes=0;out+=c;}out.append(slashes*2,L'\\');out+=L'\"';return out;};
    std::wstring command=quote(exe.wstring());for(const auto& arg:args)command+=L" "+quote(wide(arg));
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE output=CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);require(output!=INVALID_HANDLE_VALUE,"cannot create slicer log");
    HANDLE job=CreateJobObjectW(nullptr,nullptr);if(!job){CloseHandle(output);require(false,"cannot create slicer job");}JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits));
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;si.hStdOutput=si.hStdError=output;si.hStdInput=nullptr;PROCESS_INFORMATION pi{};
    bool created=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,exe.parent_path().c_str(),&si,&pi);CloseHandle(output);if(!created){CloseHandle(job);require(false,"Bambu Studio process creation failed");}
    if(!AssignProcessToJobObject(job,pi.hProcess)){TerminateProcess(pi.hProcess,1);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(job);require(false,"cannot contain slicer process");}ResumeThread(pi.hThread);CloseHandle(pi.hThread);DWORD wait=WAIT_TIMEOUT,code=1;auto started=std::chrono::steady_clock::now();while(std::chrono::steady_clock::now()-started<std::chrono::seconds(180)&&!(control.cancelled&&control.cancelled())){wait=WaitForSingleObject(pi.hProcess,50);if(wait!=WAIT_TIMEOUT)break;}if(wait==WAIT_OBJECT_0)GetExitCodeProcess(pi.hProcess,&code);else TerminateJobObject(job,1);CloseHandle(pi.hProcess);CloseHandle(job);require(wait==WAIT_OBJECT_0,"slicer cancelled or exceeded 180 seconds; failed artifacts retained");return (int)code;
#else
    std::vector<std::string> values{exe.string()};values.insert(values.end(),args.begin(),args.end());std::vector<char*> argv;for(auto& s:values)argv.push_back(s.data());argv.push_back(nullptr);
    pid_t pid=fork();require(pid>=0,"cannot fork slicer");if(pid==0){setpgid(0,0);int fd=open(log.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)_exit(126);dup2(fd,STDOUT_FILENO);dup2(fd,STDERR_FILENO);close(fd);execv(exe.c_str(),argv.data());_exit(127);}setpgid(pid,pid);int status=0;auto start=std::chrono::steady_clock::now();for(;;){auto r=waitpid(pid,&status,WNOHANG);if(r==pid)break;if(r<0){kill(-pid,SIGKILL);require(false,"cannot wait for slicer");}if((control.cancelled&&control.cancelled())||std::chrono::steady_clock::now()-start>std::chrono::seconds(180)){kill(-pid,SIGKILL);waitpid(pid,&status,0);require(false,"slicer cancelled or exceeded 180 seconds; failed artifacts retained");}std::this_thread::sleep_for(std::chrono::milliseconds(50));}return WIFEXITED(status)?WEXITSTATUS(status):128;
#endif
}
}
V runSlicerBridge(const Document& document,u64 revision,const V& args){
    Graph g(plan(document,text(args,"name")));require(text(g.root,"process")=="fdm","slicer requires FDM plan");const auto& cfg=g.data(text(args,"slicer"),"slicer"),&setup=g.data(text(args,"setup"),"setup"),&part=g.data(text(setup,"part"),"part");
    auto digest=sourceDigest(document,part);require(digest==text(part,"source_sha256"),"STALE_SOURCE: refresh source binding before slicing");auto exe=fs::absolute(fs::u8path(text(cfg,"executable",true,4096)));require(fs::is_regular_file(exe),"configured slicer executable is unavailable");
    V profiles=V::makeArr();std::vector<std::string> paths,profileBytes;for(auto key:{"machine_profile","process_profile","filament_profile"}){auto path=fs::absolute(fs::u8path(text(cfg,key,true,4096)));require(fs::is_regular_file(path)&&path.extension()==".json","explicit full slicer JSON profile required");auto data=bytes(path,4*1024*1024);V parsed;require(arkjson::parse(data,parsed)&&parsed.t==V::Obj,"invalid slicer profile JSON");auto utf=path.u8string();paths.emplace_back(utf.begin(),utf.end());profileBytes.push_back(data);V p=V::makeObj();p.set("role",V::makeStr(key));p.set("path",V::makeStr(paths.back()));p.set("sha256",V::makeStr(iterationSha256(data)));profiles.arr.push_back(p);}
    auto directory=fs::absolute(fs::u8path(text(args,"output_directory",true,4096)));require(!fs::exists(directory)&&fs::is_directory(directory.parent_path()),"slice destination must be a new directory under an existing parent");require(fs::create_directory(directory),"cannot reserve slice output directory");
    for(size_t i=0;i<paths.size();++i){auto snapshot=directory/(std::string(i==0?"machine":i==1?"process":"filament")+".json");std::ofstream f(snapshot,std::ios::binary);f<<profileBytes[i];f.close();require((bool)f,"cannot snapshot slicer profile");auto utf=snapshot.u8string();paths[i]=std::string(utf.begin(),utf.end());profiles.arr[i].set("input_path",V::makeStr(paths[i]));}
    auto source=directory/"source.stl",output=directory/"sliced.3mf";const auto* b=body(document,(u64)num(part,"feature"));require(exportSTL(source.string().c_str(),b->tessellate(document.tessOpt)),"cannot export source-derived STL");
    auto utf=[](const fs::path& p){auto s=p.u8string();return std::string(s.begin(),s.end());};std::vector<std::string> argv{"--slice","0","--debug","2","--load-settings",paths[0]+";"+paths[1],"--load-filaments",paths[2],"--outputdir",utf(directory),"--export-3mf","sliced.3mf",utf(source)};
    const int code=run(exe,argv,directory/"slicer.log");require(code==0,"slicer failed with exit "+std::to_string(code)+"; inspect slicer.log in output directory");require(fs::is_regular_file(output),"slicer succeeded without expected sliced.3mf");
    V result=V::makeObj();result.set("revision",V::makeNum(revision));result.set("source_sha256",V::makeStr(digest));result.set("mesh_sha256",V::makeStr(iterationSha256(bytes(source,128*1024*1024))));result.set("slicer_executable_sha256",V::makeStr(iterationSha256(bytes(exe,256*1024*1024))));result.set("profiles",profiles);auto library=exe.parent_path()/"BambuStudio.dll";if(fs::is_regular_file(library))result.set("slicer_library_sha256",V::makeStr(iterationSha256(bytes(library,256*1024*1024))));result.set("output",V::makeStr(utf(output)));result.set("output_sha256",V::makeStr(iterationSha256(bytes(output,256*1024*1024))));result.set("cad_unchanged",V::makeBool(true));result.set("next_action",V::makeStr("Import the selected plate G-code with manufacturing_plan action:import_gcode, review its graph update, preview/apply, then simulate."));return result;
}
}
namespace ark {
std::string readManufacturingProgram(const std::string& path,const std::string& selected){using namespace mfg;const auto file=std::filesystem::u8path(path);auto raw=bytes(file,256*1024*1024);auto extension=file.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return (char)std::tolower(c);});if(extension!=".3mf"&&extension!=".zip"){require(raw.size()<=64*1024*1024,"G-code exceeds 64 MiB program capacity");require(raw.find('\0')==std::string::npos,"binary input is not G-code");return raw;}
    require(raw.size()>=22,"truncated ZIP");size_t eocd=std::string::npos;const size_t first=raw.size()>65557?raw.size()-65557:0;for(size_t at=raw.size()-22;;--at){if(integer(raw,at,4)==0x06054b50&&at+22+integer(raw,at+20,2)==raw.size()){eocd=at;break;}if(at==first)break;}require(eocd!=std::string::npos,"ZIP end record missing");require(integer(raw,eocd+4,2)==0&&integer(raw,eocd+6,2)==0,"multi-disk ZIP unsupported");size_t count=integer(raw,eocd+10,2),at=integer(raw,eocd+16,4);require(count<65535&&at<eocd,"ZIP64 unsupported");std::string found;size_t matches=0;
    for(size_t i=0;i<count;++i){require(integer(raw,at,4)==0x02014b50,"invalid ZIP central directory");uint32_t flags=integer(raw,at+8,2),method=integer(raw,at+10,2),crc=integer(raw,at+16,4),packed=integer(raw,at+20,4),unpacked=integer(raw,at+24,4),nameLength=integer(raw,at+28,2),extra=integer(raw,at+30,2),comment=integer(raw,at+32,2),offset=integer(raw,at+42,4);require(at+46+nameLength+extra+comment<=raw.size(),"truncated ZIP entry");std::string name=raw.substr(at+46,nameLength);at+=46+nameLength+extra+comment;bool gcode=name.size()>=6&&name.substr(name.size()-6)==".gcode";if(!gcode||(!selected.empty()&&selected!=name))continue;require(++matches==1,"multiple plate G-codes: specify member explicitly");require(!(flags&1)&&(method==0||method==8),"encrypted or unsupported ZIP member");require(unpacked<=64*1024*1024&&packed<=64*1024*1024,"G-code member exceeds capacity");require(integer(raw,offset,4)==0x04034b50,"invalid ZIP local header");size_t begin=offset+30+integer(raw,offset+26,2)+integer(raw,offset+28,2);require(begin<=raw.size()&&packed<=raw.size()-begin,"truncated compressed member");if(method==0){require(packed==unpacked,"stored ZIP size mismatch");found=raw.substr(begin,packed);}else{found.resize(unpacked);int decoded=stbi_zlib_decode_noheader_buffer(found.data(),(int)unpacked,raw.data()+begin,(int)packed);require(decoded==(int)unpacked,"G-code deflate failed");}require(crc32(found)==crc,"G-code ZIP CRC mismatch");}
    require(matches==1,"selected G-code member not found");require(found.find('\0')==std::string::npos,"binary G-code member rejected");return found;
}
}
