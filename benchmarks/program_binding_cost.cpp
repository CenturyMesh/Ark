// D.02/G.01 synthetic cost of carrying parametric program bindings, with no
// drawing/session input. Eight namespaces each bind one large generated program
// (about 2000 statements, four legacy boxes, ~112 KB source); then one
// unrelated param.set is published repeatedly through doc_apply:
//  - "journal": the installing session, whose iteration journal grows by one
//    node per run (identical sequence for every executable, so comparable);
//  - "bound": a copy of the document without journal, isolating per-apply work;
//  - "detached": the same objects after detaching every binding (control).
// Serialization, clone and save/load of the journal-free bound document are
// timed separately. Run baseline and candidate executables alternately on the
// same machine and compare medians; retain every run.
#include "ark/parametric_programs.hpp"
#include "ark/serialize.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace ark;
using V=arkjson::Value;
using Clock=std::chrono::steady_clock;
static V data(const V& r){const auto* e=r.find("isError");if(e&&e->b)throw std::runtime_error(r.dump().substr(0,600));return *r.find("structuredContent");}
static V program(int k){
    std::string body="[";for(int i=0;i<2000;++i)body+="{\"let\":\"v"+std::to_string(i)+"\",\"value\":\"width*"+std::to_string(i%7+1)+"+sin("+std::to_string(i)+")*height/(1+"+std::to_string(i%5)+")\"},";
    body+="{\"for\":\"i\",\"count\":4,\"body\":[{\"emit\":{\"op\":\"feature.add\",\"type\":\"box\",\"name\":\"Block {i+1}\",\"p\":{\"dx\":\"{width}\",\"dy\":\"{height}\",\"dz\":\"{10+i}\"}}}]}]";
    V p;if(!arkjson::parse(R"({"format":"ark.parametric-program.v1","name":"Large )"+std::to_string(k)+R"(","parameters":{"width":{"default":20,"min":1,"max":100},"height":{"default":30,"min":1,"max":100}},"body":)"+body+"}",p))throw std::runtime_error("program JSON");return p;
}
static double ms(Clock::time_point a){return std::chrono::duration<double,std::milli>(Clock::now()-a).count();}
static std::string stats(std::vector<double> v){std::sort(v.begin(),v.end());double sum=0;for(double x:v)sum+=x;char buf[160];std::snprintf(buf,sizeof buf,"median %.3f ms, mean %.3f ms, min %.3f ms, max %.3f ms (n=%zu)",v[v.size()/2],sum/v.size(),v.front(),v.back(),v.size());return buf;}
static void applyRuns(AgentSession& s,const char* label,int runs){
    std::vector<double> apply;
    for(int i=0;i<runs;++i){V a=V::makeObj(),c=V::makeArr(),cmd=V::makeObj();cmd.set("op",V::makeStr("param.set"));cmd.set("name",V::makeStr("unrelated"));cmd.set("expr",V::makeNum(1+i));c.arr.push_back(cmd);a.set("commands",c);a.set("expected_revision",V::makeNum((double)s.revision));
        const auto t=Clock::now();data(s.call("doc_apply",a));apply.push_back(ms(t));}
    std::cout<<label<<" doc_apply param.set: "<<stats(apply)<<"\n";
}
static void copyRuns(const Document& d,const char* label,int runs){
    std::vector<double> serialize,clone;
    for(int i=0;i<runs;++i){auto t=Clock::now();const auto json=documentToJson(d);serialize.push_back(ms(t));if(json.empty())throw std::runtime_error("empty");
        t=Clock::now();Document copy;std::string error;if(!cloneDocumentAuthoring(d,copy,&error))throw std::runtime_error(error);clone.push_back(ms(t));}
    std::cout<<label<<" documentToJson: "<<stats(serialize)<<"\n"<<label<<" cloneDocumentAuthoring: "<<stats(clone)<<"\n";
}
static std::unique_ptr<AgentSession> journalFree(const AgentSession& source){auto s=std::make_unique<AgentSession>();s->document=source.document;s->document.iterationHistory=V{};s->revision=source.revision;s->suppressIterationHistory();return s;}
static void run(AgentSession& s,const V& args){V a=args;a.set("expected_revision",V::makeNum((double)s.revision));const auto preview=data(s.call("parametric_program",a));V apply=V::makeObj();apply.set("candidate_id",*preview.find("candidate_id"));apply.set("expected_revision",V::makeNum((double)s.revision));data(s.call("doc_apply",apply));}
int main(int argc,char** argv){try{
    const int runs=argc>1?std::max(5,std::atoi(argv[1])):30;AgentSession s;size_t bytes=0;
    auto t=Clock::now();
    for(int k=0;k<8;++k){const auto p=program(k);bytes+=p.dump().size();V a=V::makeObj();a.set("action",V::makeStr("prepare"));a.set("mode",V::makeStr("install"));a.set("namespace",V::makeStr("ns"+std::to_string(k)));a.set("program",p);run(s,a);}
    std::cout<<"bindings "<<s.document.parametricPrograms.arr.size()<<", program source "<<bytes<<" bytes, features "<<s.document.features.size()<<", eight installs "<<ms(t)<<" ms\n";
    {auto boundSession=journalFree(s);auto& bound=*boundSession;applyRuns(bound,"bound",runs);copyRuns(bound.document,"bound",runs);
        t=Clock::now();V a=V::makeObj();a.set("action",V::makeStr("prepare"));a.set("mode",V::makeStr("configure"));a.set("namespace",V::makeStr("ns0"));V values=V::makeObj();values.set("width",V::makeNum(25));a.set("values",values);run(bound,a);std::cout<<"bound configure ns0 prepare+apply: "<<ms(t)<<" ms\n";
        const auto dir=std::filesystem::temp_directory_path()/("ark-binding-cost-"+std::to_string(Clock::now().time_since_epoch().count()));std::filesystem::create_directory(dir);const auto path=(dir/"bound.ark").string();
        std::vector<double> save,load;for(int i=0;i<std::max(5,runs/3);++i){t=Clock::now();if(!saveDocument(bound.document,path.c_str()))throw std::runtime_error("save");save.push_back(ms(t));t=Clock::now();Document d;std::string error;if(!loadDocument(path.c_str(),d,&error))throw std::runtime_error(error);load.push_back(ms(t));}
        std::cout<<"bound saveDocument: "<<stats(save)<<"\nbound loadDocument: "<<stats(load)<<"\n";std::error_code ec;std::filesystem::remove_all(dir,ec);
        auto detachedSession=journalFree(s);auto& detached=*detachedSession;for(int k=0;k<8;++k){V a=V::makeObj();a.set("action",V::makeStr("prepare"));a.set("mode",V::makeStr("detach"));a.set("namespace",V::makeStr("ns"+std::to_string(k)));run(detached,a);}
        std::cout<<"control: bindings "<<(detached.document.parametricPrograms.isNull()?0:detached.document.parametricPrograms.arr.size())<<", features "<<detached.document.features.size()<<"\n";
        applyRuns(detached,"detached",runs);copyRuns(detached.document,"detached",runs);}
    applyRuns(s,"journal",std::max(5,runs/2));
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
