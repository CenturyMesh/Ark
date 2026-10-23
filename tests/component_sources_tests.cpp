#include "ark/component_sources.hpp"
#include "ark/iteration_history.hpp"
#include "ark/component_vendors.hpp"
#include <filesystem>
#include <chrono>
#include <iostream>
#include <stdexcept>
using V=arkjson::Value;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}check(rejected,"invalid source request was accepted");}
static void researchWorkflow(){
    namespace fs=std::filesystem;const auto root=fs::temp_directory_path()/("ark-056c-component-research-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(fs::create_directory(root),"isolated source policy root");
    V query=V::makeObj();query.set("source",V::makeStr("web"));query.set("query",V::makeStr("AX42 bearing"));query.set("root",V::makeStr(root.string()));
    const std::string product="<h1>Fixture manufacturer</h1><p>"+std::string(33000,'x')+"</p><p>AX42 bearing: nominal bore 42 mm, fixture evidence only.</p>";
    const std::string search="<a class=\"result__a\" href=\"https://supplier.example/AX42\">AX42 bearing</a><a class=\"result__a\" href=\"https://www.mouser.com/AX42\">AX42 bearing</a>";
    unsigned calls=0;auto transport=[&](const std::string& url,std::size_t){++calls;return ark::ComponentHttpResponse{200,url.find("duckduckgo.com/")!=std::string::npos?search:product,"text/html",""};};
    const auto found=ark::searchComponentSource(query,transport);const auto& first=found.find("items")->arr[0];const auto& second=found.find("items")->arr[1];
    check(first.find("vendor")->str=="direct_https"&&second.find("vendor")->str=="mouser"&&first.find("discovery_source")->str=="web","web discovery identifies actual supplier policy, never search-provider policy");
    auto request=*first.find("next_action")->find("args");const auto initial=ark::componentSourcePage(request,transport);
    check(calls==2&&initial.find("source_policy")->find("vendor")->str=="direct_https"&&initial.find("truncated")->b&&initial.find("text")->str.find("AX42")==std::string::npos,"observed web result follow-up reads supplier without vendor:web rejection");
    request.set("text_offset",*initial.find("text_window")->find("next_text_offset"));request.set("expected_evidence_sha256",*initial.find("evidence_sha256"));
    auto continued=ark::componentSourcePage(request,transport);check(continued.find("text")->str.find("AX42 bearing")!=std::string::npos&&continued.find("evidence_sha256")->str==initial.find("evidence_sha256")->str,"long product evidence can be read beyond initial 32k under exact hash");
    V locate=V::makeObj();locate.set("query",V::makeStr("ax42"));locate.set("max_text_bytes",V::makeNum(256));auto match=ark::parseComponentSourcePage(product,"https://supplier.example/AX42","https://supplier.example/AX42",locate);check(match.find("text_window")->find("query_found")->b&&match.find("text")->str.find("AX42")!=std::string::npos,"bounded literal query includes matching reference beyond initial page window");
    locate.set("query",V::makeStr("unobserved model"));auto absent=ark::parseComponentSourcePage(product,"https://supplier.example/AX42","https://supplier.example/AX42",locate);check(!absent.find("text_window")->find("query_found")->b&&absent.find("text")->str.empty(),"missing query remains explicit without invented evidence");
    rejects([&]{ark::parseComponentSourcePage(product+"changed","https://supplier.example/AX42","https://supplier.example/AX42",request);});
    locate.set("text_offset",V::makeNum(0));rejects([&]{ark::parseComponentSourcePage(product,"https://supplier.example/AX42","https://supplier.example/AX42",locate);});
    auto invalid=request;invalid.set("vendor",V::makeStr("web"));const auto before=calls;rejects([&]{ark::componentSourcePage(invalid,transport);});check(calls==before,"invalid search-provider policy never bypasses approved supplier policy");
    invalid=request;invalid.set("max_text_bytes",V::makeNum(0));rejects([&]{ark::componentSourcePage(invalid,transport);});check(calls==before,"invalid page bounds reject before network access");
    invalid=request;invalid.set("expected_evidence_sha256",V::makeStr(""));rejects([&]{ark::componentSourcePage(invalid,transport);});check(calls==before,"malformed evidence guard rejects before network access");
    V unicode=V::makeObj();unicode.set("max_text_bytes",V::makeNum(256));const auto unicodePage=ark::parseComponentSourcePage(std::string(255,'a')+"\xc3\xa7"+" part","https://supplier.example/AX42","https://supplier.example/AX42",unicode);check(unicodePage.find("text")->str.size()==255&&unicodePage.find("text_window")->find("next_text_offset")->num==255,"page window never splits UTF-8 evidence");
    fs::remove(root);
}
int main(int argc,char** argv){try{
    researchWorkflow();
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
    if(argc==2&&(std::string(argv[1])=="--live-web"||std::string(argv[1])=="--live-page")){
        V pageArgs=V::makeObj();
        if(std::string(argv[1])=="--live-web"){
            V request=V::makeObj();request.set("source",V::makeStr("web"));request.set("query",V::makeStr("Raspberry Pi RP2040 site:raspberrypi.com"));
            const auto found=ark::searchComponentSource(request);check(!found.find("items")->arr.empty(),"live public discovery candidates");pageArgs=*found.find("items")->arr[0].find("next_action")->find("args");
        }else pageArgs.set("url",V::makeStr("https://www.raspberrypi.com/products/rp2040/"));
        pageArgs.set("query",V::makeStr("RP2040"));pageArgs.set("max_text_bytes",V::makeNum(1024));auto live=ark::componentSourcePage(pageArgs);check(live.find("text_window")->find("query_found")->b,"live manufacturer reference is readable");
        V receipt=V::makeObj();for(const char* field:{"source_url","evidence_sha256","retrieved_at_utc","text_window","http_evidence","source_policy"})receipt.set(field,*live.find(field));std::cout<<receipt.dump()<<'\n';
    }
    std::cout<<"Component source validation passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
