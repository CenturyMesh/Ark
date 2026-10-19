#include "ark/result_path.hpp"
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ark {
namespace {
using V=arkjson::Value;
struct Step {enum Kind {Key,Index,Range} kind=Key;std::string key;std::size_t from=0,to=0;};
std::vector<Step> parse(const std::string& path){
    if(path.empty()||path.size()>256)throw std::runtime_error("Result path must be 1..256 characters: "+path);
    std::vector<Step> steps;std::size_t start=0;
    auto number=[&](const std::string& text,std::size_t fallback){
        if(text.empty())return fallback;
        if(text.size()>6||text.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Result path index must be a non-negative integer: "+path);
        return (std::size_t)std::stoul(text);
    };
    for(;;){
        const auto end=path.find('.',start);const auto segment=path.substr(start,end==std::string::npos?std::string::npos:end-start);
        const auto open=segment.find('[');const auto key=segment.substr(0,open);
        if(key.empty()||key.find(']')!=std::string::npos)throw std::runtime_error("Each result path step needs a field name: "+path);
        steps.push_back({Step::Key,key,0,0});
        if(open!=std::string::npos){
            if(segment.back()!=']'||segment.find('[',open+1)!=std::string::npos)throw std::runtime_error("A result path step accepts one [n], [a:b] or []: "+path);
            const auto inside=segment.substr(open+1,segment.size()-open-2);const auto colon=inside.find(':');
            if(inside.empty())steps.push_back({Step::Range,{},0,std::numeric_limits<std::size_t>::max()});
            else if(colon==std::string::npos)steps.push_back({Step::Index,{},number(inside,0),0});
            else{Step range{Step::Range,{},number(inside.substr(0,colon),0),number(inside.substr(colon+1),std::numeric_limits<std::size_t>::max())};if(range.to<range.from)throw std::runtime_error("Result path range ends before it starts: "+path);steps.push_back(range);}
        }
        if(steps.size()>32)throw std::runtime_error("Result path has too many steps: "+path);
        if(end==std::string::npos)break;start=end+1;
    }
    return steps;
}
std::string keys(const V& value){
    if(value.t!=V::Obj)return " (not an object)";
    std::string out="; available: ";bool first=true;for(const auto& field:value.obj){if(!first)out+=", ";out+=field.first;first=false;}
    return out;
}
bool select(const V& value,const std::vector<Step>& steps,std::size_t at,V& out,std::string& where,bool mapped){
    if(at==steps.size()){out=value;return true;}
    const auto& step=steps[at];
    if(step.kind==Step::Key){
        const V* next=value.find(step.key.c_str());
        if(!next){if(mapped){out=V{};return true;}where=step.key+keys(value);return false;}
        return select(*next,steps,at+1,out,where,mapped);
    }
    if(value.t!=V::Arr){if(mapped){out=V{};return true;}where="["+std::string(step.kind==Step::Index?std::to_string(step.from):"")+"] on a value that is not an array";return false;}
    if(step.kind==Step::Index){
        if(step.from>=value.arr.size()){if(mapped){out=V{};return true;}where="index "+std::to_string(step.from)+" of an array of "+std::to_string(value.arr.size());return false;}
        return select(value.arr[step.from],steps,at+1,out,where,mapped);
    }
    out=V::makeArr();
    for(std::size_t i=step.from;i<value.arr.size()&&i<step.to;++i){V item;select(value.arr[i],steps,at+1,item,where,true);out.arr.push_back(std::move(item));}
    return true;
}
}
void validateResultPath(const std::string& path){parse(path);}
bool selectResultPath(const V& value,const std::string& path,V& out,std::string& where){return select(value,parse(path),0,out,where,false);}
}
