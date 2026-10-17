#include "ark/resource_library.hpp"
#include "ark/json.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <cstdint>
namespace ark {
struct ResourceLibrary::Data {
    std::vector<unsigned char> bytes;
    struct Entry {std::size_t offset,size;};
    std::map<std::string,Entry> entries;
};
namespace {
void check(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
const arkjson::Value& field(const arkjson::Value& v,const char* key){const auto* p=v.find(key);check(p!=nullptr,"Incomplete ARK resource index");return *p;}
std::size_t integer(const arkjson::Value& v){check(v.t==arkjson::Value::Num&&std::isfinite(v.num)&&v.num>=0&&v.num<=64*1024*1024&&std::floor(v.num)==v.num,"Invalid ARK resource range");return (std::size_t)v.num;}
}
ResourceLibrary::ResourceLibrary(const std::string& path){
    auto data=std::make_shared<Data>();std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
    check((bool)file,"Cannot open ARK resource library");const auto size=file.tellg();check(size>=12&&size<=64*1024*1024,"Invalid ARK resource library size");data->bytes.resize((std::size_t)size);file.seekg(0);file.read((char*)data->bytes.data(),size);check((bool)file,"Incomplete ARK resource library");
    check(std::equal(data->bytes.begin(),data->bytes.begin()+8,"ARKLIB01"),"Unsupported ARK resource library version");
    std::uint32_t length=0;for(unsigned i=0;i<4;++i)length|=(std::uint32_t)data->bytes[8+i]<<(8*i);check(length>0&&length<=1024*1024&&12+(std::size_t)length<=data->bytes.size(),"Invalid ARK resource index size");
    arkjson::Value index;check(arkjson::parse(std::string((const char*)data->bytes.data()+12,length),index)&&field(index,"format").asStr()=="ark.resource-library.v1","Invalid ARK resource index");const auto& entries=field(index,"entries");check(entries.t==arkjson::Value::Arr&&!entries.arr.empty()&&entries.arr.size()<=1024,"Invalid ARK resource entries");
    const auto base=12+(std::size_t)length;std::size_t next=0;std::string previous;
    for(const auto& entry:entries.arr){const auto& n=field(entry,"name");check(n.t==arkjson::Value::Str&&!n.str.empty()&&n.str.size()<=256,"Invalid ARK resource name");const auto& name=n.str;check(name.find_first_of("\\:\r\n\0",0,5)==std::string::npos&&name.front()!='/'&&name.find("..") ==std::string::npos&&name>previous,"Unsafe or duplicate ARK resource name");const auto offset=integer(field(entry,"offset")),bytes=integer(field(entry,"size"));check(offset==next&&bytes>0&&offset<=data->bytes.size()-base&&bytes<=data->bytes.size()-base-offset,"Overlapping or out-of-range ARK resource");const auto& digest=field(entry,"sha256");check(digest.t==arkjson::Value::Str&&digest.str.size()==64&&iterationSha256(std::string((const char*)data->bytes.data()+base+offset,bytes))==digest.str,"ARK resource integrity check failed");data->entries.emplace(name,Data::Entry{base+offset,bytes});next+=bytes;previous=name;}
    check(base+next==data->bytes.size(),"Undeclared ARK resource payload");data_=std::move(data);
}
bool ResourceLibrary::contains(const std::string& name)const{return data_->entries.count(name)!=0;}
std::vector<unsigned char> ResourceLibrary::read(const std::string& name)const{const auto p=data_->entries.find(name);check(p!=data_->entries.end(),"ARK resource is absent");return {data_->bytes.begin()+p->second.offset,data_->bytes.begin()+p->second.offset+p->second.size};}
std::vector<std::string> ResourceLibrary::names()const{std::vector<std::string> out;for(const auto& item:data_->entries)out.push_back(item.first);return out;}
}
