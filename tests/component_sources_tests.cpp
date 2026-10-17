#include "ark/component_sources.hpp"
#include "ark/iteration_history.hpp"
#include <iostream>
#include <stdexcept>
using V=arkjson::Value;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}check(rejected,"invalid source request was accepted");}
int main(int argc,char** argv){try{
    ark::validateComponentHttpsUrl("https://gitlab.com/kicad/libraries/model.step");
    for(auto* url:{"http://example.com/file.step","file:///private.step","https://user:pass@example.com/a.step","https://example.com/a#fragment","https://example.com/a\r\nHeader:x","https:///empty-host","https://example.com\\a.step"})rejects([&]{ark::validateComponentHttpsUrl(url);});
    auto sources=ark::componentSources();check(sources.find("sources")->arr.size()==10,"catalog registry");check(!sources.find("automatic_standard_recognition")->b,"catalog labels never certify standards");
    V query=V::makeObj();query.set("source",V::makeStr("traceparts"));rejects([&]{ark::searchComponentSource(query);});
    query=V::makeObj();query.set("path",V::makeStr("../escape"));rejects([&]{ark::searchComponentSource(query);});
    query=V::makeObj();query.set("page",V::makeNum(1.5));rejects([&]{ark::searchComponentSource(query);});
    for(auto* url:{"https://127.0.0.1/a.step","https://localhost/a.step","https://host.local/a.step","https://[::1]/a.step","https://example.com:8443/a.step","https://example.com./a.step","https://a..example.com/a.step","https://example.com/a?token=private","https://example.com/a?%61pi_key=private","https://example.com/a?%54oken=private"})rejects([&]{ark::validateComponentHttpsUrl(url);});
    const std::string step="ISO-10303-21;\nHEADER;ENDSEC;DATA;ENDSEC;END-ISO-10303-21;\n";unsigned calls=0;
    auto transport=[&](const std::string& url,std::size_t limit){++calls;check(limit==64u*1024u*1024u,"STEP limit reaches transport");return url.ends_with("/start")?ark::ComponentHttpResponse{302,"","","/file.step"}:ark::ComponentHttpResponse{200,step,"application/step",""};};
    auto downloaded=ark::fetchComponentResource("https://supplier.example/start",ark::ComponentResourceKind::Step,{},transport);check(calls==2&&downloaded.bytes==step&&downloaded.receipt.find("sha256")->str==ark::iterationSha256(step)&&downloaded.receipt.find("byte_size")->num==step.size()&&downloaded.receipt.find("final_url")->str=="https://supplier.example/file.step"&&!downloaded.receipt.find("credentials_sent")->b,"redirected exact-byte custody");
    calls=0;auto cross=[&](const std::string&,std::size_t){++calls;return calls==1?ark::ComponentHttpResponse{302,"","","https://cdn.example/file.step"}:ark::ComponentHttpResponse{200,step,"application/octet-stream",""};};rejects([&]{ark::fetchComponentResource("https://supplier.example/start",ark::ComponentResourceKind::Step,{},cross);});check(calls==1,"cross-host redirect denied before second request");calls=0;check(ark::fetchComponentResource("https://supplier.example/start",ark::ComponentResourceKind::Step,{"cdn.example"},cross).bytes==step&&calls==2,"explicit finite redirect host allowed");
    auto response=[&](unsigned status,std::string body,std::string type,std::string location=""){return [=](const std::string&,std::size_t){return ark::ComponentHttpResponse{status,body,type,location};};};
    for(unsigned status:{401u,403u,429u,500u})rejects([&]{ark::fetchComponentResource("https://supplier.example/a",ark::ComponentResourceKind::Step,{},response(status,"denied","text/html"));});
    rejects([&]{ark::fetchComponentResource("https://supplier.example/a",ark::ComponentResourceKind::Step,{},response(200,"<html>"+step+"</html>","text/html"));});rejects([&]{ark::fetchComponentResource("https://supplier.example/a",ark::ComponentResourceKind::Step,{},response(200,"<html>"+step+"</html>","application/octet-stream"));});rejects([&]{ark::fetchComponentResource("https://supplier.example/a",ark::ComponentResourceKind::Step,{},response(200,"PK archive","application/zip"));});rejects([&]{ark::fetchComponentResource("https://supplier.example/a",ark::ComponentResourceKind::Step,{},response(302,"","","http://supplier.example/file.step"));});rejects([&]{ark::fetchComponentResource("https://supplier.example/a",ark::ComponentResourceKind::Step,{},response(302,"","","/a"));});
    calls=0;auto endless=[&](const std::string&,std::size_t){++calls;return ark::ComponentHttpResponse{302,"","","/redirect"+std::to_string(calls)};};rejects([&]{ark::fetchComponentResource("https://supplier.example/a",ark::ComponentResourceKind::Step,{},endless);});check(calls==4,"three redirects bound enforced");
    rejects([&]{ark::fetchComponentResource("https://supplier.example/a",ark::ComponentResourceKind::Page,{},response(200,std::string(2u*1024u*1024u+1,'x'),"text/html"));});
    const auto page=ark::parseComponentSourcePage("<h1>AX42</h1><a href='model.step'>STEP AX42</a><script><a href='fake.step'>fake</a></script>","https://supplier.example/a","https://supplier.example/a");check(page.find("links")->arr.size()==1&&!page.find("links")->arr[0].find("bytes_verified")->b&&page.find("links")->arr[0].find("link_id")->str.size()==64,"observed deterministic anchor with unverified target bytes");
    if(argc==2&&std::string(argv[1])=="--live"){
        query=V::makeObj();query.set("path",V::makeStr("Connector_PinHeader_2.54mm.3dshapes"));query.set("query",V::makeStr("1x02"));auto found=ark::searchComponentSource(query);check(!found.find("items")->arr.empty(),"public KiCad source discovery");std::cout<<found.dump()<<'\n';
    }
    std::cout<<"Component source validation passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
