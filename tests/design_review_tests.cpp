#include "ark/design_review.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace ark;using V=arkjson::Value;
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static V json(const std::string& s){V v;check(arkjson::parse(s,v),"fixture JSON");return v;}
static const V& field(const V& v,const char* k){static V n;const auto* p=v.find(k);return p?*p:n;}
static V invoke(AgentSession& s,const std::string& tool,V args){args.set("expected_revision",V::makeNum((double)s.revision));auto r=s.call(tool,args);if(field(r,"isError").b)throw std::runtime_error(r.dump());return field(r,"structuredContent");}
static void apply(AgentSession& s,const std::string& commands){V args=V::makeObj();args.set("commands",json(commands));invoke(s,"doc_apply",args);}
static V review(AgentSession& s,const char* action){V a=V::makeObj();a.set("action",V::makeStr(action));return invoke(s,"design_review",a);}
int main(){try{
    AgentSession s;
    apply(s,R"([{"op":"param.set","name":"width","expr":"10"},{"op":"feature.add","type":"native_solid","name":"Block","native":{"op":"box"},"p":{"width":"width","depth":8,"height":6},"part":{"description":"Review fixture","part_number":"R-001"}},{"op":"assembly.add","feature":1,"name":"Block occurrence"}])");
    const auto id=field(iterationInfo(s.document),"head_id").asStr();
    {Document reordered=s.document;std::reverse(reordered.find(1)->part.obj.begin(),reordered.find(1)->part.obj.end());const auto same=compareDesignRevisions(reordered,s.revision,id);check(field(same,"changed").num==0,"JSON object key order must not create a false revision change");check(field(same,"bom_changes").arr.empty(),"JSON object key order must not create false BOM differences");}
    apply(s,R"([{"op":"param.set","name":"width","expr":"14"},{"op":"assembly.add","feature":1,"name":"Second occurrence","p":{"tx":30}}])");
    const auto saved=documentToJson(s.document);const auto revision=s.revision;V args=V::makeObj();args.set("action",V::makeStr("compare"));args.set("before_id",V::makeStr(id));auto diff=invoke(s,"design_review",args);
    check(field(diff,"changed").num==1,"Driven dimension change must change geometry even when the feature recipe is identical");check(field(diff,"parameter_changes").arr.size()==1,"Named parameter difference is explicit");check(documentToJson(s.document)==saved&&s.revision==revision,"Compare must not publish or change journal");
    check(field(field(field(diff,"features").arr[0],"after_geometry"),"geometry_authority").asStr()=="native_brep","Exact comparison uses native digest");
    const auto beforeItems=field(field(diff,"bom_before"),"items"),afterItems=field(field(diff,"bom_after"),"items");check(field(beforeItems.arr[0],"quantity").num==1&&field(afterItems.arr[0],"quantity").num==2,"BOM counts preserve revision-specific occurrences");
    args.set("before_id",V::makeStr("missing-history-id"));args.set("expected_revision",V::makeNum((double)s.revision));check(field(s.call("design_review",args),"isError").b,"Unknown history cannot silently fall back");
    args.set("before_id",V::makeStr(id));args.set("expected_revision",V::makeNum((double)s.revision-1));check(field(s.call("design_review",args),"isError").b,"Stale review must reject");
    check(field(review(s,"diagnose"),"failures").arr.empty(),"Valid model has no artificial repair findings");
    // External/native loading can expose a failed editable source. The repair
    // path must recover it through the same whole-document preview transaction.
    s.document.find(1)->p["width"]="unbound_width";s.document.iterationHistory=V();s.document.recompute();const auto broken=documentToJson(s.document);auto diagnostics=review(s,"diagnose");check(field(diagnostics,"failures").arr.size()==1,"Failed source is diagnosed");
    V repair=json(R"({"action":"prepare_repair","id":1,"command":{"op":"feature.edit","id":1,"p":{"width":"width"}}})");repair.set("source_sha256",field(diagnostics,"source_sha256"));auto candidate=invoke(s,"design_review",repair);check(!field(candidate,"candidate_id").asStr().empty(),"Repair returns a retained reviewed candidate");check(documentToJson(s.document)==broken&&s.revision==revision,"Preparing repair must leave broken authoring untouched");
    V forbidden=repair;forbidden.set("command",json(R"({"op":"feature.edit","id":1,"suppressed":true})"));forbidden.set("expected_revision",V::makeNum((double)s.revision));check(field(s.call("design_review",forbidden),"isError").b,"Suppression must not masquerade as repair");
    V stale=repair;stale.set("source_sha256",V::makeStr(std::string(64,'0')));stale.set("expected_revision",V::makeNum((double)s.revision));check(field(s.call("design_review",stale),"isError").b,"Source fingerprint protects repair");
    V loosen=repair;loosen.set("command",json(R"({"op":"feature.edit","id":1,"p":{"width":"width","tolerance":10}})"));loosen.set("expected_revision",V::makeNum((double)s.revision));check(field(s.call("design_review",loosen),"isError").b,"Repair cannot bypass failure with a relaxed tolerance");
    stale=repair;stale.set("expected_revision",V::makeNum((double)s.revision-1));check(field(s.call("design_review",stale),"isError").b,"Repair rejects stale revision independently of fingerprint");
    V publish=V::makeObj();publish.set("candidate_id",field(candidate,"candidate_id"));invoke(s,"doc_apply",publish);check(s.revision==revision+1&&s.document.features.size()==1,"Reviewed repair publishes exactly once without dropping solids");check(field(review(s,"diagnose"),"failures").arr.empty(),"Repair resolves current failure");check(s.document.assembly.size()==2,"Repair preserves assembly occurrences");
    AgentSession rejected;
    apply(rejected,R"([{"op":"feature.add","type":"native_solid","name":"Everyday block","native":{"op":"box"},"p":{"width":10,"depth":8,"height":6}}])");
    const auto validSource=documentToJson(rejected.document);const auto validRevision=rejected.revision;
    V bad=json(R"({"commands":[{"op":"feature.edit","id":1,"p":{"width":-10}}]})");bad.set("expected_revision",V::makeNum((double)validRevision));const auto failure=rejected.call("doc_apply",bad);check(field(failure,"isError").b,"Everyday negative dimension edit rejects");check(documentToJson(rejected.document)==validSource&&rejected.revision==validRevision,"Rejected edit retains the current valid source");
    auto retained=review(rejected,"diagnose");check(!field(retained,"failures").arr.empty(),"Guided repair must expose a rejected edit even while current model remains valid");const auto failedRow=field(retained,"failures").arr.front();check(!field(failedRow,"failure_id").asStr().empty(),"Rejected edit diagnosis retains failure custody");
    V correct=json(R"({"action":"prepare_repair","id":1,"command":{"op":"feature.edit","id":1,"p":{"width":18}}})");correct.set("failure_id",field(failedRow,"failure_id"));correct.set("source_sha256",field(retained,"source_sha256"));const auto reviewed=invoke(rejected,"design_review",correct);check(documentToJson(rejected.document)==validSource,"Corrected rejected edit is still only a preview");V accept=V::makeObj();accept.set("candidate_id",field(reviewed,"candidate_id"));invoke(rejected,"doc_apply",accept);check(rejected.document.find(1)->get("width")=="18"&&rejected.revision==validRevision+1,"Reviewed correction of ordinary rejected edit publishes once");
    correct.set("source_sha256",field(review(rejected,"diagnose"),"source_sha256"));correct.set("expected_revision",V::makeNum((double)rejected.revision));check(field(rejected.call("design_review",correct),"isError").b,"Retained failure from old revision cannot authorize a later correction");
    V tools=V::makeArr();appendDesignReviewTools(tools);check(tools.arr.size()==1&&!field(field(tools.arr[0],"annotations"),"readOnlyHint").b,"Prepare tool uses non-readonly steering despite no direct publication");
    std::cout<<"Design review: retained revision geometry, dimensions/BOM, atomic repair and rejection checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
