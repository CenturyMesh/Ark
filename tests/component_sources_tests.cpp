#include "ark/component_sources.hpp"
#include <iostream>
#include <stdexcept>
using V=arkjson::Value;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}check(rejected,"invalid source request was accepted");}
int main(int argc,char** argv){try{
    ark::validateComponentHttpsUrl("https://gitlab.com/kicad/libraries/model.step");
    for(auto* url:{"http://example.com/file.step","file:///private.step","https://user:pass@example.com/a.step","https://example.com/a#fragment","https://example.com/a\r\nHeader:x","https:///empty-host","https://example.com\\a.step"})rejects([&]{ark::validateComponentHttpsUrl(url);});
    auto sources=ark::componentSources();check(sources.find("sources")->arr.size()==4,"catalog registry");check(!sources.find("automatic_standard_recognition")->b,"catalog labels never certify standards");
    V query=V::makeObj();query.set("source",V::makeStr("traceparts"));rejects([&]{ark::searchComponentSource(query);});
    query=V::makeObj();query.set("path",V::makeStr("../escape"));rejects([&]{ark::searchComponentSource(query);});
    query=V::makeObj();query.set("page",V::makeNum(1.5));rejects([&]{ark::searchComponentSource(query);});
    if(argc==2&&std::string(argv[1])=="--live"){
        query=V::makeObj();query.set("path",V::makeStr("Connector_PinHeader_2.54mm.3dshapes"));query.set("query",V::makeStr("1x02"));auto found=ark::searchComponentSource(query);check(!found.find("items")->arr.empty(),"public KiCad source discovery");std::cout<<found.dump()<<'\n';
    }
    std::cout<<"Component source validation passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
