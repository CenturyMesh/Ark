#include "ark/recovery.hpp"
#include "ark/document_container.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace fs=std::filesystem;
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
std::string utf8(const fs::path& path){auto s=path.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
std::string bytes(const fs::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
int main(){auto directory=fs::temp_directory_path()/("ark-recovery-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directories(directory);try{
    auto original=directory/"source.ark";ark::Document source;source.setParam("length","10");check(ark::saveDocument(source,utf8(original).c_str()),"source saves");auto intact=bytes(original);auto recovery=ark::recoveryFilePath(utf8(directory),utf8(original));std::string error;
    source.setParam("length","20");check(ark::saveRecovery(source,recovery.c_str(),utf8(original),std::numeric_limits<uint64_t>::max(),&error),error);check(bytes(original)==intact,"autosave preserves source bytes");
    ark::Document candidate;ark::RecoveryInfo info;check(ark::loadRecovery(recovery.c_str(),candidate,info,&error),error);check(candidate.getParam("length")==20&&info.revision==std::numeric_limits<uint64_t>::max()&&!info.savedUtc.empty()&&!info.sourcePath.empty(),"candidate and full-width revision restore");
    source.setParam("length","30");check(ark::saveRecovery(source,recovery.c_str(),utf8(original),12,&error)&&ark::loadRecovery(recovery.c_str(),candidate,info,&error)&&candidate.getParam("length")==30,"incremental recovery updates separate candidate");check(bytes(original)==intact,"updated recovery preserves original");
    auto writerA=ark::recoveryFilePath(utf8(directory),utf8(original),"writer-A"),writerB=ark::recoveryFilePath(utf8(directory),utf8(original),"writer-B");check(writerA!=writerB,"writers have distinct source recovery paths");
    source.setParam("length","41");check(ark::saveRecovery(source,writerA.c_str(),utf8(original),13,"writer-A",&error),error);source.setParam("length","42");check(ark::saveRecovery(source,writerB.c_str(),utf8(original),14,"writer-B",&error),error);auto bytesA=bytes(fs::path(reinterpret_cast<const char8_t*>(writerA.c_str())));
    check(!ark::saveRecovery(source,writerA.c_str(),utf8(original),15,"writer-B",&error)&&bytes(fs::path(reinterpret_cast<const char8_t*>(writerA.c_str())))==bytesA,"another writer cannot overwrite an existing recovery");
    auto candidates=ark::recoveryCandidates(utf8(directory),utf8(original),&error);check(error.empty()&&candidates.size()==3,"source discovery retains every separate session candidate");check(ark::loadRecovery(writerA.c_str(),candidate,info,&error)&&info.writerToken=="writer-A"&&candidate.getParam("length")==41,"earlier writer candidate survives another writer save");
    auto untitledA=ark::recoveryFilePath(utf8(directory),"","untitled-A"),untitledB=ark::recoveryFilePath(utf8(directory),"","untitled-B");check(untitledA!=untitledB&&ark::saveRecovery(source,untitledA.c_str(),"",1,"untitled-A",&error)&&ark::saveRecovery(source,untitledB.c_str(),"",2,"untitled-B",&error),"simultaneous untitled sessions retain separate recoveries");check(ark::recoveryCandidates(utf8(directory),"",&error).size()==2,"untitled discovery finds both independent sessions");
    check(!ark::saveRecovery(source,utf8(original).c_str(),utf8(original),13,&error)&&bytes(original)==intact,"same source destination rejects");
    auto disguised=directory/"ordinary.ark-recovery";check(ark::saveDocument(source,utf8(disguised).c_str()),"ordinary document saves");auto ordinary=bytes(disguised);check(!ark::saveRecovery(source,utf8(disguised).c_str(),utf8(original),13,&error)&&bytes(disguised)==ordinary,"ordinary document cannot be repurposed as autosave");
    std::error_code linkError;auto alias=directory/"alias.ark";fs::create_hard_link(original,alias,linkError);if(!linkError)check(!ark::saveRecovery(source,utf8(alias).c_str(),utf8(original),13,&error)&&bytes(original)==intact,"hard-link source alias rejects");
    auto stable=ark::documentToJson(candidate);auto savedInfo=info.sourcePath;fs::resize_file(fs::path(reinterpret_cast<const char8_t*>(recovery.c_str())),17);check(!ark::loadRecovery(recovery.c_str(),candidate,info,&error)&&ark::documentToJson(candidate)==stable&&info.sourcePath==savedInfo,"truncated recovery rejects atomically");check(bytes(original)==intact,"truncated recovery never changes source");
    auto unsaved=ark::recoveryFilePath(utf8(directory),"");check(ark::saveRecovery(source,unsaved.c_str(),"",0,&error)&&ark::loadRecovery(unsaved.c_str(),candidate,info,&error)&&info.sourcePath.empty(),"untitled recovery works without a source file");
    check(ark::recoveryFilePath(utf8(directory),utf8(directory/"."/"source.ark"))==recovery,"normalized source determines stable recovery key");
    fs::remove_all(directory);std::cout<<"recovery tests passed\n";return 0;
}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';fs::remove_all(directory);return 1;}}
