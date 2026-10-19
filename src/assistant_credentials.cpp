#include "ark/assistant_credentials.hpp"
#include "ark/assistant_connectors.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#endif
namespace ark {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
// Scheme and host (with port) of an https endpoint; the key never leaves this host.
std::string endpointHost(const std::string& endpoint){
    const std::string scheme="https://";
    if(endpoint.size()<=scheme.size()||endpoint.compare(0,scheme.size(),scheme)!=0)return {};
    auto host=endpoint.substr(scheme.size(),endpoint.find('/',scheme.size())==std::string::npos?std::string::npos:endpoint.find('/',scheme.size())-scheme.size());
    if(host.empty()||host.find('@')!=std::string::npos)return {};
    std::transform(host.begin(),host.end(),host.begin(),[](unsigned char c){return (char)std::tolower(c);});
    return scheme+host;
}
std::string binding(const AssistantConfig& config){
    const auto host=endpointHost(config.endpoint);
    require(!host.empty()&&!config.provider.empty(),"Saved API keys require a provider with an HTTPS endpoint.");
    return "ark-assistant-api-key-v1|"+config.provider+"|"+host;
}
void validateKey(const std::string& key){
    require(!key.empty()&&key.size()<=8192&&std::all_of(key.begin(),key.end(),[](unsigned char c){return c>=33&&c<=126;}),"Invalid API key: paste only the key, without spaces or line breaks.");
}
#ifdef _WIN32
std::filesystem::path storeDirectory(){
    if(const char* overridden=std::getenv("ARK_API_KEY_DIRECTORY");overridden&&*overridden)return std::filesystem::u8path(overridden);
    PWSTR root=nullptr;require(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&root)==S_OK&&root!=nullptr,"Cannot locate local Windows app data");
    std::filesystem::path path(root);CoTaskMemFree(root);return path/"ARK-Point"/"api-keys";
}
std::filesystem::path keyPath(const AssistantConfig& config){return storeDirectory()/(iterationSha256(binding(config)).substr(0,32)+".key");}
DATA_BLOB entropy(const std::string& value){return DATA_BLOB{(DWORD)value.size(),(BYTE*)value.data()};}
// A custom connector and the preset for the same host use one credential (a
// custom DeepSeek endpoint and the DeepSeek preset): reuse a key saved by the
// other instead of asking again. Distinct presets never share keys, and a key
// is still only ever sent to the host it was saved for.
AssistantConfig savedKeyRoute(const AssistantConfig& config){
    std::error_code error;if(std::filesystem::exists(keyPath(config),error))return config;
    const auto host=endpointHost(config.endpoint);std::vector<std::string> providers;
    if(config.provider=="custom"){for(const auto& preset:assistantProviderPresets())if(preset.id!="custom"&&endpointHost(preset.endpoint)==host)providers.push_back(preset.id);}
    else for(const auto& preset:assistantProviderPresets())if(preset.id==config.provider&&endpointHost(preset.endpoint)==host)providers.push_back("custom");
    for(const auto& provider:providers){auto alternative=config;alternative.provider=provider;if(std::filesystem::exists(keyPath(alternative),error))return alternative;}
    return config;
}
#endif
}
bool assistantApiKeyStoreSupported(){
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}
void storeAssistantApiKey(const AssistantConfig& config,const std::string& key){
    validateKey(key);
#ifdef _WIN32
    const auto bound=binding(config);const auto path=keyPath(config);std::filesystem::create_directories(path.parent_path());
    std::string plaintext=key;DATA_BLOB input{(DWORD)plaintext.size(),(BYTE*)plaintext.data()},encrypted{};auto salt=entropy(bound);
    const bool ok=CryptProtectData(&input,L"ARK assistant API key",&salt,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted)!=FALSE;
    SecureZeroMemory(plaintext.data(),plaintext.size());require(ok,"Could not protect the API key for this Windows user.");
    auto pending=path;pending+=L".pending";
    try{
        std::ofstream output(pending,std::ios::binary|std::ios::trunc);require((bool)output,"Could not save the API key.");
        output.write((const char*)encrypted.pbData,encrypted.cbData);output.flush();require((bool)output,"Could not save the API key.");output.close();
        require(MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"Could not commit the saved API key.");
    }catch(...){LocalFree(encrypted.pbData);std::error_code ignored;std::filesystem::remove(pending,ignored);throw;}
    LocalFree(encrypted.pbData);
#else
    (void)config;throw std::runtime_error("Saved API keys are available on Windows; use the API key environment variable here.");
#endif
}
std::string loadAssistantApiKey(const AssistantConfig& config){
#ifdef _WIN32
    if(endpointHost(config.endpoint).empty()||config.provider.empty())return {};
    const auto route=savedKeyRoute(config);const auto bound=binding(route);const auto path=keyPath(route);std::error_code error;
    if(!std::filesystem::exists(path,error))return {};
    const auto size=std::filesystem::file_size(path,error);require(!error&&size>0&&size<=64*1024,"Invalid saved API key file; save the key again.");
    std::ifstream input(path,std::ios::binary);require((bool)input,"Cannot read the saved API key.");
    std::string encrypted((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    DATA_BLOB blob{(DWORD)encrypted.size(),(BYTE*)encrypted.data()},plaintext{};auto salt=entropy(bound);
    require(CryptUnprotectData(&blob,nullptr,&salt,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plaintext)!=FALSE,"The saved API key is not available to this Windows user; save it again.");
    std::string key((char*)plaintext.pbData,plaintext.cbData);SecureZeroMemory(plaintext.pbData,plaintext.cbData);LocalFree(plaintext.pbData);
    try{validateKey(key);}catch(...){SecureZeroMemory(key.data(),key.size());throw;}
    return key;
#else
    (void)config;return {};
#endif
}
bool assistantApiKeyStored(const AssistantConfig& config){
#ifdef _WIN32
    if(endpointHost(config.endpoint).empty()||config.provider.empty())return false;
    std::error_code error;return std::filesystem::exists(keyPath(savedKeyRoute(config)),error);
#else
    (void)config;return false;
#endif
}
bool removeAssistantApiKey(const AssistantConfig& config){
#ifdef _WIN32
    if(endpointHost(config.endpoint).empty()||config.provider.empty())return false;
    std::error_code error;return std::filesystem::remove(keyPath(savedKeyRoute(config)),error);
#else
    (void)config;return false;
#endif
}
}
