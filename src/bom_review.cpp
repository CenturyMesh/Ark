#include "ark/agent.hpp"
#include "ark/bom.hpp"
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
}
void AgentSession::observeBomInspection(const V& bom){auto hash=text(bom,"authoring_sha256");if(bomInspectedRevision_!=revision||bomInspectedHash_!=hash){bomInspectedItems_.clear();bomInspectedHash_=hash;bomInspectedRevision_=revision;}for(const auto& row:bom.find("items")->arr)bomInspectedItems_.insert((u64)row.find("feature")->num);}
arkjson::Value AgentSession::reviewBom(const V& args){
    for(const auto& entry:args.obj)if(entry.first!="expected_revision"&&entry.first!="scope"&&entry.first!="pending_notes"&&entry.first!="fastening_review"&&entry.first!="ids")throw std::runtime_error("Unknown BOM review field: "+entry.first);
    const auto* expected=args.find("expected_revision");if(!expected||expected->t!=V::Num||expected->num!=(double)revision)throw std::runtime_error("BOM review revision conflict; inspect the current BOM");
    const auto scope=text(args,"scope");if(scope!="concept"&&scope!="detailed")throw std::runtime_error("BOM scope must be concept or detailed; honor the user's requested scope");
    auto bom=inspectBom(document,revision);const bool selected=args.find("ids")!=nullptr;if(selected)bom=selectBomDefinitions(document,bom,*args.find("ids"));
    bool inspected=bomInspectedRevision_==revision&&bomInspectedHash_==text(bom,"authoring_sha256");for(const auto& row:bom.find("items")->arr)inspected&=bomInspectedItems_.count((u64)row.find("feature")->num)!=0;
    if(!inspected)throw std::runtime_error("Inspect every bom_inspect item page at the current revision before submitting bom_review");auto status=summary(bom);if(status.find("identity_fields_missing")->num||text(bom,"document_id").empty())throw std::runtime_error("BOM identity is incomplete. Assign part_number and description to every counted physical definition and initialize BOM identity before completion.");
    if(scope=="detailed"&&!bom.find("metadata_complete")->b)throw std::runtime_error("Detailed completion blocked: BOM metadata, sources or engineering review remain unresolved. Inspect bom_inspect, resolve with evidence, or report needs_input; do not invent commercial data.");
    const auto fastening=note(args,"fastening_review");std::string pending;if(!bom.find("metadata_complete")->b)pending=note(args,"pending_notes");else if(args.find("pending_notes"))pending=note(args,"pending_notes");
    V receipt=V::makeObj();receipt.set("revision",V::makeNum((double)revision));receipt.set("authoring_sha256",*bom.find("authoring_sha256"));receipt.set("scope",V::makeStr(scope));receipt.set("pending_notes",V::makeStr(pending));receipt.set("fastening_review",V::makeStr(fastening));receipt.set("review_summary",status);receipt.set("manufacturing_qualified",V::makeBool(false));receipt.set("global_review",V::makeBool(!selected));
    if(selected){receipt.set("selected_feature_ids",*bom.find("selected_feature_ids"));receipt.set("completion_authority",V::makeStr("selected definitions only; does not satisfy global BOM completion"));return receipt;}
    bomReview_=receipt;bomAuthored_=false;return bomReview_;
}
arkjson::Value AgentSession::bomCompletionCheck(bool onlyIfAuthored)const{
    V out=V::makeObj();out.set("allowed",V::makeBool(true));out.set("summary",V::makeStr(""));
    if(onlyIfAuthored&&!bomAuthored_&&bomReview_.isNull())return out;
    // Until BOM work is in play (identity initialized, part metadata declared or a
    // review submitted), a concept goal completes without a BOM review. Geometry
    // alone is not a request for procurement data.
    const bool bomInPlay=!document.bomDocumentId.empty()||!bomReview_.isNull()||std::any_of(document.features.begin(),document.features.end(),[](const Feature& feature){return feature.part.t==V::Obj&&!feature.part.obj.empty();});
    if(!bomInPlay)return out;
    const auto bom=inspectBom(document,revision);if(bom.find("items")->arr.empty())return out;out.set("revision",V::makeNum((double)revision));out.set("review_summary",summary(bom));
    const auto* reviewed=bomReview_.find("revision");bool current=reviewed&&reviewed->num==(double)revision&&text(bomReview_,"authoring_sha256")==text(bom,"authoring_sha256");
    if(!current){out.set("allowed",V::makeBool(false));out.set("error",V::makeStr("BOM completion blocked at revision "+std::to_string(revision)+": inspect bom_inspect and submit bom_review for this revision, with scope, pending_notes for unresolved fields and fastening_review. Part numbers/descriptions are required. Detailed scope rejects unresolved metadata. Use needs_input for missing required specifications; never invent materials or supplier data."));return out;}
    out.set("scope",*bomReview_.find("scope"));out.set("metadata_complete",*bom.find("metadata_complete"));std::string message=text(bomReview_,"scope")=="concept"?"BOM: concept review; ":"BOM: detailed metadata review; ";message+=std::to_string(bom.find("items")->arr.size())+" definitions, "+std::to_string(bom.find("occurrences")->arr.size())+" occurrences. ";if(!bom.find("metadata_complete")->b)message+="INCOMPLETE: "+text(bomReview_,"pending_notes")+". ";message+="Fastenings: "+text(bomReview_,"fastening_review")+". Not manufacturing qualification.";out.set("summary",V::makeStr(message));return out;
}
}
