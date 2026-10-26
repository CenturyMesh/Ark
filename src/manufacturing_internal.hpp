#pragma once
#include "ark/manufacturing_simulation.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace ark::mfg {
using V=arkjson::Value;
inline const V& get(const V& v,const char* key){static const V empty;auto p=v.find(key);return p?*p:empty;}
inline void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error("MANUFACTURING: "+message);}
inline void keys(const V& v,std::initializer_list<const char*> names){require(v.t==V::Obj,"expected object");for(const auto& f:v.obj)require(std::any_of(names.begin(),names.end(),[&](auto k){return f.first==k;}),"unknown field: "+f.first);}
inline std::string text(const V& v,const char* key,bool required=true,size_t max=128){const auto& p=get(v,key);if(!required&&p.isNull())return {};require(p.t==V::Str&&(!required||!p.str.empty())&&p.str.size()<=max,std::string("invalid text: ")+key);return p.str;}
inline double num(const V& v,const char* key,double fallback=0,double limit=1e9){const auto& p=get(v,key);if(p.isNull())return fallback;require(p.t==V::Num&&std::isfinite(p.num)&&std::abs(p.num)<=limit,std::string("invalid number: ")+key);return p.num;}
inline bool flag(const V& v,const char* key,bool fallback=false){const auto& p=get(v,key);if(p.isNull())return fallback;require(p.t==V::Bool,std::string("invalid flag: ")+key);return p.b;}
inline Vec3 vec(const V& v,const char* key){const auto& p=get(v,key);require(p.t==V::Arr&&p.arr.size()==3,std::string("expected XYZ vector: ")+key);for(const auto& n:p.arr)require(n.t==V::Num&&std::isfinite(n.num)&&std::abs(n.num)<=1e6,"invalid XYZ value");return {p.arr[0].num,p.arr[1].num,p.arr[2].num};}
inline V xyz(Vec3 p){V v=V::makeArr();for(double n:{p.x,p.y,p.z})v.arr.push_back(V::makeNum(n));return v;}
inline V strings(std::initializer_list<const char*> items){V v=V::makeArr();for(auto s:items)v.arr.push_back(V::makeStr(s));return v;}
inline V canonical(V v){if(v.t==V::Obj){for(auto& f:v.obj)f.second=canonical(std::move(f.second));std::sort(v.obj.begin(),v.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});}else if(v.t==V::Arr)for(auto& x:v.arr)x=canonical(std::move(x));return v;}
inline std::string hash(const V& v){return iterationSha256(canonical(v).dump());}
inline const Body* body(const Document& d,u64 id){for(const auto& b:d.evalBodies)if(b.id==id)return &b;return nullptr;}
inline const V& plan(const Document& d,const std::string& name){for(const auto& p:d.manufacturingPlans.arr)if(text(p,"name")==name)return p;throw std::runtime_error("MANUFACTURING: plan not found: "+name);}
struct Graph {
    const V& root;std::map<std::string,const V*> nodes;std::vector<std::string> order;
    explicit Graph(const V&);
    const V& data(const std::string& id,const char* kind=nullptr)const{auto i=nodes.find(id);require(i!=nodes.end(),"missing node: "+id);if(kind)require(text(*i->second,"kind")==kind,"node kind mismatch: "+id);return get(*i->second,"data");}
};
std::vector<std::string> dependencies(const V&);
std::string sourceDigest(const Document&,const V&);
std::string operationProgram(const Graph&,const V&);
V runSlicerBridge(const Document&,u64,const V&);
Mesh printPlateMesh062(const Document&,const Graph&,const V&);
}
