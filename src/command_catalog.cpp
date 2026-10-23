#include "ark/command_catalog.hpp"
#include "ark/agent.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
V json(const char* text){V out;if(!arkjson::parse(text,out))throw std::runtime_error("Invalid built-in command recipe");return out;}
struct Recipe {const char* id;const char* summary;const char* topic;const char* schema;const char* call;};
const Recipe recipes[]={
    {"surface_mid_mirror","Preview an associative half-surface mirror across a named centre plane with explicit G0/G1 seam requirements.","surfacing",
     R"({"type":"object","required":["expected_revision","name","source","plane","continuity"],"properties":{"expected_revision":{"type":"integer","minimum":0},"name":{"type":"string","minLength":1,"maxLength":128},"source":{"type":["integer","string"],"minimum":1,"minLength":1,"maxLength":128},"plane":{"type":"string","minLength":1,"maxLength":128},"continuity":{"type":"string","enum":["G0","G1"]}},"additionalProperties":false})",
     R"({"tool":"doc_preview","arguments":{"expected_revision":"$expected_revision","detail":"changes","commands":[{"op":"feature.add","type":"surface_edit","name":"$name","inputs":["$source"],"native":{"op":"mirror","plane":"$plane","continuity":"$continuity"}}]}})"},
    {"rubber_body","Plan an editable belt, track, tire or ball with explicit dimensions in mm.","rubber",
     R"({"type":"object","required":["expected_revision","kind","name","dimensions"],"properties":{"expected_revision":{"type":"integer","minimum":0},"kind":{"type":"string","maxLength":16},"name":{"type":"string","maxLength":128},"dimensions":{"type":"object","minProperties":2,"maxProperties":4,"additionalProperties":{"type":["number","string"]}}},"additionalProperties":false})",
     R"({"tool":"rubber_plan","arguments":{"action":"create","expected_revision":"$expected_revision","kind":"$kind","name":"$name","dimensions":"$dimensions"}})"},
    {"parameter_set","Set one named expression through an atomic preview.","automation",
     R"({"type":"object","required":["expected_revision","name","expr"],"properties":{"expected_revision":{"type":"integer","minimum":0},"name":{"type":"string","minLength":1,"maxLength":128},"expr":{"type":"string","minLength":1,"maxLength":4096}},"additionalProperties":false})",
     R"({"tool":"doc_preview","arguments":{"expected_revision":"$expected_revision","detail":"changes","commands":[{"op":"param.set","name":"$name","expr":"$expr"}]}})"},
    {"native_box","Create an editable exact box using width, depth and height in mm.","solids",
     R"({"type":"object","required":["expected_revision","name","width","depth","height"],"properties":{"expected_revision":{"type":"integer","minimum":0},"name":{"type":"string","minLength":1,"maxLength":128},"width":{"type":["number","string"]},"depth":{"type":["number","string"]},"height":{"type":["number","string"]}},"additionalProperties":false})",
     R"({"tool":"doc_preview","arguments":{"expected_revision":"$expected_revision","detail":"changes","commands":[{"op":"feature.add","type":"native_solid","name":"$name","native":{"op":"box"},"p":{"width":"$width","depth":"$depth","height":"$height"}}]}})"},
    {"feature_parameters","Edit selected feature parameters after reading object_context.","automation",
     R"({"type":"object","required":["expected_revision","id","p"],"properties":{"expected_revision":{"type":"integer","minimum":0},"id":{"type":["integer","string"],"minimum":1,"minLength":1,"maxLength":128},"p":{"type":"object","minProperties":1,"maxProperties":64,"additionalProperties":{"type":["number","string"]}}},"additionalProperties":false})",
     R"({"tool":"doc_preview","arguments":{"expected_revision":"$expected_revision","detail":"changes","commands":[{"op":"feature.edit","id":"$id","p":"$p"}]}})"},
    {"assembly_instance","Place a reusable feature definition at explicit translation coordinates in mm.","assembly",
     R"({"type":"object","required":["expected_revision","name","feature","tx","ty","tz"],"properties":{"expected_revision":{"type":"integer","minimum":0},"name":{"type":"string","minLength":1,"maxLength":128},"feature":{"type":["integer","string"],"minimum":1,"minLength":1,"maxLength":128},"tx":{"type":["number","string"]},"ty":{"type":["number","string"]},"tz":{"type":["number","string"]}},"additionalProperties":false})",
     R"({"tool":"doc_preview","arguments":{"expected_revision":"$expected_revision","detail":"changes","commands":[{"op":"assembly.add","name":"$name","feature":"$feature","p":{"tx":"$tx","ty":"$ty","tz":"$tz"}}]}})"},
    {"material_density","Declare a named material density in kg/m3 from an explicit source.","materials",
     R"({"type":"object","required":["expected_revision","name","density_kg_m3"],"properties":{"expected_revision":{"type":"integer","minimum":0},"name":{"type":"string","minLength":1,"maxLength":128},"density_kg_m3":{"type":"number","minimum":0,"maximum":100000}},"additionalProperties":false})",
     R"({"tool":"doc_preview","arguments":{"expected_revision":"$expected_revision","detail":"changes","commands":[{"op":"material.set","name":"$name","density_kg_m3":"$density_kg_m3"}]}})"},
    {"revision_status","Read the live revision and status without expanding the design inventory.","overview",
     R"({"type":"object","properties":{},"additionalProperties":false})",
     R"({"tool":"doc_info","arguments":{"detail":"revision"}})"}
};
std::string lower(std::string text){for(auto& c:text)c=(char)std::tolower((unsigned char)c);return text;}
std::vector<std::string> words(const std::string& text){
    std::vector<std::string> out;std::string token;
    for(unsigned char c:lower(text)){if(std::isalnum(c)||c>=128)token+=(char)c;else if(!token.empty()){out.push_back(token);token.clear();}}
    if(!token.empty())out.push_back(token);return out;
}
int score(const std::string& query,const std::string& id,const std::string& text){
    if(query.empty())return 1;const auto haystack=lower(id+" "+text);int value=0;
    for(const auto& token:words(query)){if(haystack.find(token)==std::string::npos)return 0;value+=lower(id).find(token)!=std::string::npos?10:1;}
    return value+(lower(id)==lower(query)?100:0);
}
bool typeMatches(const V& value,const std::string& type){
    if(type=="integer")return value.t==V::Num&&std::isfinite(value.num)&&std::floor(value.num)==value.num;
    if(type=="number")return value.t==V::Num&&std::isfinite(value.num);
    if(type=="string")return value.t==V::Str;
    if(type=="object")return value.t==V::Obj;
    return false;
}
void validate(const V& value,const V& schema,const std::string& path){
    const auto* type=schema.find("type");bool matches=false;
    if(type&&type->t==V::Str)matches=typeMatches(value,type->str);
    if(type&&type->t==V::Arr)for(const auto& t:type->arr)matches=matches||typeMatches(value,t.str);
    if(!matches)throw std::runtime_error("Command parameter has invalid type: "+path);
    if(value.t==V::Num){
        if(const auto* min=schema.find("minimum");min&&value.num<min->num)throw std::runtime_error("Command parameter below minimum: "+path);
        if(const auto* max=schema.find("maximum");max&&value.num>max->num)throw std::runtime_error("Command parameter above maximum: "+path);
    }
    if(value.t==V::Str){
        if(value.str.find('\0')!=std::string::npos||value.str.empty()||value.str.size()>4096)throw std::runtime_error("Command string parameter is empty or oversized: "+path);
        if(const auto* max=schema.find("maxLength");max&&value.str.size()>(size_t)max->num)throw std::runtime_error("Command string parameter too long: "+path);
    }
    if(value.t==V::Obj){
        if(const auto* min=schema.find("minProperties");min&&value.obj.size()<(size_t)min->num)throw std::runtime_error("Command parameter object is empty: "+path);
        if(const auto* max=schema.find("maxProperties");max&&value.obj.size()>(size_t)max->num)throw std::runtime_error("Command parameter object too large: "+path);
        if(const auto* required=schema.find("required"))for(const auto& key:required->arr)if(!value.find(key.str.c_str()))throw std::runtime_error("Missing command parameter: "+key.str);
        const auto* properties=schema.find("properties");const auto* additional=schema.find("additionalProperties");
        for(const auto& [key,entry]:value.obj){
            if(key.empty()||key.size()>128||key.find('\0')!=std::string::npos)throw std::runtime_error("Invalid command parameter key");
            const auto* spec=properties?properties->find(key.c_str()):nullptr;
            if(!spec&&additional&&additional->t==V::Obj)spec=additional;
            if(!spec)throw std::runtime_error("Unknown command parameter: "+key);
            validate(entry,*spec,path+"."+key);
        }
    }
}
V bind(V value,const V& parameters){
    if(value.t==V::Str&&value.str.size()>1&&value.str[0]=='$'){
        const auto* supplied=parameters.find(value.str.substr(1).c_str());if(!supplied)throw std::runtime_error("Missing command parameter: "+value.str.substr(1));return *supplied;
    }
    if(value.t==V::Obj)for(auto& entry:value.obj)entry.second=bind(std::move(entry.second),parameters);
    if(value.t==V::Arr)for(auto& entry:value.arr)entry=bind(std::move(entry),parameters);
    return value;
}
V recipeEntry(const Recipe& recipe,bool full){
    V out=V::makeObj();out.set("id",V::makeStr(recipe.id));out.set("kind",V::makeStr("recipe"));out.set("summary",V::makeStr(recipe.summary));out.set("topic",V::makeStr(recipe.topic));
    const auto schema=json(recipe.schema);out.set("parameter_schema",schema);if(full)out.set("call_template",json(recipe.call));return out;
}
V toolEntry(const V& tool,bool full){
    V out=V::makeObj();const auto name=tool.find("name")->str;out.set("id",V::makeStr("tool:"+name));out.set("kind",V::makeStr("tool"));out.set("tool",V::makeStr(name));
    const auto description=tool.find("description")->str;out.set("summary",V::makeStr(full?description:description.substr(0,240)));out.set("annotations",*tool.find("annotations"));
    if(full)out.set("input_schema",*tool.find("inputSchema"));return out;
}
}
V agentCommandCatalog(const V& args){
    for(const auto& [key,value]:args.obj)if(key!="topic"&&key!="detail"&&key!="query"&&key!="limit"&&key!="command"&&key!="parameters")throw std::runtime_error("Unknown agent_onboard field: "+key);
    std::string query,command;size_t limit=8;
    if(const auto* q=args.find("query")){if(q->t!=V::Str||q->str.size()>128||q->str.find('\0')!=std::string::npos)throw std::runtime_error("commands query must be a string of at most 128 bytes");query=q->str;}
    if(const auto* c=args.find("command")){if(c->t!=V::Str||c->str.empty()||c->str.size()>128)throw std::runtime_error("commands command must be a nonempty identifier");command=c->str;}
    if(const auto* l=args.find("limit")){if(l->t!=V::Num||!std::isfinite(l->num)||l->num<1||l->num>16||std::floor(l->num)!=l->num)throw std::runtime_error("commands limit must be an integer from 1 to 16");limit=(size_t)l->num;}
    if(!query.empty()&&!command.empty())throw std::runtime_error("Use query or exact command, not both");
    if(args.find("parameters")&&command.empty())throw std::runtime_error("parameters require an exact recipe command");
    // Build once from the live registry: new tools remain discoverable without a
    // second manually maintained list, and no document snapshot enters the cache.
    static const V registry=AgentSession::tools(true);
    V out=V::makeObj();out.set("topic",V::makeStr("commands"));out.set("detail",V::makeStr("guide"));out.set("document_state_included",V::makeBool(false));out.set("read_only",V::makeBool(true));out.set("executed",V::makeBool(false));
    out.set("workflow",V::makeStr("Search query, inspect an exact command, then supply its parameters to prepare an ordinary tool call. Call the returned tool yourself. Recipes never execute. Read the current revision and selected object_context before edits. Mutation recipes prepare doc_preview; inspect its candidate and explicitly apply with doc_apply at the observed revision. Tool permissions, scoped access and native validation remain enforced by the host."));
    if(!command.empty()){
        for(const auto& recipe:recipes)if(command==recipe.id){
            out.set("command",recipeEntry(recipe,true));
            if(const auto* parameters=args.find("parameters")){validate(*parameters,json(recipe.schema),"parameters");out.set("prepared_call",bind(json(recipe.call),*parameters));}
            return out;
        }
        for(const auto& tool:registry.arr)if(command=="tool:"+tool.find("name")->str){
            if(args.find("parameters"))throw std::runtime_error("Tool contracts are documentation; call the actual tool with its arguments");
            out.set("command",toolEntry(tool,true));return out;
        }
        throw std::runtime_error("Unknown command identifier; search agent_onboard topic commands first");
    }
    struct Match {int score;std::string id;V entry;};std::vector<Match> matches;
    for(const auto& recipe:recipes){int rank=score(query,recipe.id,std::string(recipe.summary)+" "+recipe.topic);if(rank)matches.push_back({rank+1,recipe.id,recipeEntry(recipe,false)});}
    for(const auto& tool:registry.arr){const auto& id=tool.find("name")->str;int rank=score(query,id,tool.find("description")->str);if(rank)matches.push_back({rank,id,toolEntry(tool,false)});}
    std::sort(matches.begin(),matches.end(),[](const auto& a,const auto& b){return a.score!=b.score?a.score>b.score:a.id<b.id;});
    V entries=V::makeArr();for(size_t i=0;i<std::min(limit,matches.size());++i)entries.arr.push_back(std::move(matches[i].entry));
    out.set("query",V::makeStr(query));out.set("total_matches",V::makeNum((double)matches.size()));out.set("partial",V::makeBool(matches.size()>limit));out.set("commands",entries);return out;
}
}
