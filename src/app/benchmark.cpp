#include "ark/agent_benchmark.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace {
using V=arkjson::Value;
V read(const std::string& path){std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Cannot read "+path);std::ostringstream text;text<<stream.rdbuf();V result;if(!arkjson::parse(text.str(),result))throw std::runtime_error("Invalid JSON: "+path);return result;}
std::vector<std::string> split(const std::string& text){std::vector<std::string> result;std::istringstream stream(text);std::string item;while(std::getline(stream,item,',')){if(item.empty())throw std::runtime_error("Empty list option");result.push_back(item);}return result;}
unsigned positive(const std::string& text){if(text.empty()||text.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Expected a positive integer");auto n=std::stoul(text);if(n<1||n>10000)throw std::runtime_error("Integer out of range");return (unsigned)n;}
std::filesystem::path executableDirectory(const char* argv0){
#ifdef _WIN32
    std::wstring buffer(32768,L'\0');const auto length=GetModuleFileNameW(nullptr,buffer.data(),(DWORD)buffer.size());if(length>0&&length<buffer.size()){buffer.resize(length);return std::filesystem::path(buffer).parent_path();}
#elif defined(__linux__)
    std::string buffer(32768,'\0');const auto length=readlink("/proc/self/exe",buffer.data(),buffer.size());if(length>0&&static_cast<std::size_t>(length)<buffer.size()){buffer.resize((std::size_t)length);return std::filesystem::path(buffer).parent_path();}
#endif
    std::error_code error;auto executable=std::filesystem::absolute(argv0,error);return error?std::filesystem::path{}:executable.parent_path();
}
std::string defaultResource(const std::filesystem::path& executableDir,const std::string& relative){std::error_code error;const auto bundled=executableDir/std::filesystem::path(relative);if(!executableDir.empty()&&std::filesystem::is_regular_file(bundled,error))return bundled.string();return relative;}
}
int main(int argc,char** argv){try{
    const auto executableDir=executableDirectory(argv[0]);
    std::string suitePath=defaultResource(executableDir,"benchmarks/agent_briefs_v1.json"),configPath=defaultResource(executableDir,"benchmarks/offline.json"),outPath;bool live=false,preflight=false,vision=false;std::vector<std::string> ids,strategies;unsigned repetitions=0,deadline=0;
    for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--live")live=true;else if(arg=="--preflight")preflight=true;else if(arg=="--vision-smoke")vision=true;else if(arg=="--help"){std::cout<<"ark-benchmark [--suite FILE] [--config FILE] [--out JSONL] [--cases B01,P01,A01] [--strategies Single,Focused,Parallel,PartFocused] [--repetitions N] [--deadline-seconds N] [--preflight] [--vision-smoke] [--live]\nDefault: offline scripted native harness; --live explicitly enables the configured provider. No settings or user drawings are loaded.\nDefault suite/config/vision fixture resolve beside the executable first, then the development working directory. Explicit --suite/--config and --out paths are used exactly as supplied.\n";return 0;}else {if(i+1==argc)throw std::runtime_error("Missing value for "+arg);std::string value=argv[++i];if(arg=="--suite")suitePath=value;else if(arg=="--config")configPath=value;else if(arg=="--out")outPath=value;else if(arg=="--cases")ids=split(value);else if(arg=="--strategies")strategies=split(value);else if(arg=="--repetitions")repetitions=positive(value);else if(arg=="--deadline-seconds")deadline=positive(value);else throw std::runtime_error("Unknown option: "+arg);}}
    auto suite=ark::parseAgentBenchmarkSuite(read(suitePath));std::ofstream out;if(!outPath.empty()){auto parent=std::filesystem::path(outPath).parent_path();if(!parent.empty())std::filesystem::create_directories(parent);out.open(outPath,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot create "+outPath);}
    auto write=[&](const V& row){if(out.is_open()){out<<row.dump()<<'\n';out.flush();if(!out)throw std::runtime_error("Cannot persist benchmark receipt: "+outPath);}};
    if(preflight){auto report=ark::preflightAgentBenchmark(suite);write(report);std::cout<<report.dump()<<'\n';return report.find("passed")->b?0:2;}
    auto config=ark::parseAgentBenchmarkConfig(read(configPath));config.caseIds=ids;if(!strategies.empty())config.strategies=strategies;if(repetitions)config.repetitions=repetitions;if(deadline)config.deadlineSeconds=deadline;
    if(live&&config.assistant.provider=="offline")throw std::runtime_error("--live requires an explicitly configured real provider");
    if(vision){auto fixture=read(defaultResource(executableDir,"examples/pneumatic-cylinder.arkops"));const auto* args=fixture.find("arguments");if(!args||!args->find("commands"))throw std::runtime_error("Invalid synthetic vision fixture");std::string imagePath=outPath.empty()?"":outPath+".png";auto result=ark::runAgentVisionSmoke(*args->find("commands"),config,live,imagePath);write(result);std::cout<<result.dump()<<'\n';return result.find("completed")->b&&(!live||result.find("pixels_in_completed_provider_turn")->b)?0:2;}
    auto result=ark::runAgentBenchmark(suite,config,live,[&](const V& row){write(row);std::cout<<row.find("case")->str<<" "<<row.find("strategy")->str<<" repetition "<<row.find("repetition")->num<<" accepted="<<row.find("accepted")->b<<"\n"<<std::flush;});
    V summary=result;summary.set("attempts",V{});write(summary);std::cout<<summary.dump()<<'\n';return result.find("failed_attempt_count")->num==0?0:2;
}catch(const std::exception& error){std::cerr<<"Benchmark: "<<error.what()<<'\n';return 1;}}
