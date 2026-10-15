#include "ark/agent.hpp"
#include "ark/iteration_history.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using V=arkjson::Value;
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
std::string bytes(const std::filesystem::path& p){std::ifstream input(p,std::ios::binary);check((bool)input,"Cannot open corpus file");return {std::istreambuf_iterator<char>(input),{}};}
int main(int argc,char** argv){try{
    check(argc==2,"Expected corpus manifest path");auto manifestPath=std::filesystem::path(reinterpret_cast<const char8_t*>(argv[1]));V manifest;check(arkjson::parse(bytes(manifestPath),manifest),"Manifest parses");check(manifest.find("schema")->str=="ark.regression.corpus.v1","Corpus schema");int cases=0;
    for(const auto& fixture:manifest.find("fixtures")->arr){auto name=fixture.find("path")->str;check(name.find("..") == std::string::npos&&std::filesystem::path(name).is_relative(),"Fixture stays in corpus");auto content=bytes(manifestPath.parent_path()/name);check(ark::iterationSha256(content)==fixture.find("sha256")->str,"Corpus input hash differs: "+name);V inputs;check(arkjson::parse(content,inputs)&&inputs.find("seed")->num==manifest.find("seed")->num,"Fixture parses with declared seed");
        for(const auto& example:inputs.find("cases")->arr){auto id=example.find("id")->str;ark::AgentSession session;session.suppressFailureCapture();session.document.setParam("preserved","19");auto initial=ark::documentToJson(session.document);V request=V::makeObj();request.set("commands",*example.find("commands"));request.set("expected_revision",example.find("revision")?*example.find("revision"):V::makeNum(0));auto response=session.call("doc_apply",request);bool accepted=!response.find("isError")->b;check(accepted==(example.find("expected")->str=="accept"),id+": unexpected outcome: "+response.dump());
            if(accepted){check(session.revision==1&&session.document.features.size()==(size_t)example.find("features")->num,id+": published feature count/revision");auto serialized=ark::documentToJson(session.document);ark::Document restored;std::string error;check(ark::documentFromJson(serialized,restored,&error)&&ark::documentToJson(restored)==serialized,id+": positive persistence roundtrip: "+error);}
            else check(session.revision==0&&ark::documentToJson(session.document)==initial,id+": rejection must preserve the entire document and revision");
            ++cases;std::cout<<id<<": "<<(accepted?"accepted":"rejected atomically")<<'\n';
        }
    }
    std::cout<<"Synthetic corpus: "<<cases<<" cases passed; seed="<<manifest.find("seed")->num<<"; manifest_sha256="<<ark::iterationSha256(bytes(manifestPath))<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
