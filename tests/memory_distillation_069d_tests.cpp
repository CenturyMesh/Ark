#include "ark/project_memory.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using V=arkjson::Value;
namespace fs=std::filesystem;
static int checks=0;
static void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
static const V& at(const V& v,const char* k){auto p=v.find(k);if(!p)throw std::runtime_error(std::string("Missing ")+k);return *p;}
static V& mutableAt(V& v,const char* k){for(auto& p:v.obj)if(p.first==k)return p.second;throw std::runtime_error(std::string("Missing mutable ")+k);}
static V json(const std::string& s){V v;if(!arkjson::parse(s,v))throw std::runtime_error("JSON");return v;}
template<class F>static void rejects(F f,const char* why){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad,why);}
static void erase(V& v,const std::string& key){v.obj.erase(std::remove_if(v.obj.begin(),v.obj.end(),[&](const auto& p){return p.first==key;}),v.obj.end());}
static V canonical(V v){std::sort(v.obj.begin(),v.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});return v;}
int main(){const auto directory=fs::temp_directory_path()/("ark-distill-069d-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));try{
    ark::ProjectMemoryConfig config;config.rootDirectory=directory.string();config.maxResultBytes=131072;
    ark::ProjectMemory memory(config);auto call=[&](const char* action,const V& args){return memory.call(action,args,"synthetic:069d","test:069d");};
    V selection=json(R"({"keys":[],"expected_revisions":{}})");V originals=V::makeObj();
    for(int i=0;i<12;++i){
        const auto key="episode-"+std::to_string(i);
        auto note=json(R"({"kind":"episode","title":"Synthetic exact workflow","source":"synthetic:069d","expected_revision":0,"observed_revision":7,"steps":["Inspect current native source","Review exact candidate","Apply with current revision"],"facts":[{"subject":"fixture","predicate":"thickness_mm","value":2}],"tags":["synthetic"],"outcome":"successful"})");
        note.set("key",V::makeStr(key));note.set("content",V::makeStr("Exact historical text, preserve qualifications. "+std::string(2500,'x')));note.set("source_fingerprint",V::makeStr(ark::iterationSha256(key)));
        if(i==11){note.set("outcome",V::makeStr("failed"));note.set("expires_at",V::makeNum(1));}
        call("store",note);mutableAt(selection,"keys").arr.push_back(V::makeStr(key));mutableAt(selection,"expected_revisions").set(key,V::makeNum(1));
        auto get=json(R"({"key":""})");get.set("key",V::makeStr(key));originals.set(key,at(call("get",get),"records").arr.front());
    }
    const auto stats=call("stats",V::makeObj()).dump();
    auto index=call("get",json(R"({"key":"episode-0","detail":"index"})"));auto indexRow=at(index,"records").arr.front();
    check(!indexRow.find("content")&&!indexRow.find("facts")&&!indexRow.find("steps")&&at(indexRow,"title").str=="Synthetic exact workflow","index contains discovery metadata but no prose or facts");
    auto request=json(R"({"key":"episode-0","detail":"full"})");request.set("known_scope",at(index,"scope"));request.set("known_fingerprints",at(index,"known_fingerprints"));
    auto full=call("get",request);check(at(at(full,"records").arr.front(),"content").str.size()>2500&&!at(at(full,"records").arr.front(),"unchanged").b,"index receipt cannot erase the first full recall");
    request.set("detail",V::makeStr("index"));auto repeated=call("get",request);
    check(at(at(repeated,"records").arr.front(),"unchanged").b&&!at(repeated,"records").arr.front().find("title"),"exact repeated index uses its own representation fingerprint");
    request.set("known_fingerprints",at(full,"known_fingerprints"));auto inverse=call("get",request);check(!at(at(inverse,"records").arr.front(),"unchanged").b&&at(inverse,"records").arr.front().find("title"),"full receipt cannot suppress index metadata");
    rejects([&]{call("get",json(R"({"key":"episode-0","detail":"summary"})"));},"unsupported detail rejects");
    rejects([&]{call("get",json(R"({"key":"episode-0","detail":"index","max_bytes":1024})"));},"narrow index cannot fall back to a misleading full-record fingerprint");
    auto distilled=call("distill",selection);
    check(at(distilled,"unique_extracts").num==1&&at(distilled,"selected_records").num==12,"identical text and ordered structured content share one exact extract");
    check(!at(distilled,"partial").b&&!at(distilled,"storage_changed").b&&!distilled.find("known_fingerprints"),"complete read-only representation cannot be used as a full-read delta receipt");
    for(const auto& row:at(distilled,"records").arr){
        auto recovered=row;const auto& payload=at(at(distilled,"extracts"),at(row,"extract_id").str.c_str());
        erase(recovered,"extract_id");erase(recovered,"facts_in_extract");recovered.set("facts_included",V::makeBool(true));for(const auto& p:payload.obj)recovered.set(p.first,p.second);
        check(canonical(recovered).dump()==canonical(at(originals,at(row,"key").str.c_str())).dump(),"all original record fields reconstruct exactly, including failure, expiry and attribution");
    }
    check(distilled.dump().size()<at(distilled,"full_records_bytes").num*.7,"repeated-content fixture reduces serialized bytes by at least30percent");
    std::cout<<"MEMORY_DISTILLATION_069D full_records_bytes="<<at(distilled,"full_records_bytes").num<<" distilled_receipt_bytes="<<distilled.dump().size()<<" index_bytes="<<index.dump().size()<<" full_single_bytes="<<full.dump().size()<<'\n';
    auto stale=selection;mutableAt(stale,"expected_revisions").set("episode-0",V::makeNum(2));rejects([&]{call("distill",stale);},"stale source selection rejects atomically");
    auto narrow=selection;narrow.set("max_bytes",V::makeNum(1024));rejects([&]{call("distill",narrow);},"oversized extracts reject without silently losing qualifications");
    rejects([&]{memory.call("distill",selection,"another-project","test");},"distillation stays in its project");
    check(call("stats",V::makeObj()).dump()==stats,"all reads and rejected attempts preserve stored records and revisions");
    auto changed=json(R"({"key":"conflicting","kind":"decision","title":"Contradictory historical declaration","source":"synthetic:other","expected_revision":0,"content":"Declared differently; inspect source.","facts":[{"subject":"fixture","predicate":"thickness_mm","value":3}]})");changed.set("source_fingerprint",V::makeStr(std::string(64,'a')));call("store",changed);
    mutableAt(selection,"keys").arr.push_back(V::makeStr("conflicting"));mutableAt(selection,"expected_revisions").set("conflicting",V::makeNum(1));selection.set("current_revision",V::makeNum(8));
    distilled=call("distill",selection);check(at(distilled,"disagreements").arr.size()==1&&at(at(distilled,"disagreements").arr.front(),"claims").arr.size()==2,"contradictory values remain separate and explicitly attributed");
    int staleCount=0;for(const auto& row:at(distilled,"records").arr)if(at(row,"stale").b)++staleCount;check(staleCount==12,"freshness is recalculated for every selected record");
    std::cout<<"Memory distillation checks passed: "<<checks<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
