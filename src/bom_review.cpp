#include "ark/agent.hpp"
#include "ark/bom.hpp"
#include "ark/assembly.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>
namespace ark {
namespace {
using V=arkjson::Value;
std::string text(const V& v,const char* k){auto* p=v.find(k);return p?p->asStr():"";}
V summary(const V& bom){V out=V::makeObj(),missing=V::makeObj();size_t incomplete=0,identityMissing=0;for(const auto& row:bom.find("items")->arr){if(!row.find("metadata_complete")->b)++incomplete;for(const auto& key:row.find("missing_or_review_required")->arr){const auto* n=missing.find(key.str.c_str());missing.set(key.str,V::makeNum(n?n->num+1:1));if(key.str=="part_number"||key.str=="description")++identityMissing;}}
    out.set("items",V::makeNum(bom.find("items")->arr.size()));out.set("occurrences",V::makeNum(bom.find("occurrences")->arr.size()));out.set("incomplete_items",V::makeNum(incomplete));out.set("identity_fields_missing",V::makeNum(identityMissing));out.set("missing_fields",missing);out.set("metadata_complete",*bom.find("metadata_complete"));out.set("fastener_quantity",*bom.find("fastener_quantity"));out.set("issues",*bom.find("issues"));out.set("unplaced_annotated_count",V::makeNum(bom.find("unplaced_annotated_definitions")->arr.size()));return out;}
std::string note(const V& args,const char* key){const auto* v=args.find(key);if(!v||v->t!=V::Str||v->str.size()>2048||v->str.find_first_not_of(" \t\r\n")==std::string::npos)throw std::runtime_error(std::string("Provide a nonempty bounded ")+key);return v->str;}
std::map<u64,std::string> taskDefinitions(const Document& document){
    const auto bom=inspectBom(document,0);V authored;
    if(!arkjson::parse(documentToJson(document,false,false),authored))throw std::runtime_error("BOM task authoring unavailable");
    std::map<u64,std::string> definitions;std::set<u64> nativeDefinitions;
    for(const auto& row:bom.find("items")->arr){const auto id=(u64)row.find("feature")->num;definitions[id]=row.dump();if(row.find("brep_base64_sha256"))nativeDefinitions.insert(id);}
    if(const auto* features=authored.find("features"))for(const auto& feature:features->arr){const auto id=(u64)feature.find("id")->num;if(definitions.count(id))definitions[id]+=feature.dump();}
    for(const auto& row:bom.find("occurrences")->arr)definitions.at((u64)row.find("feature")->num)+=row.dump();
    // Native B-rep hashes bind evaluated geometry without treating conservative
    // cache invalidation as an actual change to every later operation. For core
    // geometry, evaluation keys conservatively cover parameter/input changes.
    for(auto& [id,value]:definitions){if(!nativeDefinitions.count(id)){const auto cached=document.evaluationCache.find(id);if(cached!=document.evaluationCache.end())value+=cached->second.key;}const auto* feature=document.find(id);if(feature)if(const auto* materials=authored.find("materials"))for(const auto& material:materials->arr)if(text(material,"name")==feature->material)value+=material.dump();}
    std::vector<AssemblyPlacement> placements;std::string error;
    if(!document.assembly.empty()&&!resolveAssembly(document,placements,error))throw std::runtime_error(error);
    for(const auto& placement:placements)if(definitions.count(placement.feature)){V matrix=V::makeArr();for(const auto coordinate:placement.world.m)matrix.arr.push_back(V::makeNum(coordinate));definitions[placement.feature]+=matrix.dump();}
    for(auto& [id,value]:definitions)value=iterationSha256(value);
    return definitions;
}
bool coversTask(const V& task,const V& selected){
    if(!task.find("active")->b||!task.find("removed_feature_ids")->arr.empty())return false;
    std::set<u64> ids;for(const auto& id:selected.arr)ids.insert((u64)id.num);
    for(const auto& id:task.find("affected_feature_ids")->arr)if(!ids.count((u64)id.num))return false;
    return true;
}
V recovery(const V& bom,bool inspectionCurrent,const std::set<u64>& inspected){
    V out=V::makeObj(),unread=V::makeArr(),identities=V::makeArr();size_t unreadCount=0,identityCount=0;
    for(const auto& row:bom.find("items")->arr){
        const auto id=(u64)row.find("feature")->num;
        if(!inspectionCurrent||!inspected.count(id)){++unreadCount;if(unread.arr.size()<64)unread.arr.push_back(V::makeNum((double)id));}
        V fields=V::makeArr();for(const auto& field:row.find("missing_or_review_required")->arr)if(field.str=="part_number"||field.str=="description")fields.arr.push_back(field);
        if(!fields.arr.empty()){++identityCount;if(identities.arr.size()<32){V entry=V::makeObj();entry.set("feature",*row.find("feature"));entry.set("name",*row.find("name"));entry.set("fields",fields);identities.arr.push_back(entry);}}
    }
    out.set("expected_revision",*bom.find("revision"));out.set("uninspected_count",V::makeNum((double)unreadCount));out.set("missing_identity_count",V::makeNum((double)identityCount));out.set("missing_identity",identities);out.set("missing_identity_truncated",V::makeBool(identityCount>identities.arr.size()));out.set("unresolved_fields",*summary(bom).find("missing_fields"));
    out.set("document_identity_missing",V::makeBool(text(bom,"document_id").empty()));
    out.set("issues",*bom.find("issues"));out.set("unplaced_annotated_count",V::makeNum(bom.find("unplaced_annotated_definitions")->arr.size()));
    V next=V::makeObj(),args=V::makeObj();
    if(unreadCount){next.set("tool",V::makeStr("bom_inspect"));args.set("ids",unread);args.set("occurrence_limit",V::makeNum(1));next.set("args",args);}
    else next.set("instruction",V::makeStr(identityCount?"Use revision-checked feature.edit part_patch to assign missing internal part_number/description on these counted definitions. Preserve known metadata. Reinspect after editing.":"Submit bom_review at this revision with the user's scope and actual fastening evidence; disclose pending fields for concept, or obtain missing required specifications for detailed scope."));
    out.set("next_action",next);
    std::string progress=text(bom,"authoring_sha256")+"/"+text(bom,"document_id")+"/"+bom.find("revision")->dump();if(inspectionCurrent)for(auto id:inspected)progress+="/"+std::to_string(id);
    out.set("progress_key",V::makeStr(iterationSha256(progress)));return out;
}
}
void AgentSession::beginBomTask(bool continuation){
    if(continuation&&bomTaskActive_)return;
    bomTaskReview_={};bomTaskBaseline_.clear();bomTaskActive_=false;bomTaskError_.clear();
    try{bomTaskBaseline_=taskDefinitions(document);bomTaskActive_=true;}catch(const std::exception& error){bomTaskError_=error.what();}
}
arkjson::Value AgentSession::bomTaskScope()const{
    V out=V::makeObj(),affected=V::makeArr(),removed=V::makeArr();out.set("active",V::makeBool(bomTaskActive_));out.set("revision",V::makeNum((double)revision));if(!bomTaskError_.empty())out.set("baseline_error",V::makeStr(bomTaskError_));
    if(bomTaskActive_){const auto current=taskDefinitions(document);for(const auto& [id,value]:current){const auto before=bomTaskBaseline_.find(id);if(before==bomTaskBaseline_.end()||before->second!=value)affected.arr.push_back(V::makeNum((double)id));}for(const auto& [id,value]:bomTaskBaseline_)if(!current.count(id))removed.arr.push_back(V::makeNum((double)id));}
    out.set("affected_feature_ids",affected);out.set("removed_feature_ids",removed);out.set("authority",V::makeStr("Host-observed task delta. Explicit completion:task requires all affected counted definitions; removals require project review. No global completion authority."));return out;
}
void AgentSession::observeBomInspection(const V& bom){auto hash=text(bom,"authoring_sha256");if(bomInspectedRevision_!=revision||bomInspectedHash_!=hash){bomInspectedItems_.clear();bomInspectedHash_=hash;bomInspectedRevision_=revision;}for(const auto& row:bom.find("items")->arr)bomInspectedItems_.insert((u64)row.find("feature")->num);}
arkjson::Value AgentSession::reviewBom(const V& args){
    for(const auto& entry:args.obj)if(entry.first!="expected_revision"&&entry.first!="scope"&&entry.first!="pending_notes"&&entry.first!="fastening_review"&&entry.first!="ids"&&entry.first!="completion")throw std::runtime_error("Unknown BOM review field: "+entry.first);
    const auto completion=args.find("completion")?text(args,"completion"):"project";
    if(completion!="project"&&completion!="task")throw std::runtime_error("BOM completion must be project or task");
    const auto* expected=args.find("expected_revision");if(!expected||expected->t!=V::Num||expected->num!=(double)revision)throw std::runtime_error("BOM review revision conflict; inspect the current BOM");
    const auto scope=text(args,"scope");if(scope!="concept"&&scope!="detailed")throw std::runtime_error("BOM scope must be concept or detailed; honor the user's requested scope");
    auto bom=inspectBom(document,revision);const bool selected=args.find("ids")!=nullptr||completion=="task";
    if(const auto* ids=args.find("ids"))bom=selectBomDefinitions(document,bom,*ids);
    else if(completion=="task"){
        const auto task=bomTaskScope();const auto& ids=*task.find("affected_feature_ids");
        if(!ids.arr.empty())bom=selectBomDefinitions(document,bom,ids);
        else{for(const char* field:{"items","occurrences","groups","unplaced_annotated_definitions"})bom.set(field,V::makeArr());bom.set("selected_feature_ids",V::makeArr());bom.set("fastener_quantity",V::makeNum(0));bom.set("metadata_complete",V::makeBool(bom.find("issues")->arr.empty()));}
    }
    if(completion=="task"&&(!selected||!coversTask(bomTaskScope(),*bom.find("selected_feature_ids"))))throw std::runtime_error("Task BOM review requires an active host task and ids covering every affected definition from bom_inspect.task_scope; removed definitions require project review. Never downgrade a requested project-wide deliverable.");
    bool inspected=bomInspectedRevision_==revision&&bomInspectedHash_==text(bom,"authoring_sha256");for(const auto& row:bom.find("items")->arr)inspected&=bomInspectedItems_.count((u64)row.find("feature")->num)!=0;
    if(!inspected)throw std::runtime_error("BOM inspection is incomplete at this revision. "+recovery(bom,bomInspectedRevision_==revision&&bomInspectedHash_==text(bom,"authoring_sha256"),bomInspectedItems_).dump());auto status=summary(bom);if(status.find("identity_fields_missing")->num||text(bom,"document_id").empty())throw std::runtime_error("BOM identity is incomplete. Patch missing internal part_number/description without replacing other fields. "+recovery(bom,true,bomInspectedItems_).dump());
    if(scope=="detailed"&&!bom.find("metadata_complete")->b)throw std::runtime_error("Detailed completion blocked: BOM metadata, sources or engineering review remain unresolved. Inspect bom_inspect, resolve with evidence, or report needs_input; do not invent commercial data.");
    const auto fastening=note(args,"fastening_review");std::string pending;if(!bom.find("metadata_complete")->b)pending=note(args,"pending_notes");else if(args.find("pending_notes"))pending=note(args,"pending_notes");
    V receipt=V::makeObj();receipt.set("revision",V::makeNum((double)revision));receipt.set("authoring_sha256",*bom.find("authoring_sha256"));receipt.set("scope",V::makeStr(scope));receipt.set("pending_notes",V::makeStr(pending));receipt.set("fastening_review",V::makeStr(fastening));receipt.set("review_summary",status);receipt.set("manufacturing_qualified",V::makeBool(false));receipt.set("global_review",V::makeBool(!selected));
    if(selected){receipt.set("selected_feature_ids",*bom.find("selected_feature_ids"));receipt.set("completion_authority",V::makeStr("selected definitions only; does not satisfy global BOM completion"));receipt.set("completion",V::makeStr(completion));if(completion=="task"){receipt.set("task_scope",bomTaskScope());bomTaskReview_=receipt;}return receipt;}
    bomReview_=receipt;bomAuthored_=false;return bomReview_;
}
arkjson::Value AgentSession::bomCompletionCheck(bool onlyIfAuthored,bool allowTaskReview)const{
    V out=V::makeObj();out.set("allowed",V::makeBool(true));out.set("summary",V::makeStr(""));
    if(onlyIfAuthored&&!bomAuthored_&&bomReview_.isNull()&&bomTaskReview_.isNull())return out;
    // Until BOM work is in play (identity initialized, part metadata declared or a
    // review submitted), a concept goal completes without a BOM review. Geometry
    // alone is not a request for procurement data.
    const bool bomInPlay=!document.bomDocumentId.empty()||!bomReview_.isNull()||std::any_of(document.features.begin(),document.features.end(),[](const Feature& feature){return feature.part.t==V::Obj&&!feature.part.obj.empty();});
    if(!bomInPlay)return out;
    const auto bom=inspectBom(document,revision);if(bom.find("items")->arr.empty())return out;out.set("revision",V::makeNum((double)revision));out.set("review_summary",summary(bom));
    const auto* reviewed=bomReview_.find("revision");bool current=reviewed&&reviewed->num==(double)revision&&text(bomReview_,"authoring_sha256")==text(bom,"authoring_sha256");
    if(!current){
        const auto* taskRevision=bomTaskReview_.find("revision");
        if(allowTaskReview&&taskRevision&&taskRevision->num==(double)revision&&text(bomTaskReview_,"authoring_sha256")==text(bom,"authoring_sha256")&&coversTask(bomTaskScope(),*bomTaskReview_.find("selected_feature_ids"))){
            out.set("completion",V::makeStr("task"));out.set("global_review",V::makeBool(false));out.set("project_completion_allowed",V::makeBool(false));out.set("selected_feature_ids",*bomTaskReview_.find("selected_feature_ids"));
            out.set("summary",V::makeStr("BOM: limited task review ("+text(bomTaskReview_,"scope")+"). Global project/BOM completion remains pending. Not manufacturing qualification."));return out;
        }
        auto repair=recovery(bom,bomInspectedRevision_==revision&&bomInspectedHash_==text(bom,"authoring_sha256"),bomInspectedItems_);
        if(allowTaskReview)repair.set("task_scope",bomTaskScope());
        out.set("allowed",V::makeBool(false));out.set("recovery",repair);out.set("progress_key",*repair.find("progress_key"));
        const auto counts=std::to_string((size_t)repair.find("uninspected_count")->num)+" definitions still need current inspection; "+std::to_string((size_t)repair.find("missing_identity_count")->num)+" definitions lack internal identity fields.";
        out.set("user_message",V::makeStr("BOM review pending at revision "+std::to_string(revision)+". "+counts+" Prior CAD changes are retained. Project completion has not been approved."));
        out.set("error",V::makeStr("BOM completion blocked at revision "+std::to_string(revision)+". "+counts+" Follow recovery.next_action and load agent_onboard topic bom for the field contract. Use part_patch for partial metadata edits. For an explicitly limited task, bom_review{completion:task,...} without ids selects the host-observed affected definitions, including an empty set for display-only edits; global BOM remains pending. Honor requested concept/detailed scope. A needs_input reply is permitted for an actual missing requirement; never invent sourcing or claim completion."));return out;}
    out.set("scope",*bomReview_.find("scope"));out.set("metadata_complete",*bom.find("metadata_complete"));std::string message=text(bomReview_,"scope")=="concept"?"BOM: concept review; ":"BOM: detailed metadata review; ";message+=std::to_string(bom.find("items")->arr.size())+" definitions, "+std::to_string(bom.find("occurrences")->arr.size())+" occurrences. ";if(!bom.find("metadata_complete")->b)message+="INCOMPLETE; unresolved data recorded in the review. ";message+="Not manufacturing qualification.";out.set("summary",V::makeStr(message));return out;
}
}
