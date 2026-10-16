#include "ark/agent.hpp"
#include "ark/manufacturing.hpp"
#include <map>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
V canonical(V value){if(value.t==V::Obj){for(auto& item:value.obj)item.second=canonical(std::move(item.second));std::sort(value.obj.begin(),value.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});}else if(value.t==V::Arr)for(auto& item:value.arr)item=canonical(std::move(item));return value;}
std::string processKey(const V& value){return canonical(value.t==V::Obj?value:V::makeObj()).dump();}
const V& need(const V& value,const char* key){const auto* field=value.find(key);if(!field)throw std::runtime_error(std::string("Missing field: ")+key);return *field;}
const V& component(const V& report,u64 id){for(const auto& row:need(report,"components").arr)if(need(row,"id").num==(double)id)return row;throw std::runtime_error("DFM component missing after edit");}
const V& rule(const V& row,const std::string& id){for(const auto& r:need(need(row,"dfm"),"rules").arr)if(need(r,"id").str==id)return r;throw std::runtime_error("DFM rule is not applicable to this component's declared process");}
void exact(const V& row){const auto& geometry=need(row,"geometry");const auto* valid=geometry.find("brep_valid"),*solids=geometry.find("solids"),*volume=geometry.find("volume_mm3");if(!need(geometry,"exact_geometry").b||!valid||!valid->b||!solids||solids->num<1||!volume||volume->num<=0)throw std::runtime_error("DFM proposal requires current valid positive-volume exact B-rep evidence");}
void bounded(const V& commands){
    if(commands.t!=V::Arr||commands.arr.empty()||commands.arr.size()>8||commands.dump().size()>65536)throw std::runtime_error("DFM proposal requires 1..8 editable commands within 64 KiB");
    const std::map<std::string,std::set<std::string>> fields{
        {"feature.edit",{"op","id","p"}}, {"param.set",{"op","name","expr"}},
        {"sketch.patch",{"op","name","entity","anchor","x","y","r","a0","a1","segment","role"}},
        {"sketch.fillet",{"op","name","entity","corner","radius"}},
        {"sketch.chamfer",{"op","name","entity","corner","distance"}},
        {"sheet.base.edit",{"op","id","values"}}, {"sheet.flange.edit",{"op","id","entity","values"}},
        {"sheet.opening.edit",{"op","id","entity","values"}}, {"sheet.hem.edit",{"op","id","entity","values"}},
        {"sheet.jog.edit",{"op","id","entity","values"}}, {"sheet.sketch_bend.edit",{"op","id","entity","values"}},
        {"frame.weld.edit",{"op","id","entity","values"}}
    };
    for(const auto& command:commands.arr){if(command.t!=V::Obj||need(command,"op").t!=V::Str)throw std::runtime_error("DFM editable command requires a typed operation");const auto found=fields.find(need(command,"op").str);if(found==fields.end())throw std::runtime_error("DFM proposal operation is outside the bounded editable design scope");for(const auto& field:command.obj)if(!found->second.count(field.first))throw std::runtime_error("DFM proposal cannot replace process, material, source programs or definitions; unsupported edit field: "+field.first);for(const char* key:{"p","values"})if(const auto* values=command.find(key);values&&values->t==V::Obj)for(const auto& value:values->obj)if(value.first.find("density")!=std::string::npos||value.first.find("tol")!=std::string::npos||value.first=="material"||value.first=="sample"||value.first=="epsilon")throw std::runtime_error("DFM proposal cannot change material, sampling or comparison tolerance inputs");}
}
}
V AgentSession::proposeManufacturing(const V& args){
    const auto& observed=need(args,"expected_revision");if(observed.t!=V::Num||observed.num!=(double)revision)throw std::runtime_error("Revision conflict; read doc_info with detail design and retry");
    for(const auto& field:args.obj)if(field.first!="expected_revision"&&field.first!="id"&&field.first!="rule_id"&&field.first!="rule_version"&&field.first!="commands"&&field.first!="checks")throw std::runtime_error("Unknown DFM proposal field: "+field.first);
    const auto& selector=need(args,"id");u64 id=0;for(const auto& feature:document.features)if((selector.t==V::Str&&selector.str==feature.name)||(selector.t==V::Num&&selector.num==(double)feature.id)){if(id)throw std::runtime_error("Ambiguous DFM component");id=feature.id;}if(!id)throw std::runtime_error("DFM component not found");
    const auto& ruleId=need(args,"rule_id"),&version=need(args,"rule_version");if(ruleId.t!=V::Str||ruleId.str.empty()||version.t!=V::Num||version.num<1||version.num!=std::floor(version.num))throw std::runtime_error("DFM recommendation needs rule_id and integer rule_version");
    bounded(need(args,"commands"));
    V all=V::makeObj();V before=canonical(inspectManufacturing(document,all,revision));const auto& beforeRow=component(before,id);exact(beforeRow);const auto& target=rule(beforeRow,ruleId.str);
    if(need(target,"version").num!=version.num)throw std::runtime_error("DFM rule version conflict; inspect current manufacturing recommendations");
    const auto& baselineDfm=need(beforeRow,"dfm");const V* recommendation=nullptr;for(const auto& item:need(baselineDfm,"recommendations").arr)if(need(item,"rule_id").str==ruleId.str&&need(item,"rule_version").num==version.num)recommendation=&item;
    if(!recommendation)throw std::runtime_error("DFM proposal requires an actual current unresolved recommendation");
    V recipe=V::makeObj();recipe.set("expected_revision",observed);recipe.set("commands",need(args,"commands"));recipe.set("detail",V::makeStr("changes"));if(const auto* checks=args.find("checks"))recipe.set("checks",*checks);
    V out=preparePreview(recipe);out.set("document_unchanged",V::makeBool(true));out.set("manufacturing_qualified",V::makeBool(false));out.set("automatic_edit",V::makeBool(false));out.set("acceptance_required",V::makeBool(true));
    if(!need(out,"valid").b)return out;
    if(!out.find("candidate_cached")||!out.find("candidate_cached")->b){out.obj.erase(std::remove_if(out.obj.begin(),out.obj.end(),[](const auto& field){return field.first=="apply";}),out.obj.end());out.set("valid",V::makeBool(false));out.set("proposal_error",V::makeStr("DFM comparison requires a retained candidate; cache capacity exceeded"));return out;}
    const std::string candidate=need(out,"candidate_id").str;auto found=std::find_if(prepared_.begin(),prepared_.end(),[&](const Prepared& p){return p.id==candidate;});
    try{
        const auto& next=*found->document;
        if(next.features.size()!=document.features.size())throw std::runtime_error("DFM proposal must preserve all component definitions");
        bool changed=false;for(const auto& feature:document.features){const Feature* retained=nullptr;for(const auto& f:next.features)if(f.id==feature.id)retained=&f;if(!retained||retained->manufacturingRoute!=feature.manufacturingRoute||processKey(retained->manufacturingProcess)!=processKey(feature.manufacturingProcess)||retained->material!=feature.material||retained->suppressed!=feature.suppressed)throw std::runtime_error("DFM comparison requires identical process, material and active-definition inputs");}
        changed=documentToJson(document,false,false)!=documentToJson(next,false,false);if(!changed)throw std::runtime_error("DFM proposal contains no editable design change");
        const Body* originalBody=nullptr;const Body* candidateBody=nullptr;for(const auto& b:document.evalBodies)if(b.id==id)originalBody=&b;for(const auto& b:next.evalBodies)if(b.id==id)candidateBody=&b;if(!originalBody||!candidateBody||need(originalBody->nativeData,"brep_base64").dump()==need(candidateBody->nativeData,"brep_base64").dump())throw std::runtime_error("DFM proposal must change the selected component exact geometry");
        V after=canonical(inspectManufacturing(next,all,revision));after.set("committed",V::makeBool(false));exact(component(after,id));const auto& afterTarget=rule(component(after,id),ruleId.str);
        V comparison=V::makeObj(),transitions=V::makeArr(),unresolved=V::makeArr();
        for(const auto& row:need(after,"components").arr){const u64 componentId=(u64)need(row,"id").num;const auto& prior=component(before,componentId);for(const auto& r:need(need(row,"dfm"),"rules").arr){const auto& was=rule(prior,need(r,"id").str);V transition=V::makeObj();transition.set("feature_id",need(row,"id"));transition.set("rule_id",need(r,"id"));transition.set("before_status",need(was,"status"));transition.set("after_status",need(r,"status"));transition.set("evidence_changed",V::makeBool(was.dump()!=r.dump()));transitions.arr.push_back(transition);if(need(r,"status").str=="unknown"||need(r,"status").str=="incompatible"){V pending=V::makeObj();pending.set("feature_id",need(row,"id"));pending.set("rule",r);unresolved.arr.push_back(pending);}}}
        comparison.set("schema_version",V::makeNum(1));V milestones=V::makeArr();milestones.arr={V::makeStr("C.03"),V::makeStr("D.02")};comparison.set("milestones",milestones);comparison.set("candidate_id",V::makeStr(candidate));comparison.set("baseline_revision",observed);comparison.set("feature_id",V::makeNum((double)id));comparison.set("recommendation",*recommendation);comparison.set("ruleset_id",need(baselineDfm,"ruleset_id"));comparison.set("ruleset_version",need(baselineDfm,"ruleset_version"));comparison.set("same_process_inputs",V::makeBool(true));comparison.set("before",before);comparison.set("after",after);comparison.set("rule_transitions",transitions);comparison.set("unresolved",unresolved);comparison.set("target_screen_resolved",V::makeBool(need(target,"status").str=="incompatible"&&need(afterTarget,"status").str=="compatible"));comparison.set("manufacturing_qualified",V::makeBool(false));comparison.set("cost_improvement_claimed",V::makeBool(false));comparison.set("design_improvement_claimed",V::makeBool(false));comparison.set("scope",V::makeStr("Exact editable design candidate against identical caller process/material inputs at one baseline revision; bounded screen changes are not functional design approval, cost optimization or production qualification."));
        const size_t retainedBytes=2*comparison.dump().size();size_t used=retainedBytes;for(const auto& p:prepared_)used+=p.bytes;if(used>128u*1024u*1024u)throw std::runtime_error("DFM comparison exceeds the retained candidate cache capacity");found->bytes+=retainedBytes;found->requiresDfmAcceptance=true;found->dfmComparison=comparison;out.set("refresh_supported",V::makeBool(false));out.set("refresh_reason",V::makeStr("Manufacturing proposals require a new explicit proposal preview"));out.set("dfm_comparison",comparison);V apply=need(out,"apply");apply.set("accept_dfm",V::makeBool(true));out.set("apply",apply);return out;
    }catch(...){prepared_.erase(std::remove_if(prepared_.begin(),prepared_.end(),[&](const Prepared& p){return p.id==candidate;}),prepared_.end());throw;}
}
}
