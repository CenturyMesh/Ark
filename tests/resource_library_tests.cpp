#include "ark/resource_library.hpp"
#include "ark/iteration_history.hpp"
#include "ark/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <functional>
using V=arkjson::Value;
namespace fs=std::filesystem;
int main(){
    const auto root=fs::temp_directory_path()/("ark-resource-tests-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));fs::create_directories(root);int checks=0;
    auto check=[&](bool ok){if(!ok)throw std::runtime_error("Resource library regression");++checks;};
    auto pack=[](V entries,const std::string& bytes){V index=V::makeObj();index.set("format",V::makeStr("ark.resource-library.v1"));index.set("entries",entries);auto json=index.dump();std::string out="ARKLIB01";for(unsigned i=0;i<4;++i)out.push_back((char)((json.size()>>(8*i))&255));return out+json+bytes;};
    auto write=[&](const std::string& name,const std::string& bytes){auto path=root/name;std::ofstream f(path,std::ios::binary);f.write(bytes.data(),bytes.size());f.close();return path.string();};
    auto rejected=[&](const std::string& bytes){bool failed=false;try{ark::ResourceLibrary r(write("bad.arklib",bytes));}catch(const std::exception&){failed=true;}check(failed);};
    try{
        const std::string bytes("PNG\0original\xff",13);V entries=V::makeArr(),entry=V::makeObj();entry.set("name",V::makeStr("mark.png"));entry.set("offset",V::makeNum(0));entry.set("size",V::makeNum((double)bytes.size()));entry.set("sha256",V::makeStr(ark::iterationSha256(bytes)));entries.arr.push_back(entry);
        auto good=pack(entries,bytes);ark::ResourceLibrary r(write("valid.arklib",good));check(r.contains("mark.png"));check(!r.contains("missing.png"));auto actual=r.read("mark.png");check(std::string(actual.begin(),actual.end())==bytes);check(r.names()==std::vector<std::string>{"mark.png"});
        rejected(good.substr(0,10));auto corrupt=good;corrupt[0]='X';rejected(corrupt);corrupt=good;corrupt.back()^=1;rejected(corrupt);rejected(good+"hidden");
        auto bad=entries;bad.arr[0].set("name",V::makeStr("../mark.png"));rejected(pack(bad,bytes));bad=entries;bad.arr[0].set("offset",V::makeNum(1));rejected(pack(bad,bytes));bad=entries;bad.arr.push_back(entry);rejected(pack(bad,bytes+bytes));bad=entries;bad.arr[0].set("size",V::makeNum(10000));rejected(pack(bad,bytes));
        bool absent=false;try{r.read("missing.png");}catch(const std::exception&){absent=true;}check(absent);
        std::cout<<checks<<" binary resource library checks passed\n";fs::remove_all(root);return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<" fixture="<<root<<'\n';return 1;}
}
