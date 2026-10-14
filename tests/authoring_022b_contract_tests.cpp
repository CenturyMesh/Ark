#include "ark/agent.hpp"
#include "ark/onboarding.hpp"
#include "ark/assistant.hpp"
#include <iostream>
#include <stdexcept>
using namespace ark;
using V=arkjson::Value;
static void check(bool pass,const char* reason){if(!pass)throw std::runtime_error(reason);}
static V parse(const char* text){V v;check(arkjson::parse(text,v),"fixture JSON");return v;}
static const V& tool(const V& list,const char* name){for(const auto& item:list.arr)if(item.find("name")->str==name)return item;throw std::runtime_error("missing discovered tool");}
static bool contains(const V& list,const std::string& text){return std::any_of(list.arr.begin(),list.arr.end(),[&](const V& item){return item.t==V::Str&&item.str==text;});}
static V call(AgentSession& s,const char* name,V args){if(std::string(name)=="doc_apply")args.set("expected_revision",V::makeNum((double)s.revision));auto out=s.call(name,args);if(out.find("isError")->b)throw std::runtime_error(out.find("structuredContent")->dump());return *out.find("structuredContent");}
int main(){try{
    const V tools=AgentSession::tools(true);const auto& apply=*tool(tools,"doc_apply").find("inputSchema")->find("properties")->find("commands")->find("items");const auto& preview=*tool(tools,"doc_preview").find("inputSchema")->find("properties")->find("commands")->find("items");
    check(apply.dump()==preview.dump(),"preview and apply command contracts diverged");const auto& props=*apply.find("properties"),ops=*props.find("op")->find("enum");
    for(const char* kind:{"miter","swept_flange","corner","rip","tab"})for(const char* action:{"add","edit","remove"})check(contains(ops,std::string("sheet.")+kind+"."+action),"advanced sheet operation missing from tools/list");
    check(contains(ops,"sheet.bend_table.set")&&contains(ops,"sheet.bend_table.remove"),"bend table operations missing");
    const auto& patterns=*props.find("pattern")->find("oneOf")->arr.back().find("properties")->find("type")->find("enum");for(const char* kind:{"linear","circular","pattern_driven","sketch_driven","curve_driven","chain"})check(contains(patterns,kind),"component pattern absent from actual discovery");
    check(props.find("corner")->find("oneOf")->arr.front().find("type")->str=="integer","sketch corner contract overwritten by sheet corner object");
    check(props.find("native")->find("properties")->find("source_vertex")&&props.find("drawing"),"native vertex/drawing recipe not discoverable");
    AgentSession session;const auto baseline=documentToJson(session.document);const auto overview=agentOnboard(session,parse(R"({"topic":"overview"})"));
    for(const auto& topic:overview.find("available_topics")->arr){V args=V::makeObj();args.set("topic",topic);const auto guide=agentOnboard(session,args);check(guide.find("drawing_authorization")&&guide.find("drawing_authorization")->str.find("only when the user explicitly requests")!=std::string::npos,"drawing permission missing from onboarding topic");}
    check(session.revision==0&&documentToJson(session.document)==baseline&&session.document.drawings.empty(),"onboarding wrote drawings or changed authoring");
    AssistantConfig config;config.model="offline-022b-contract";bool observed=false;
    auto run=runAssistant(config,V::makeStr("Create an editable concept; no drawings requested"),tools,[&](const auto&,const auto&){throw std::runtime_error("unexpected model tool call");return V{};},{},nullptr,[&](const auto&,const V& request,auto*){check(request.find("instructions")&&request.find("instructions")->str.find("Create or update 2D/manufacturing drawings only when the user explicitly requests")!=std::string::npos,"runtime assistant omitted drawing opt-in policy");observed=true;return parse(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"No drawing work requested."}]}]})");});
    check(observed&&!run.text.empty()&&session.document.drawings.empty(),"drawing authorization runtime fixture failed");
#ifdef ARK_022B_NATIVE_TESTS
    V args=parse(R"({"topic":"sheet"})");auto guide=agentOnboard(session,args);check(guide.find("example_advanced_sheet"),"advanced sheet example absent");call(session,"doc_apply",*guide.find("example_advanced_sheet"));check(session.document.drawings.empty(),"sheet recipe automatically authored drawings");
    auto catalog=call(session,"drawing_inspect",V::makeObj());check(catalog.find("sources")&&!catalog.find("sources")->arr.empty(),"drawing source catalog unavailable");
    bool found=false;for(const auto& source:catalog.find("sources")->arr)if(source.find("supported")->b){check(source.find("suggested_drawing"),"supported drawing source has no editable recipe");found=true;}check(found&&session.document.drawings.empty(),"drawing discovery created a sheet or advertised no valid source");
#endif
    std::cout<<"022B actual tool discovery, executable onboarding, optional drawings and runtime policy passed; tools="<<tools.arr.size()<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
