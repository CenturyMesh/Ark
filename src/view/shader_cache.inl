// Optional driver-specific program binaries. Never stores drawing data.
// A corrupt/incompatible cache entry is a miss; GLSL remains authoritative.
#include "ark/iteration_history.hpp"
namespace studio_shader_cache {
using GetBinary = void (APIENTRY*)(GLuint,GLsizei,GLsizei*,GLenum*,void*);
using SetBinary = void (APIENTRY*)(GLuint,GLenum,const void*,GLsizei);
using Parameter = void (APIENTRY*)(GLuint,GLenum,GLint);
using DeleteShader = void (APIENTRY*)(GLuint);
using DeleteProgram = void (APIENTRY*)(GLuint);
using StringAt = const GLubyte* (APIENTRY*)(GLenum,GLuint);
static GetBinary getBinary=nullptr;
static SetBinary setBinary=nullptr;
static Parameter parameter=nullptr;
static DeleteShader deleteShader=nullptr;
static DeleteProgram deleteProgram=nullptr;
static bool initialized=false,available=false;
static unsigned hits=0,misses=0,rejected=0,writes=0;
static constexpr std::uintmax_t entryLimit=16u*1024u*1024u,folderLimit=128u*1024u*1024u;
static void* address(const char* name){
    auto p=wglGetProcAddress(name);auto n=reinterpret_cast<std::intptr_t>(p);
    return n==0||n==1||n==2||n==3||n==-1?nullptr:reinterpret_cast<void*>(p);
}
static void initialize(){
    if(initialized)return;initialized=true;
    deleteShader=reinterpret_cast<DeleteShader>(address("glDeleteShader"));
    deleteProgram=reinterpret_cast<DeleteProgram>(address("glDeleteProgram"));
    GLint major=0,minor=0;glGetIntegerv(0x821B,&major);glGetIntegerv(0x821C,&minor);
    bool supported=major>4||(major==4&&minor>=1);
    if(!supported)if(auto getStringAt=reinterpret_cast<StringAt>(address("glGetStringi"))){
        GLint count=0;glGetIntegerv(0x821D,&count);
        for(GLint i=0;i<count;++i){auto extension=getStringAt(GL_EXTENSIONS,i);
            if(extension&&std::strcmp(reinterpret_cast<const char*>(extension),"GL_ARB_get_program_binary")==0){supported=true;break;}}
    }
    if(!supported)return;
    getBinary=reinterpret_cast<GetBinary>(address("glGetProgramBinary"));
    setBinary=reinterpret_cast<SetBinary>(address("glProgramBinary"));
    parameter=reinterpret_cast<Parameter>(address("glProgramParameteri"));
    GLint formats=0;glGetIntegerv(0x87FE,&formats);
    available=formats>0&&getBinary&&setBinary&&parameter;
}
static std::filesystem::path directory(){
    if(const auto* overridePath=_wgetenv(L"ARK_SHADER_CACHE_DIR");overridePath&&*overridePath)return overridePath;
    if(g_headless)return {}; // Test runs must explicitly select isolated cache custody.
    const auto* local=_wgetenv(L"LOCALAPPDATA");
    return local&&*local?std::filesystem::path(local)/"ARK"/"cache"/"shaders-v1":std::filesystem::path{};
}
static std::string key(const char* vertex,const char* fragment){
    std::string source="ark.gl.program.v1";
    auto add=[&](const char* bytes){if(!bytes)bytes="";source+='\n';source+=std::to_string(std::strlen(bytes));source+=':';source+=bytes;};
    add(app_version());add(reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    add(reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    add(reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    add(reinterpret_cast<const char*>(glGetString(0x8B8C)));add(vertex);add(fragment);
    return iterationSha256(source);
}
static bool load(GLuint program,const std::string& identity){
    initialize();if(!available)return false;
    try{
        const auto folder=directory();if(folder.empty())return false;
        const auto file=folder/(identity+".glbin");std::error_code error;
        const auto size=std::filesystem::file_size(file,error);if(error){++misses;return false;}
        if(size<150||size>entryLimit+1024){++rejected;return false;}
        std::ifstream input(file,std::ios::binary);std::string raw(static_cast<size_t>(size),'\0');
        input.read(raw.data(),static_cast<std::streamsize>(raw.size()));
        if(!input||input.peek()!=std::char_traits<char>::eof()){++rejected;return false;}
        const auto newline=raw.find('\n');if(newline==std::string::npos||newline>512){++rejected;return false;}
        const std::string header=raw.substr(0,newline);
        arkjson::Value record;if(!arkjson::parse(header,record)){++rejected;return false;}
        const auto* schema=record.find("schema"),*savedKey=record.find("key"),*format=record.find("format"),*digest=record.find("sha256");
        if(!schema||schema->asNum()!=1||!savedKey||savedKey->asStr()!=identity||!format||format->t!=arkjson::Value::Num||!std::isfinite(format->num)||format->num<0||format->num>4294967295.||std::floor(format->num)!=format->num||!digest||digest->t!=arkjson::Value::Str){++rejected;return false;}
        std::string binary=raw.substr(newline+1);
        if(binary.empty()||binary.size()>entryLimit||iterationSha256(binary)!=digest->str){++rejected;return false;}
        // Only formats advertised by this driver may be submitted back to it.
        GLint count=0;glGetIntegerv(0x87FE,&count);if(count<1||count>128){++rejected;return false;}
        std::vector<GLint> formats(count);glGetIntegerv(0x87FF,formats.data());
        if(std::none_of(formats.begin(),formats.end(),[&](GLint f){return static_cast<GLenum>(f)==static_cast<GLenum>(format->num);})){++rejected;return false;}
        setBinary(program,static_cast<GLenum>(format->num),binary.data(),static_cast<GLsizei>(binary.size()));
        GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
        if(!linked){++rejected;return false;}++hits;return true;
    }catch(...){++rejected;return false;}
}
static void store(GLuint program,const std::string& identity){
    if(!available)return;
    std::filesystem::path temporary;
    try{
        const auto folder=directory();if(folder.empty())return;
        GLint size=0;glGetProgramiv(program,0x8741,&size);if(size<=0||size>static_cast<GLint>(entryLimit))return;
        std::string binary(static_cast<size_t>(size),'\0');GLsizei written=0;GLenum format=0;
        getBinary(program,size,&written,&format,binary.data());if(written<=0||written>size)return;binary.resize(written);
        arkjson::Value header=arkjson::Value::makeObj();header.set("schema",arkjson::Value::makeNum(1));
        header.set("key",arkjson::Value::makeStr(identity));header.set("format",arkjson::Value::makeNum(format));header.set("sha256",arkjson::Value::makeStr(iterationSha256(binary)));
        const std::string bytes=header.dump()+"\n"+binary;
        std::filesystem::create_directories(folder);
        // A cache write never waits on another Studio. The short-lived exclusive
        // file makes the scan + publication bound hold across concurrent launches.
        struct FolderLock{HANDLE handle=INVALID_HANDLE_VALUE;~FolderLock(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}} lock;
        const auto lockPath=folder/L".write-lock";
        lock.handle=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        if(lock.handle==INVALID_HANDLE_VALUE)return;
        const auto destination=folder/(identity+".glbin");
        std::uintmax_t used=0;unsigned files=0;
        for(const auto& entry:std::filesystem::directory_iterator(folder)){
            if(entry.path()==destination||entry.path()==lockPath)continue;
            if(++files>=512)return;if(entry.is_regular_file())used+=entry.file_size();if(used+bytes.size()>folderLimit)return;
        }
        temporary=folder/(identity+"."+std::to_string(GetCurrentProcessId())+"."+std::to_string(GetTickCount64())+".tmp");
        HANDLE output=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(output==INVALID_HANDLE_VALUE){temporary.clear();return;}
        DWORD count=0;const bool complete=WriteFile(output,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size();
        CloseHandle(output);
        if(complete&&MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){temporary.clear();++writes;}
    }catch(...){}
    if(!temporary.empty()){std::error_code ignored;std::filesystem::remove(temporary,ignored);}
}
static void diagnostic(const char* kind,const char* log){
    std::string message=std::string("ARK ")+kind+": "+log+"\n";
    std::fprintf(stderr,"%s",message.c_str());OutputDebugStringA(message.c_str());
}
static void report(){
    std::fprintf(stdout,"shader_cache available=%u hits=%u misses=%u rejected=%u writes=%u\n",available?1:0,hits,misses,rejected,writes);
}
}
