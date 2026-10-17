#include "ark/component_vendors.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
using V=arkjson::Value;namespace fs=std::filesystem;
void check(bool b,const char* text){if(!b)throw std::runtime_error(text);}
template<class F>void rejects(F f){bool denied=false;try{f();}catch(const std::exception&){denied=true;}check(denied,"invalid vendor request accepted");}
void lockContracts(const fs::path& root,V args){
    const auto lockPath=root/".approved-vendors.lock";{std::ofstream file(lockPath);file<<"";}args.set("expected_revision",V::makeNum(2));args.set("approved",V::makeBool(true));auto unlocked=ark::componentVendors(args);check(unlocked.find("revision")->num==3,"an old unlocked lock file cannot leave policy permanently blocked");args.set("expected_revision",V::makeNum(3));args.set("approved",V::makeBool(false));
#ifdef _WIN32
    auto handle=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);check(handle!=INVALID_HANDLE_VALUE,"create external writer lock");rejects([&]{ark::componentVendors(args);});CloseHandle(handle);
#else
    auto descriptor=::open(lockPath.c_str(),O_CREAT|O_RDWR,0600);check(descriptor>=0&&flock(descriptor,LOCK_EX|LOCK_NB)==0,"create external writer lock");rejects([&]{ark::componentVendors(args);});::close(descriptor);
#endif
    auto released=ark::componentVendors(args);check(released.find("revision")->num==4,"OS lock release permits exact next revision without stale directory cleanup");check(ark::componentVendorForUrl("https://html.duckduckgo.com/html/")=="web","search provider has its own observed vendor policy");
}
int main(){try{const auto root=fs::temp_directory_path()/("ark-vendors-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(fs::create_directory(root),"create isolated fixture");struct Cleanup{fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}}cleanup{root};const auto bytes=root.u8string();const std::string directory(bytes.begin(),bytes.end());V args=V::makeObj();args.set("root",V::makeStr(directory));auto original=ark::componentVendors(args);check(original.find("vendors")->arr.size()==10&&original.find("revision")->num==1&&!original.find("secrets_stored")->b,"complete vendor registry, no credentials");check(!fs::exists(root/"approved-vendors.json"),"list remains read only");check(ark::componentVendorForUrl("https://uk.misumi-ec.com/item")=="misumi"&&ark::componentVendorForUrl("https://notmisumi-ec.com/item")=="direct_https","domain boundary and regional attribution");auto evidence=ark::componentVendorEvidence("misumi","https://uk.misumi-ec.com/item",directory);check(evidence.find("approved")->b&&evidence.find("policy_sha256")->str==original.find("policy_sha256")->str,"approval receipt binds exact policy");rejects([&]{ark::componentVendorEvidence("misumi","https://misumi-ec.com.evil.example/item",directory);});args.set("operation",V::makeStr("update"));args.set("vendor",V::makeStr("misumi"));args.set("approved",V::makeBool(false));args.set("expected_revision",V::makeNum(1));auto updated=ark::componentVendors(args);check(updated.find("revision")->num==2&&updated.find("policy_sha256")->str!=original.find("policy_sha256")->str,"revision checked local approval persists");rejects([&]{ark::componentVendors(args);});rejects([&]{ark::componentVendorEvidence("misumi","https://uk.misumi-ec.com/item",directory);});rejects([&]{ark::componentVendorEvidence("direct_https","https://uk.misumi-ec.com/item",directory);});args.set("expected_revision",V::makeNum(2));check(ark::componentVendors(args).find("revision")->num==2,"idempotent update retains revision");lockContracts(root,args);args.set("password",V::makeStr("test-must-not-persist"));rejects([&]{ark::componentVendors(args);});std::ifstream file(root/"approved-vendors.json");auto stored=std::string(std::istreambuf_iterator<char>(file),{});check(stored.find("password")==std::string::npos&&stored.find("test-must-not-persist")==std::string::npos,"credential fields are never stored");file.close();{std::ofstream file(root/"approved-vendors.json");file<<"{}";}rejects([&]{ark::componentVendorEvidence("kicad","https://gitlab.com/kicad",directory);});std::cout<<"Approved vendor policy contracts passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
