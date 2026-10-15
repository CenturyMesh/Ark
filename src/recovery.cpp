#include "ark/recovery.hpp"
#include "ark/document_container.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
namespace fs=std::filesystem;
fs::path path(const std::string& s){return fs::path(reinterpret_cast<const char8_t*>(s.c_str()));}
std::string utf8(const fs::path& p){auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
std::string identity(const std::string& s){
    if(s.empty())return {};
    auto p=fs::weakly_canonical(fs::absolute(path(s)));
#ifdef _WIN32
    auto text=p.native();std::transform(text.begin(),text.end(),text.begin(),[](wchar_t c){return (wchar_t)std::towlower(c);});p=fs::path(text);
#endif
    return utf8(p);
}
std::string utc(){auto now=std::time(nullptr);std::tm value{};
#ifdef _WIN32
    gmtime_s(&value,&now);
#else
    gmtime_r(&now,&value);
#endif
    std::ostringstream out;out<<std::put_time(&value,"%Y-%m-%dT%H:%M:%SZ");return out.str();
}
const V& field(const V& value,const char* name,V::Type type){auto result=value.find(name);if(!result||result->t!=type)throw std::runtime_error("Invalid recovery metadata");return *result;}
void validWriter(const std::string& token){if(token.empty()||token.size()>128||token.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid recovery writer token");}
}
std::string recoveryWriterToken(){static const std::string token=[](){std::random_device random;std::ostringstream out;out<<std::hex<<std::chrono::steady_clock::now().time_since_epoch().count()<<'-';for(int i=0;i<4;++i)out<<std::setw(8)<<std::setfill('0')<<random();return out.str();}();return token;}
std::string recoveryFilePath(const std::string& directory,const std::string& sourcePath,const std::string& writerToken){validWriter(writerToken);return utf8(path(directory)/(iterationSha256(identity(sourcePath))+"-"+writerToken+".ark-recovery"));}
std::vector<std::string> recoveryCandidates(const std::string& directory,const std::string& sourcePath,std::string* error){
    try{
        std::vector<std::string> result;auto root=path(directory);if(!fs::exists(root))return result;auto prefix=iterationSha256(identity(sourcePath));
        for(const auto& item:fs::directory_iterator(root)){if(!item.is_regular_file()||item.is_symlink())continue;auto filename=utf8(item.path().filename());if(filename==prefix+".ark-recovery"||(filename.starts_with(prefix+"-")&&filename.ends_with(".ark-recovery")))result.push_back(utf8(item.path()));}
        std::sort(result.begin(),result.end());if(error)error->clear();return result;
    }catch(const std::exception& ex){if(error)*error=ex.what();return {};}
}
bool saveRecovery(const Document& document,const char* recoveryPath,const std::string& sourcePath,uint64_t revision,std::string* error){
    return saveRecovery(document,recoveryPath,sourcePath,revision,recoveryWriterToken(),error);
}
bool saveRecovery(const Document& document,const char* recoveryPath,const std::string& sourcePath,uint64_t revision,const std::string& writerToken,std::string* error){
    try{
        validWriter(writerToken);
        if(!recoveryPath||!*recoveryPath)throw std::runtime_error("Empty recovery path");
        auto source=identity(sourcePath),destination=identity(recoveryPath);
        if(!source.empty()&&(source==destination||(fs::exists(path(source))&&fs::exists(path(destination))&&fs::equivalent(path(source),path(destination)))))throw std::runtime_error("Recovery cannot replace the source document");
        V root;if(!arkjson::parse(documentToJson(document),root)||root.t!=V::Obj)throw std::runtime_error("Cannot serialize recovery document");
        // Decimal string avoids rounding the revision through a JSON double.
        V metadata=V::makeObj();metadata.set("schema",V::makeStr("ark.recovery.v1"));metadata.set("source_path",V::makeStr(source));metadata.set("saved_utc",V::makeStr(utc()));metadata.set("revision",V::makeStr(std::to_string(revision)));metadata.set("writer_token",V::makeStr(writerToken));root.set("_ark_recovery",std::move(metadata));
        // Refuse to repurpose any existing ordinary document as an autosave.
        if(fs::exists(path(destination))){V prior;std::string why;if(!loadDocumentContainer(recoveryPath,prior,&why)||!prior.find("_ark_recovery")||field(*prior.find("_ark_recovery"),"schema",V::Str).str!="ark.recovery.v1"||field(*prior.find("_ark_recovery"),"source_path",V::Str).str!=source)throw std::runtime_error("Destination is not recovery for this source");if(field(*prior.find("_ark_recovery"),"writer_token",V::Str).str!=writerToken)throw std::runtime_error("Recovery belongs to another writer session");}
        return saveDocumentContainer(root,recoveryPath,error);
    }catch(const std::exception& ex){if(error)*error=ex.what();return false;}
}
bool loadRecovery(const char* recoveryPath,Document& candidate,RecoveryInfo& info,std::string* error){
    try{
        V root;if(!loadDocumentContainer(recoveryPath,root,error))return false;
        const auto& metadata=field(root,"_ark_recovery",V::Obj);if(field(metadata,"schema",V::Str).str!="ark.recovery.v1")throw std::runtime_error("Unsupported recovery schema");
        RecoveryInfo next;next.sourcePath=field(metadata,"source_path",V::Str).str;next.savedUtc=field(metadata,"saved_utc",V::Str).str;next.writerToken=field(metadata,"writer_token",V::Str).str;validWriter(next.writerToken);auto number=field(metadata,"revision",V::Str).str;
        if(number.empty()||number.size()>20||number.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid recovery revision");size_t used=0;next.revision=std::stoull(number,&used);if(used!=number.size())throw std::runtime_error("Invalid recovery revision");
        root.obj.erase(std::remove_if(root.obj.begin(),root.obj.end(),[](const auto& entry){return entry.first=="_ark_recovery";}),root.obj.end());
        Document restored;if(!documentFromJson(root.dump(),restored,error))return false;
        candidate=std::move(restored);info=std::move(next);return true;
    }catch(const std::exception& ex){if(error)*error=ex.what();return false;}
}
} // namespace ark
