#include "ark/authoring_023b.hpp"
#include "ark/cad_import.hpp"
#include "ark/design_containers.hpp"
#include "ark/holes_orchestrator.hpp"
#include <cmath>
#include <stdexcept>
namespace ark {
namespace {
using V=arkjson::Value;
std::string text(const V& value,const char* key,const std::string& fallback={}){const auto* item=value.find(key);if(!item)return fallback;if(item->t!=V::Str||item->str.empty())throw std::runtime_error(std::string(key)+" must be nonempty text");return item->str;}
void observed(const AgentSession& session,const V& args){const auto* expected=args.find("expected_revision");if(!expected||expected->t!=V::Num||expected->num!=(double)session.revision)throw std::runtime_error("Revision conflict; inspect doc_info and supply expected_revision");}
V apply(AgentSession& session,const V& commands){V args=V::makeObj();args.set("expected_revision",V::makeNum((double)session.revision));args.set("commands",commands);auto result=session.call("doc_apply",args);if(result.find("isError")&&result.find("isError")->b)throw std::runtime_error(result.find("structuredContent")->dump());return *result.find("structuredContent");}
void addTool(V& list,const char* name,const char* description,const char* json,bool read){V item=V::makeObj(),schema,annotations=V::makeObj();if(!arkjson::parse(json,schema))throw std::runtime_error("Invalid 023B tool schema");item.set("name",V::makeStr(name));item.set("description",V::makeStr(description));item.set("inputSchema",schema);annotations.set("readOnlyHint",V::makeBool(read));item.set("annotations",annotations);list.arr.push_back(item);}
}
bool callAuthoring023Tool(AgentSession& session,const std::string& tool,const V& args,V& out){
    if(tool=="holes_catalog"){out=holesCatalog(args);return true;}
    if(tool=="cad_inspect"){
        if(args.find("path")&&args.find("id"))throw std::runtime_error("Choose path or imported feature id, not both");
        if(const auto* id=args.find("id")){if(id->t!=V::Num||!std::isfinite(id->num)||id->num<1||id->num>9007199254740991.0||id->num!=std::floor(id->num))throw std::runtime_error("Imported feature id must be a positive integer");const auto* feature=session.document.find((u64)id->num);if(!feature)throw std::runtime_error("Imported CAD feature not found");const Body* candidate=nullptr;if(const auto* compare=args.find("compare_id")){if(compare->t!=V::Num||!std::isfinite(compare->num)||compare->num<1||compare->num>9007199254740991.0||compare->num!=std::floor(compare->num))throw std::runtime_error("compare_id must be a positive feature id");for(const auto& body:session.document.evalBodies)if(body.id==(u64)compare->num)candidate=&body;if(!candidate)throw std::runtime_error("Comparison feature has no current exact geometry");}out=inspectImportedCad(*feature,candidate);out.set("id",*id);out.set("revision",V::makeNum((double)session.revision));}
        else{if(args.find("compare_id"))throw std::runtime_error("compare_id requires an imported feature id");out=inspectCadFile(text(args,"path"));}return true;
    }
    if(tool=="import_cad"){
        observed(session,args);const auto mode=text(args,"mode","import");if(mode!="import"&&mode!="arkitfy")throw std::runtime_error("CAD import mode must be import or arkitfy");
        auto receipt=buildCadImport(text(args,"path"),text(args,"name"));V commands=V::makeArr();commands.arr.push_back(*receipt.find("command"));out=apply(session,commands);
        const auto* ids=out.find("created_ids");if(!ids||ids->t!=V::Arr||ids->arr.empty())throw std::runtime_error("CAD import produced no feature identity");
        const auto featureId=(u64)ids->arr.back().asNum();out.set("created_id",V::makeNum((double)featureId));out.set("mode",V::makeStr(mode));
        for(const char* key:{"report","census","units","recognition","source_sha256","source_name","source_format"})if(const auto* value=receipt.find(key))out.set(key,*value);
        out.set("arkitfy_requested",V::makeBool(mode=="arkitfy"));out.set("reconstruction_complete",V::makeBool(false));
        if(mode=="arkitfy")out.set("agent_prompt",V::makeStr(cadImportArkitfyPrompt(receipt,featureId)));return true;
    }
    if(tool=="design_containers"){
        const auto action=text(args,"action","inspect");if(action=="inspect")out=inspectDesignContainers(session.document,session.revision,args);
        else if(action=="checkout"){observed(session,args);out=checkoutDesignContainer(session.document,session.revision,args);}
        else if(action=="merge"){
            observed(session,args);const auto proposal=proposeDesignContainerMerge(session.document,session.revision,args);
            if(const auto* preview=args.find("preview");preview&&preview->t!=V::Bool)throw std::runtime_error("Container preview must be boolean");
            if((args.find("preview")&&args.find("preview")->b)||proposal.find("commands")->arr.empty()){out=proposal;out.set("applied",V::makeBool(false));}
            else{out=apply(session,*proposal.find("commands"));if(const auto* map=proposal.find("id_map"))out.set("id_map",*map);out.set("applied",V::makeBool(true));}
        }else throw std::runtime_error("Container action must be inspect, checkout or merge");return true;
    }
    return false;
}
void appendAuthoring023Tools(V& list){
    appendHolesTools(list);
    addTool(list,"cad_inspect","Read exact STEP/IGES inventory and source units, or an embedded imported CAD feature by id. Recognition is evidence, not recovered design intent; all solids/surfaces remain source authority.",R"({"type":"object","properties":{"path":{"type":"string"},"id":{"type":"integer","minimum":1},"compare_id":{"type":"integer","minimum":1}},"oneOf":[{"required":["path"]},{"required":["id"]}],"additionalProperties":false})",true);
    addTool(list,"import_cad","Append an immutable, portable exact STEP/IGES reference with all supported bodies. mode arkitfy also returns an explicit agent reconstruction task; importing alone never claims reconstruction. Atomic, revision checked, no document replacement.",R"({"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"},"mode":{"type":"string","enum":["import","arkitfy"]},"expected_revision":{"type":"integer","minimum":0},"actor":{"type":"object"},"motivation":{"type":"string"}},"required":["path","expected_revision"],"additionalProperties":false})",false);
    addTool(list,"design_containers","Inspect assembly groups as isolated work containers; checkout a scoped editable .ark with read-only dependencies, then merge against the exact observed parent revision and source digest. No implicit rebase. preview returns merge commands without applying. Explicit path publishes a new local workspace file; opening it in another ARK isolates recomputation.",R"({"type":"object","properties":{"action":{"type":"string","enum":["inspect","checkout","merge"]},"node":{"type":["integer","string"]},"path":{"type":"string"},"document":{"type":"object"},"expected_revision":{"type":"integer","minimum":0},"preview":{"type":"boolean"},"expected_child_sha256":{"type":"string","pattern":"^[0-9a-f]{64}$"},"actor":{"type":"object"},"motivation":{"type":"string"}},"required":["action"],"additionalProperties":false})",false);
}
}
