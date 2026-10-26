#pragma once
#include "ark/transmission.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace ark::transmission {
using V=arkjson::Value;
inline const V& get(const V& v,const char* key){static const V empty;auto p=v.find(key);return p?*p:empty;}
inline void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error("MECHANICAL_TRANSMISSION_INVALID: "+message);}
inline double number(const V& v,const char* key,double fallback=0,double limit=1e12){const auto& p=get(v,key);if(p.isNull())return fallback;require(p.t==V::Num&&std::isfinite(p.num)&&std::abs(p.num)<=limit,std::string("invalid finite number: ")+key);return p.num;}
inline bool flag(const V& v,const char* key,bool fallback=false){const auto& p=get(v,key);if(p.isNull())return fallback;require(p.t==V::Bool,std::string("invalid flag: ")+key);return p.b;}
inline std::string text(const V& v,const char* key,bool required=true){const auto& p=get(v,key);if(!required&&p.isNull())return {};require(p.t==V::Str&&(!required||!p.str.empty())&&p.str.size()<=4096,std::string("invalid text: ")+key);return p.str;}
inline void keys(const V& v,std::initializer_list<const char*> names){require(v.t==V::Obj,"expected object");for(const auto& item:v.obj)require(std::any_of(names.begin(),names.end(),[&](auto name){return item.first==name;}),"unknown field: "+item.first);}
inline void add(V& v,const char* key,double n){require(std::isfinite(n),std::string("nonfinite result: ")+key);v.set(key,V::makeNum(n));}
inline V strings(std::initializer_list<const char*> items){V a=V::makeArr();for(auto s:items)a.arr.push_back(V::makeStr(s));return a;}
inline V canonical(V v){if(v.t==V::Obj){for(auto& item:v.obj)item.second=canonical(std::move(item.second));std::sort(v.obj.begin(),v.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});}else if(v.t==V::Arr)for(auto& item:v.arr)item=canonical(std::move(item));return v;}
inline std::string hash(const V& v){return iterationSha256(canonical(v).dump());}
inline u64 nodeId(const Document& d,const V& ref){if(ref.t==V::Num){require(ref.num>=1&&ref.num<=9007199254740990.&&ref.num==std::floor(ref.num),"invalid occurrence id");for(const auto& n:d.assembly)if(n.id==(u64)ref.num)return n.id;}else if(ref.t==V::Str){u64 id=0;for(const auto& n:d.assembly)if(n.name==ref.str){require(!id,"ambiguous occurrence name");id=n.id;}if(id)return id;}throw std::runtime_error("MECHANICAL_OCCURRENCE_MISSING: select an existing assembly occurrence");}
}
