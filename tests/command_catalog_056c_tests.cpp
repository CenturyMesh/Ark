#include "ark/agent.hpp"
#include "ark/assistant.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ark;using V=arkjson::Value;size_t checks=0;
void check(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
const V& field(const V& value,const char* key){const auto* v=value.find(key);if(!v)throw std::runtime_error(std::string("Missing field: ")+key+" "+value.dump());return *v;}
V json(const std::string& text){V out;check(arkjson::parse(text,out),"Invalid fixture JSON");return out;}
V accepted(const V& result){check(!field(result,"isError").b,result.dump());return field(result,"structuredContent");}
V call(AgentSession& session,const char* tool,const V& args){return accepted(session.call(tool,args));}
V onboard(AgentSession& session,const std::string& args){return call(session,"agent_onboard",json(args));}
bool contains(const V& tools,const std::string& name){for(const auto& t:tools.arr)if(field(t,"name").str==name)return true;return false;}
const V& findTool(const V& tools,const std::string& name){for(const auto& t:tools.arr)if(field(t,"name").str==name)return t;throw std::runtime_error("Missing tool: "+name);}
V historyCall(const std::string& id,const V& args){V out=V::makeObj();out.set("type",V::makeStr("function_call"));out.set("name",V::makeStr("agent_onboard"));out.set("call_id",V::makeStr(id));out.set("arguments",V::makeStr(args.dump()));return out;}
void discovery(){
    AgentSession s;const auto before=documentToJson(s.document);const auto revision=s.revision;
    const auto found=onboard(s,R"({"topic":"commands","query":"bom","limit":3})");
    const auto policy=field(onboard(s,R"({"topic":"overview","detail":"guide"})"),"drawing_authorization");
    check(field(found,"drawing_authorization").dump()==policy.dump(),"Command search preserves the global explicit drawing authorization policy");
    check(!field(found,"executed").b&&!field(found,"document_state_included").b&&field(found,"commands").arr.size()<=3,"Search excludes state and execution and honors its result limit");
    check(field(found,"total_matches").num>=3&&field(found,"partial").b,"Search reports truncation explicitly");
    const auto registry=AgentSession::tools(true);
    for(const auto& tool:registry.arr){const auto name=field(tool,"name").str;V args=json(R"({"topic":"commands"})");args.set("command",V::makeStr("tool:"+name));const auto exact=call(s,"agent_onboard",args);
        check(field(field(exact,"command"),"input_schema").dump()==field(tool,"inputSchema").dump(),"Exact tool schema comes from the current registry: "+name);
        check(field(exact,"drawing_authorization").dump()==policy.dump(),"Exact command discovery preserves drawing authorization: "+name);}
    const auto none=onboard(s,R"({"topic":"commands","query":"zznonexistentcommandzz"})");check(field(none,"commands").arr.empty()&&!field(none,"partial").b,"No-match search does not invent a command");
    check(documentToJson(s.document)==before&&s.revision==revision,"Catalog reads preserve all authoring/revisions");
    check(found.dump().size()<registry.dump().size()/8,"Selected discovery is materially smaller than the identical full tool registry");
    std::cout<<"discovery full_tool_bytes="<<registry.dump().size()<<" bom_search_bytes="<<found.dump().size()<<'\n';
    for(const auto* args:{R"({"topic":"commands","command":"not_real"})",R"({"topic":"commands","limit":0})",R"({"topic":"commands","limit":1.5})",R"({"topic":"commands","parameters":{}})",R"({"topic":"commands","command":"parameter_set","query":"param"})",R"({"topic":"overview","query":"bom"})"})check(field(s.call("agent_onboard",json(args)),"isError").b,"Malformed discovery rejects");
}
V prepare(AgentSession& s,const std::string& command,V parameters){V args=json(R"({"topic":"commands"})");args.set("command",V::makeStr(command));args.set("parameters",parameters);return field(call(s,"agent_onboard",args),"prepared_call");}
void publish(AgentSession& s,const V& prepared){
    check(field(prepared,"tool").str=="doc_preview","Mutations prepare preview rather than applying");
    const auto preview=call(s,"doc_preview",field(prepared,"arguments"));V apply=V::makeObj();apply.set("candidate_id",field(preview,"candidate_id"));apply.set("expected_revision",V::makeNum((double)s.revision));call(s,"doc_apply",apply);
}
void recipes(){
    AgentSession s;s.suppressIterationHistory();s.suppressFailureCapture();const auto original=documentToJson(s.document);const auto revision=s.revision;
    auto values=json(R"({"name":"fixture_width","expr":"12"})");values.set("expected_revision",V::makeNum((double)revision));const auto prepared=prepare(s,"parameter_set",values);
    check(documentToJson(s.document)==original&&s.revision==revision,"Preparing a parameter set neither previews nor mutates");publish(s,prepared);
    check(s.revision==revision+1,"Ordinary preview/apply path commits the recipe once");
    check(field(s.call("doc_preview",field(prepared,"arguments")),"isError").b,"Stale prepared revisions reject without automatic rebasing");
    const auto current=documentToJson(s.document);
    for(const auto* params:{R"({"name":"fixture_width","expr":"12"})",R"({"expected_revision":0,"name":"fixture_width","expr":"12","op":"feature.remove"})",R"({"expected_revision":1.5,"name":"fixture_width","expr":"12"})",R"({"expected_revision":-1,"name":"fixture_width","expr":"12"})"}){
        V args=json(R"({"topic":"commands","command":"parameter_set"})");args.set("parameters",json(params));check(field(s.call("agent_onboard",args),"isError").b,"Invalid or missing recipe parameters reject");}
    check(documentToJson(s.document)==current,"Rejected parameters preserve authoring");
    V create=json(R"({"commands":[{"op":"feature.add","type":"box","name":"Fixture","p":{"dx":2,"dy":3,"dz":4}}]})");create.set("expected_revision",V::makeNum((double)s.revision));call(s,"doc_apply",create);
    values=json(R"({"id":"Fixture","p":{"dx":"fixture_width","dz":8}})");values.set("expected_revision",V::makeNum((double)s.revision));publish(s,prepare(s,"feature_parameters",values));
    values=json(R"({"name":"Occurrence","feature":"Fixture","tx":1,"ty":2,"tz":3})");values.set("expected_revision",V::makeNum((double)s.revision));publish(s,prepare(s,"assembly_instance",values));
    values=json(R"({"name":"Declared steel","density_kg_m3":7850})");values.set("expected_revision",V::makeNum((double)s.revision));publish(s,prepare(s,"material_density",values));
    const auto read=prepare(s,"revision_status",json("{}"));const auto status=call(s,field(read,"tool").str.c_str(),field(read,"arguments"));check(field(status,"revision").num==(double)s.revision,"Read recipe reaches the ordinary live status tool");
#ifdef ARK_COMMAND_NATIVE_TESTS
    values=json(R"({"name":"Native fixture","width":3,"depth":4,"height":5})");values.set("expected_revision",V::makeNum((double)s.revision));publish(s,prepare(s,"native_box",values));
#endif
}
void selectiveTools(){
    auto tools=AgentSession::tools();V history=V::makeArr();const auto core=assistantActiveTools(tools,history,"");check(!contains(core,"library_source_page"),"Source-page specialist starts deferred");
    history.arr.push_back(historyCall("exact",json(R"({"topic":"commands","command":"tool:library_source_page"})")));
    const auto selected=assistantActiveTools(tools,history,"");check(selected.arr.size()==core.arr.size()+1&&contains(selected,"library_source_page")&&!contains(selected,"library_fetch"),"Exact selection exposes only the requested available specialist");
    const auto deferred=assistantDeferredCatalogue(tools,history,"");check(field(findTool(deferred,"library_source_page"),"ark_loaded_by").str=="exact"&&!findTool(deferred,"library_fetch").find("ark_loaded_by"),"Deferred provider binds only the exact tool to its loading call");
    history.arr.push_back(historyCall("topic",json(R"({"topic":"vendors","detail":"guide"})")));
    const auto expanded=assistantActiveTools(tools,history,"");for(size_t i=0;i<selected.arr.size();++i)check(field(selected.arr[i],"name").str==field(expanded.arr[i],"name").str,"Later topics retain the previously advertised tool prefix");
    const auto expandedDeferred=assistantDeferredCatalogue(tools,history,"");check(field(findTool(expandedDeferred,"library_source_page"),"ark_loaded_by").str=="exact"&&field(findTool(expandedDeferred,"library_fetch"),"ark_loaded_by").str=="topic","Alias topic loads additional specialists without changing the earlier exact receipt");
    const auto preloaded=assistantDeferredCatalogue(tools,V::makeArr(),R"({"topics":[{"topic":"vendors"}]})");check(!findTool(preloaded,"library_source_page").find("ark_deferred"),"A preloaded alias topic exposes its tools immediately");
    const auto components=assistantDeferredCatalogue(tools,V::makeArr(),R"({"topics":[{"topic":"components"}]})");check(!findTool(components,"library_source_page").find("ark_deferred"),"Preloaded components also exposes its shared tools");
    tools.arr.erase(std::remove_if(tools.arr.begin(),tools.arr.end(),[](const auto& t){return field(t,"name").str=="library_source_page";}),tools.arr.end());
    check(!contains(assistantActiveTools(tools,history,""),"library_source_page"),"Command selection never adds tools outside this role's catalogue");
    std::cout<<"tool_offer core_bytes="<<core.dump().size()<<" one_tool_bytes="<<selected.dump().size()<<" vendors_topic_bytes="<<expanded.dump().size()<<'\n';
}
}
int main(){try{discovery();recipes();selectiveTools();std::cout<<"056C command catalog: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
