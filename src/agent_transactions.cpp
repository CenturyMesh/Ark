#include "ark/design_containers.hpp"
#include "ark/agent.hpp"
#include "ark/assembly_authoring.hpp"
#include "ark/agent_activity.hpp"
#include "ark/iteration_history.hpp"
#include <atomic>
#include <random>
#include <sstream>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
std::string fingerprint(const Document& doc){
    std::string value=documentToJson(doc,false,false);
    for(double x:{doc.tessOpt.tol,doc.tessOpt.maxEdge,doc.tessOpt.minAngleDeg,(double)doc.tessOpt.maxLevels})value+='|'+V::makeNum(x).dump();
    return value;
}
void checkRevision(const V& args,u64 revision){const auto* expected=args.find("expected_revision");if(!expected||expected->t!=V::Num||expected->num!=(double)revision)throw std::runtime_error("Revision conflict; read doc_info with detail design and retry");}
size_t candidateBytes(const Document& doc){size_t bytes=documentToJson(doc).size();for(const auto& item:doc.referenceFrameCache){bytes+=item.second.brep.size()+item.second.topology.size();for(const auto& frame:item.second.frames)bytes+=frame.first.size()+frame.second.dump().size()*4;}for(const auto& b:doc.evalBodies){bytes+=2*b.nativeData.dump().size();if(b.baked)bytes+=2*(b.baked->pos.size()+b.baked->nrm.size()+b.baked->idx.size())*sizeof(float);bytes+=b.faces.size()*512;}return bytes;}
std::string candidateId(){static std::atomic<unsigned long long> next{0};static const auto salt=[](){std::random_device random;return ((unsigned long long)random()<<32)^random();}();std::ostringstream out;out<<std::hex<<salt<<'-'<<++next;return out.str();}
}
arkjson::Value AgentSession::call(const std::string& tool,const V& args){
    const auto activityStarted=std::chrono::steady_clock::now();const auto activityBefore=revision;const auto* actor=args.find("actor");
    const bool audit=callDepth_==0&&actor&&actor->t==V::Obj&&actor->find("kind")&&actor->find("kind")->asStr()=="agent"&&tool!="tool_feedback"&&tool!="activity_info"&&tool!="usage_record";
    const bool recording=!suppressFailures_&&callDepth_==0&&(tool=="doc_apply"||tool=="doc_preview");
    const auto rejectedRevision=revision;const std::string rejectedBaseline=recording?documentToJson(document):std::string();
    auto finish=[&](V response){if(revision!=activityBefore&&response.find("isError")&&!response.find("isError")->b){if(tool=="doc_load"||tool=="doc_new")resetBomReview();else if(audit)markBomAuthored();}if(recording)response=captureFailure(tool,args,args,rejectedBaseline,rejectedRevision,std::move(response));if(audit)response=recordAgentActivity(document,tool,args,*actor,std::move(response),activityBefore,revision,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-activityStarted).count());return response;};
    V clean=args;if(clean.t==V::Obj)clean.obj.erase(std::remove_if(clean.obj.begin(),clean.obj.end(),[&](const auto& field){return (field.first=="actor"&&tool!="tool_feedback")||field.first=="motivation";}),clean.obj.end());
    const bool mutating=tool=="doc_apply"||tool=="doc_new"||tool=="doc_load"||tool=="doc_undo"||tool=="doc_redo"||tool=="iteration_restore"||tool=="import_step"||tool=="import_sheet_step"||tool=="import_mesh"||tool=="import_cad"||(tool=="design_containers"&&args.find("action")&&args.find("action")->asStr()=="merge"&&!(args.find("preview")&&args.find("preview")->t==V::Bool&&args.find("preview")->b))||(tool=="image_asset"&&args.find("action")&&args.find("action")->asStr()=="import")||tool=="usage_record"||tool=="library_insert";
    struct Depth {size_t& value;Depth(size_t& n):value(n){++value;}~Depth(){--value;}} depth(callDepth_);
    const bool planeMutation=tool=="plane_authoring"&&!(args.find("preview")&&args.find("preview")->t==V::Bool&&args.find("preview")->b);
    if((!mutating&&!planeMutation)||callDepth_>1||suppressHistory_)return finish(dispatch(tool,clean));
    const auto* preparedId=args.find("candidate_id");
    const bool metadataPrepared=preparedId&&preparedId->t==V::Str&&std::any_of(prepared_.begin(),prepared_.end(),[&](const Prepared& p){return p.id==preparedId->str&&p.metadataOnly;});
    const bool lightweight=tool=="doc_apply"&&!args.find("checks")&&(metadataOnlyApply(document,clean)||metadataPrepared);
    Document before=lightweight?document.authoringCopy():document;const auto oldRevision=revision;const auto oldUndo=undo_,oldRedo=redo_;const auto oldPrepared=prepared_;
    auto rollback=[&](){if(lightweight){document.features=std::move(before.features);document.materials=std::move(before.materials);document.imageAssets=std::move(before.imageAssets);document.stamps=std::move(before.stamps);document.bomDocumentId=std::move(before.bomDocumentId);document.iterationHistory=std::move(before.iterationHistory);document.usage=std::move(before.usage);}else document=std::move(before);revision=oldRevision;undo_=oldUndo;redo_=oldRedo;prepared_=oldPrepared;cache_.clear();cacheRevision_=~u64(0);motionRevision_=~u64(0);};
    auto envelope=[](V value,bool error){V response=V::makeObj(),content=V::makeArr(),item=V::makeObj();item.set("type",V::makeStr("text"));item.set("text",V::makeStr(value.dump()));content.arr.push_back(item);response.set("content",content);response.set("structuredContent",value);response.set("isError",V::makeBool(error));return response;};
    try{
        // Validate caller metadata before running geometry or touching usage.
        (void)iterationActor(args);if(auto reason=args.find("motivation");reason&&(reason->t!=V::Str||reason->str.size()>4096))throw std::runtime_error("motivation must be text of at most 4096 bytes");
        std::string target;if(tool=="iteration_restore"){if(auto id=args.find("id");id&&id->t==V::Str)target=id->str;}
        V response=dispatch(tool,clean);if(auto error=response.find("isError");error&&error->b){rollback();return finish(std::move(response));}
        if((tool=="doc_undo"||tool=="doc_redo")&&before.iterationHistory.t==V::Obj)target=iterationUndoTarget(before,tool=="doc_redo");
        // Opening/New are project boundaries. They never copy another project's
        // geometry into the loaded/new document's persistent journal.
        const bool boundary=tool=="doc_new"||tool=="doc_load"||((tool=="doc_undo"||tool=="doc_redo")&&before.iterationHistory.t==V::Null&&document.iterationHistory.t==V::Obj);
        const bool changed=iterationAuthoring(before).dump()!=iterationAuthoring(document).dump();
        if(!boundary&&(changed||!target.empty()))iterationAppend(document,before,tool,args,target);
        if(!boundary&&(changed||!target.empty())&&document.iterationHistory.t==V::Obj){std::string error;if(!iterationValidate(document,document.iterationHistory,error))throw std::runtime_error(error);}
        const std::string persisted=documentToJson(document);if(persisted.size()>maxDocumentBytes)throw std::runtime_error("Iteration history capacity exceeds the total 4 MiB document limit; edit rejected atomically");
        V persistedRoot;if(!arkjson::parse(persisted,persistedRoot))throw std::runtime_error("Iteration history nesting capacity exceeds the JSON depth limit of 64; edit rejected atomically");
        if(document.iterationHistory.t==V::Obj&&!boundary&&(changed||!target.empty())){V receipt=*response.find("structuredContent");receipt.set("iteration_id",*document.iterationHistory.find("head_id"));receipt.set("head_id",*document.iterationHistory.find("head_id"));response=envelope(receipt,false);}
        return finish(std::move(response));
    }catch(const std::exception& exception){rollback();V error=V::makeObj();error.set("error",V::makeStr(exception.what()));error.set("error_code",V::makeStr(std::string(exception.what()).find("capacity")!=std::string::npos?"HISTORY_CAPACITY_EXCEEDED":"HISTORY_FAILED"));error.set("tool",V::makeStr(tool));error.set("revision",V::makeNum((double)revision));error.set("document_unchanged",V::makeBool(true));return finish(envelope(error,true));}
}
arkjson::Value AgentSession::preparePreview(const V& args){
    checkRevision(args,revision);if(args.find("candidate_id"))throw std::runtime_error("Preview requires commands, not another candidate");
    bool changesOnly=false;if(const auto* detail=args.find("detail")){if(detail->t!=V::Str||(detail->str!="design"&&detail->str!="changes"))throw std::runtime_error("Preview detail must be design or changes");changesOnly=detail->str=="changes";}
    const auto now=std::chrono::steady_clock::now();
    prepared_.erase(std::remove_if(prepared_.begin(),prepared_.end(),[&](const Prepared& p){return p.expires<=now||p.revision!=revision;}),prepared_.end());
    const bool metadataOnly=metadataOnlyApply(document,args)&&!args.find("checks");
    AgentSession candidate;candidate.document=document.authoringCopy();
    if(!metadataOnly){candidate.document.evaluationCache=document.evaluationCache;candidate.document.evaluationGeneration=document.evaluationGeneration;candidate.document.referenceFrameCache=document.referenceFrameCache;candidate.document.centroidCache=document.centroidCache;}
    struct BorrowGeometry {
        Document& live;Document& preview;bool active=false;
        BorrowGeometry(Document& a,Document& b,bool requested):live(a),preview(b){if(requested)borrow();}
        void borrow(){if(!active){live.evalBodies.swap(preview.evalBodies);live.lastResults.swap(preview.lastResults);preview.recomputeMs=live.recomputeMs;active=true;}}
        void restore(){if(active){live.evalBodies.swap(preview.evalBodies);live.lastResults.swap(preview.lastResults);active=false;}}
        ~BorrowGeometry(){restore();}
    } borrowed(document,candidate.document,metadataOnly);
    candidate.document.iterationHistory=V{};candidate.suppressIterationHistory();candidate.suppressFailureCapture();candidate.document.evaluatingBodies=nullptr;candidate.revision=revision;
    const auto response=candidate.call("doc_apply",args);V out=V::makeObj();bool valid=!response.find("isError")->b;
    out.set("valid",V::makeBool(valid));out.set("revision",V::makeNum((double)revision));out.set("candidate_result",*response.find("structuredContent"));out.set("apply",args);
    if(!valid)return out;
    V view=V::makeObj();view.set("detail",V::makeStr("design"));V before,after;
    if(metadataOnly){borrowed.restore();before=*call("doc_info",view).find("structuredContent");borrowed.borrow();after=*candidate.call("doc_info",view).find("structuredContent");}
    else{before=*call("doc_info",view).find("structuredContent");after=*candidate.call("doc_info",view).find("structuredContent");}
    V changed=V::makeArr(),beforeRows=V::makeArr(),afterRows=V::makeArr(),fitEvidence=V::makeArr();std::set<u64> changedIds;
    const auto* original=before.find("features");const auto* revised=after.find("features");
    auto findRow=[](const V* rows,u64 id)->const V*{if(rows&&rows->t==V::Arr)for(const auto& row:rows->arr)if(row.find("id")&&row.find("id")->num==(double)id)return &row;return nullptr;};
    if(original)for(const auto& row:original->arr){const auto id=(u64)row.find("id")->num;const auto* next=findRow(revised,id);if(!next||next->dump()!=row.dump())changedIds.insert(id);}
    if(revised)for(const auto& row:revised->arr){const auto id=(u64)row.find("id")->num;const auto* prior=findRow(original,id);if(!prior||prior->dump()!=row.dump())changedIds.insert(id);}
    for(u64 id:changedIds){changed.arr.push_back(V::makeNum((double)id));if(const auto* row=findRow(original,id))beforeRows.arr.push_back(*row);if(const auto* row=findRow(revised,id))afterRows.arr.push_back(*row);
        for(const auto& body:candidate.document.evalBodies)if(body.id==id)if(const auto* report=body.nativeData.find("report"))if(const auto* fit=report->find("end_fit")){V evidence=V::makeObj();evidence.set("id",V::makeNum((double)id));evidence.set("end_fit",*fit);fitEvidence.arr.push_back(evidence);}}
    V beforeSummary=V::makeObj(),afterSummary=V::makeObj();beforeSummary.set("revision",V::makeNum((double)revision));beforeSummary.set("features",beforeRows);beforeSummary.set("partial",V::makeBool(true));afterSummary.set("revision",V::makeNum((double)revision));afterSummary.set("features",afterRows);afterSummary.set("partial",V::makeBool(true));afterSummary.set("committed",V::makeBool(false));out.set("before",changesOnly?beforeSummary:before);out.set("after",changesOnly?afterSummary:after);out.set("changed_feature_ids",changed);out.set("end_fit_evidence",fitEvidence);out.set("detail",V::makeStr(changesOnly?"changes":"design"));if(changesOnly)out.set("summary_scope",V::makeStr("Changed feature definitions/evaluated evidence only; parameter/sketch/assembly changes remain in recipe and candidate_result."));
    borrowed.restore();
    const size_t bytes=candidateBytes(candidate.document),budget=128u*1024u*1024u;
    if(bytes>budget){out.set("candidate_cached",V::makeBool(false));out.set("cache_reason",V::makeStr("Candidate exceeds 128 MiB cache budget; apply commands recomputes"));return out;}
    size_t used=0;for(const auto& p:prepared_)used+=p.bytes;
    while(!prepared_.empty()&&(prepared_.size()>=4||used+bytes>budget)){used-=prepared_.front().bytes;prepared_.erase(prepared_.begin());}
    Prepared prepared;prepared.metadataOnly=metadataOnly;prepared.id=candidateId();prepared.baseline=fingerprint(document);prepared.revision=revision;prepared.bytes=bytes;prepared.document=std::make_shared<Document>(std::move(candidate.document));prepared.result=*response.find("structuredContent");prepared.expires=std::chrono::steady_clock::now()+std::chrono::minutes(5);
    V apply=V::makeObj();apply.set("expected_revision",V::makeNum((double)revision));apply.set("candidate_id",V::makeStr(prepared.id));out.set("apply",apply);out.set("candidate_id",V::makeStr(prepared.id));out.set("candidate_cached",V::makeBool(true));out.set("expires_in_seconds",V::makeNum(300));
    // The expanded recipe remains inspectable and can be submitted explicitly.
    out.set("recipe",args);prepared_.push_back(std::move(prepared));return out;
}
arkjson::Value AgentSession::snapshotCandidate(const V& args){
    checkRevision(args,revision);const auto* key=args.find("candidate_id");if(!key||key->t!=V::Str)throw std::runtime_error("candidate_id must be text");
    auto found=std::find_if(prepared_.begin(),prepared_.end(),[&](const Prepared& p){return p.id==key->str;});
    if(found==prepared_.end())throw std::runtime_error("Prepared candidate missing, evicted or already applied");
    if(found->expires<=std::chrono::steady_clock::now()||found->revision!=revision||found->baseline!=fingerprint(document))throw std::runtime_error("Prepared candidate expired or document changed; preview again");
    AgentSession candidate;candidate.document=*found->document;if(found->metadataOnly){candidate.document.evalBodies=document.evalBodies;candidate.document.lastResults=document.lastResults;}candidate.document.evaluatingBodies=nullptr;candidate.revision=revision;candidate.suppressIterationHistory();
    V capture=args;capture.obj.erase(std::remove_if(capture.obj.begin(),capture.obj.end(),[](const auto& item){return item.first=="candidate_id";}),capture.obj.end());
    V response=candidate.call("snapshot",capture);if(response.find("isError")->b)return response;
    V data=*response.find("structuredContent"),visual=*data.find("visual_evidence");visual.set("candidate_id",*key);visual.set("stage",V::makeStr("preview"));visual.set("committed",V::makeBool(false));data.set("visual_evidence",visual);data.set("document_unchanged",V::makeBool(true));response.set("structuredContent",data);
    V content=*response.find("content");for(auto& part:content.arr)if(part.find("type")&&part.find("type")->asStr()=="text"){part.set("text",V::makeStr(data.dump()));break;}response.set("content",content);
    return response;
}
const Document& AgentSession::PreparedTransfer::document() const{
    if(!value_||!value_->document)throw std::runtime_error("Prepared transfer already consumed");
    return *value_->document;
}
const std::string& AgentSession::PreparedTransfer::id() const{
    if(!value_)throw std::runtime_error("Prepared transfer already consumed");return value_->id;
}
const V& AgentSession::PreparedTransfer::result() const{
    if(!value_)throw std::runtime_error("Prepared transfer already consumed");return value_->result;
}
std::unique_ptr<AgentSession::PreparedTransfer> AgentSession::takePreparedCandidate(const std::string& id){
    auto found=std::find_if(prepared_.begin(),prepared_.end(),[&](const Prepared& p){return p.id==id;});
    if(found==prepared_.end())throw std::runtime_error("Prepared candidate missing, evicted or already applied");
    if(found->metadataOnly)throw std::runtime_error("Geometry worker transfer requires a complete geometry candidate");
    if(found->expires<=std::chrono::steady_clock::now()||found->revision!=revision||found->baseline!=fingerprint(document))throw std::runtime_error("Prepared candidate expired or document changed; preview again");
    auto receipt=std::make_shared<Prepared>(std::move(*found));prepared_.erase(found);
    return std::unique_ptr<PreparedTransfer>(new PreparedTransfer(std::move(receipt)));
}
void AgentSession::adoptPreparedCandidate(PreparedTransfer&& transfer,u64 expectedRevision){
    if(expectedRevision!=revision)throw std::runtime_error("Revision conflict; drawing retained");
    if(!transfer.value_)throw std::runtime_error("Prepared transfer already consumed");
    const auto& receipt=*transfer.value_;
    if(receipt.expires<=std::chrono::steady_clock::now()||receipt.revision!=revision||receipt.baseline!=fingerprint(document))throw std::runtime_error("Prepared transfer expired or source changed; preview again");
    constexpr size_t budget=128u*1024u*1024u;
    if(!receipt.document||receipt.bytes>budget||receipt.metadataOnly)throw std::runtime_error("Prepared transfer has no complete geometry within the candidate budget");
    if(std::any_of(prepared_.begin(),prepared_.end(),[&](const Prepared& p){return p.id==receipt.id;}))throw std::runtime_error("Prepared transfer already adopted");
    size_t used=0;for(const auto& p:prepared_)used+=p.bytes;
    while(!prepared_.empty()&&(prepared_.size()>=4||used+receipt.bytes>budget)){used-=prepared_.front().bytes;prepared_.erase(prepared_.begin());}
    prepared_.push_back(std::move(*transfer.value_));transfer.value_.reset();
}
arkjson::Value AgentSession::commitPrepared(const V& args){
    checkRevision(args,revision);for(const auto& field:args.obj)if(field.first!="expected_revision"&&field.first!="candidate_id"&&field.first!="accept_dfm")throw std::runtime_error("Prepared apply accepts only candidate_id, expected_revision and explicit accept_dfm for DFM proposals");
    const auto* id=args.find("candidate_id");if(!id||id->t!=V::Str)throw std::runtime_error("candidate_id must be text");
    auto found=std::find_if(prepared_.begin(),prepared_.end(),[&](const Prepared& p){return p.id==id->str;});
    if(found==prepared_.end())throw std::runtime_error("Prepared candidate missing, evicted or already applied");
    if(found->expires<=std::chrono::steady_clock::now()||found->revision!=revision||found->baseline!=fingerprint(document)){prepared_.erase(found);throw std::runtime_error("Prepared candidate expired or document changed; preview again");}
    const auto* acceptance=args.find("accept_dfm");
    if(found->requiresDfmAcceptance&&(!acceptance||acceptance->t!=V::Bool||!acceptance->b))throw std::runtime_error("DFM_ACCEPTANCE_REQUIRED: review the retained comparison and explicitly pass accept_dfm:true");
    if(acceptance&&(acceptance->t!=V::Bool||!found->requiresDfmAcceptance))throw std::runtime_error("accept_dfm applies only to a retained manufacturing proposal");
    verifyAssemblyExternalReferences(*found->document);
    auto next=found->document;V out=found->result;if(found->requiresDfmAcceptance){out.set("dfm_comparison",found->dfmComparison);out.set("dfm_accepted",V::makeBool(true));}
    validateDesignContainerTransition(document,*next);
    const bool metadata=found->metadataOnly;out.set("geometry_changed",V::makeBool(!out.find("geometry_recomputed")||out.find("geometry_recomputed")->b));remember();
    if(metadata){document.features=next->features;document.materials=next->materials;document.bomDocumentId=next->bomDocumentId;document.imageAssets=next->imageAssets;document.stamps=next->stamps;}
    else{auto activity=document.agentActivity;auto usage=document.usage;document=*next;document.agentActivity=std::move(activity);document.usage=std::move(usage);}
    ++revision;prepared_.clear();
    V evaluation=V::makeObj();for(const char* key:{"recompute_ms","features_evaluated","features_reused","parallel_features","peak_parallel_features"})if(const auto* value=out.find(key))evaluation.set(key,*value);out.set("prepared_evaluation",evaluation);
    out.set("features_evaluated",V::makeNum(0));out.set("features_reused",V::makeNum(document.evalBodies.size()));out.set("parallel_features",V::makeNum(0));out.set("peak_parallel_features",V::makeNum(0));
    out.set("revision",V::makeNum((double)revision));out.set("geometry_recomputed",V::makeBool(false));out.set("applied_prepared",V::makeBool(true));out.set("recompute_ms",V::makeNum(0));return out;
}
}
