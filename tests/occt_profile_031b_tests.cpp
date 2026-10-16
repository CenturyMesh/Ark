#include "ark/occt_solid.hpp"
#include "../src/occt/common.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using V=arkjson::Value;namespace native=ark::occt;namespace fs=std::filesystem;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void environment(const std::string& value){
#ifdef _WIN32
    _putenv_s("ARK_NATIVE_PROFILE_DIRECTORY",value.c_str());
#else
    if(value.empty())unsetenv("ARK_NATIVE_PROFILE_DIRECTORY");else setenv("ARK_NATIVE_PROFILE_DIRECTORY",value.c_str(),1);
#endif
}
struct Fixture {
    fs::path directory;std::string previous;
    Fixture(){if(const char* old=std::getenv("ARK_NATIVE_PROFILE_DIRECTORY"))previous=old;const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();directory=fs::temp_directory_path()/("ark-native-timing-test-"+std::to_string(stamp));check(fs::create_directory(directory),"create owned profile fixture directory");environment({});}
    ~Fixture(){environment(previous);std::error_code ignored;const auto temp=fs::temp_directory_path(ignored);if(!ignored&&directory.parent_path()==temp&&directory.filename().string().rfind("ark-native-timing-test-",0)==0)fs::remove_all(directory,ignored);}
};
V boxRequest(){V request=V::makeObj();request.set("op",V::makeStr("box"));request.set("width",V::makeNum(12));request.set("depth",V::makeNum(15));request.set("height",V::makeNum(8));return request;}
}
int main(){try{
    Fixture fixture;const auto baseline=native::modelSolidNative(boxRequest());
    {native::NativeProfileRequest disabled;disabled.operation("model_solid","box");native::NativeProfileScope scope("fixture.disabled");(void)native::modelSolidNative(boxRequest());disabled.succeeded();}
    check(fs::is_empty(fixture.directory),"profiling disabled creates no files");
    const auto text=fixture.directory.u8string();environment(std::string(text.begin(),text.end()));V measured;
    {native::NativeProfileRequest enabled;enabled.operation("private_request_marker","private_model_marker");native::NativeProfileScope scope("fixture.outer");{native::NativeProfileScope nested("fixture.inner");measured=native::modelSolidNative(boxRequest());}enabled.succeeded();}
    check(measured.find("brep_base64")->str==baseline.find("brep_base64")->str&&measured.find("report")->dump()==baseline.find("report")->dump(),"profiling preserves exact geometry and all acceptance report fields");
    std::vector<fs::path> files;for(const auto& file:fs::directory_iterator(fixture.directory))files.push_back(file.path());check(files.size()==1,"one opt-in request emits one timing file");const auto bytes=native::readFile(files.front());V trace;check(arkjson::parse(bytes,trace),"timing trace is valid JSON");
    const std::set<std::string> fields{"schema","operation","model_operation","success","elapsed_ms","phases"};for(const auto& field:trace.obj)check(fields.count(field.first)!=0,"trace contains only timing metadata fields");check(trace.find("success")->b&&trace.find("elapsed_ms")->num>0,"trace retains successful measured duration");check(trace.find("operation")->str=="other"&&trace.find("model_operation")->str=="other","unknown operation text is redacted to a fixed enum");check(bytes.find("private_request_marker")==std::string::npos&&bytes.find("private_model_marker")==std::string::npos&&bytes.find("brep_base64")==std::string::npos&&bytes.find("positions")==std::string::npos,"trace omits model contents and caller-controlled identifiers");
    bool nested=false,validation=false;for(const auto& phase:trace.find("phases")->arr){check(phase.obj.size()==4&&phase.find("phase")&&phase.find("calls")&&phase.find("inclusive_ms")&&phase.find("self_ms"),"phase rows contain fixed timing fields");check(phase.find("calls")->num>=1&&phase.find("self_ms")->num>=0&&phase.find("inclusive_ms")->num>=phase.find("self_ms")->num,"nested timing and call counts are internally consistent");if(phase.find("phase")->str=="fixture.outer"){nested=true;check(phase.find("inclusive_ms")->num>phase.find("self_ms")->num,"parent exclusive time subtracts nested phases");}validation|=phase.find("phase")->str=="report.validate";}
    check(nested&&validation,"timing covers nested dispatch and exact validity checks");
    {native::NativeProfileRequest failure;failure.operation("model_solid","box");native::NativeProfileScope scope("fixture.rejection");auto invalid=boxRequest();invalid.set("width",V::makeNum(-1));bool rejected=false;try{(void)native::modelSolidNative(invalid);}catch(const std::exception&){rejected=true;}check(rejected,"profiling preserves invalid geometry rejection");}
    size_t failures=0;for(const auto& file:fs::directory_iterator(fixture.directory)){V row;check(arkjson::parse(native::readFile(file.path()),row),"every emitted trace parses");if(!row.find("success")->b)++failures;}check(failures==1,"failed request has an explicit unsuccessful timing record");
    std::cout<<"Native timing: opt-in file isolation, private-data omission, nested self/inclusive counters, exact output parity and unchanged rejection passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
