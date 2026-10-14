#include "ark/direct_preview.hpp"
#include <stdexcept>

namespace ark {
DirectPreviewWorker::DirectPreviewWorker(std::function<void()> hook):beforeEvaluation_(std::move(hook)),thread_([this]{run();}){}
DirectPreviewWorker::~DirectPreviewWorker(){shutdown();}
void DirectPreviewWorker::shutdown(){
    {std::lock_guard lock(mutex_);stopping_=true;pending_.reset();}
    ready_.notify_one();if(thread_.joinable())thread_.join();
}
u64 DirectPreviewWorker::begin(std::shared_ptr<const Document> baseline,u64 revision,u64 epoch){
    if(!baseline)throw std::runtime_error("Direct preview needs a captured source document");
    if(baseline->agentActivity)throw std::runtime_error("Direct preview must use an isolated snapshot without a shared live activity ledger");
    std::lock_guard lock(mutex_);++gesture_;generation_=0;revision_=revision;epoch_=epoch;
    baseline_=std::move(baseline);pending_.reset();completed_.reset();return gesture_;
}
std::shared_ptr<const Document> DirectPreviewWorker::capture(const Document& live){
    auto snapshot=std::make_shared<Document>(live);snapshot->agentActivity.reset();snapshot->evaluatingBodies=nullptr;return snapshot;
}
u64 DirectPreviewWorker::request(arkjson::Value commands,u64 feature,const Mat4& world){
    std::lock_guard lock(mutex_);if(!baseline_||stopping_)throw std::runtime_error("Direct preview gesture has been cancelled");
    if(pending_)++statistics_.coalesced;
    pending_=Request{gesture_,++generation_,revision_,epoch_,feature,baseline_,std::move(commands),world};
    ++statistics_.submitted;completed_.reset();ready_.notify_one();return generation_;
}
void DirectPreviewWorker::cancel(){std::lock_guard lock(mutex_);++gesture_;generation_=0;baseline_.reset();pending_.reset();completed_.reset();}
std::optional<DirectPreviewWorker::Result> DirectPreviewWorker::take(){std::lock_guard lock(mutex_);auto result=std::move(completed_);completed_.reset();return result;}
DirectPreviewWorker::Statistics DirectPreviewWorker::statistics() const{std::lock_guard lock(mutex_);return statistics_;}
void DirectPreviewWorker::run(){
    using V=arkjson::Value;AgentSession probe;u64 loadedGesture=0;
    for(;;){
        Request request;
        {std::unique_lock lock(mutex_);ready_.wait(lock,[&]{return stopping_||pending_.has_value();});if(stopping_)return;request=std::move(*pending_);pending_.reset();++statistics_.evaluated;}
        Result result;result.gesture=request.gesture;result.generation=request.generation;result.revision=request.revision;result.epoch=request.epoch;
        const auto start=std::chrono::steady_clock::now();
        try{
            if(beforeEvaluation_)beforeEvaluation_();
            if(loadedGesture!=request.gesture){probe=AgentSession{};probe.document=*request.baseline;probe.document.evaluatingBodies=nullptr;probe.revision=request.revision;probe.suppressFailureCapture();probe.suppressIterationHistory();loadedGesture=request.gesture;}
            V args=V::makeObj();args.set("commands",request.commands);args.set("expected_revision",V::makeNum((double)request.revision));args.set("detail",V::makeStr("changes"));
            const V preview=probe.preparePreview(args);
            if(!preview.find("valid")||!preview.find("valid")->b){const auto* data=preview.find("candidate_result"),*error=data?data->find("error"):nullptr;throw std::runtime_error(error?error->asStr():"Direct geometry candidate rejected");}
            if(!preview.find("candidate_cached")||!preview.find("candidate_cached")->b)throw std::runtime_error("Direct preview exceeds the retained candidate capacity; reduce the edited part");
            result.receipt=probe.takePreparedCandidate(preview.find("candidate_id")->str);
            const auto& doc=result.receipt->document();result.feature=request.feature?request.feature:doc.features.back().id;
            for(const auto& body:doc.evalBodies)if(body.id==result.feature){result.mesh=body.tessellate(doc.tessOpt);if(const auto* report=body.nativeData.find("report"))result.report=*report;break;}
            if(!result.mesh.triCount())throw std::runtime_error("Direct recipe produced no display mesh");
            // Prepare display geometry once. UI only uploads a completed generation.
            result.fill.reserve(result.mesh.idx.size()*3);result.lines.reserve(result.mesh.idx.size()*6);
            auto point=[&](u32 vertex){size_t at=(size_t)vertex*3;if(at+2>=result.mesh.pos.size())throw std::runtime_error("Direct preview has an invalid mesh index");return request.world.transformPoint({result.mesh.pos[at],result.mesh.pos[at+1],result.mesh.pos[at+2]});};
            auto put=[](std::vector<float>& data,const Vec3& p){data.insert(data.end(),{(float)p.x,(float)p.y,(float)p.z});};
            for(size_t at=0;at+2<result.mesh.idx.size();at+=3){Vec3 p[3];for(int k=0;k<3;++k){p[k]=point(result.mesh.idx[at+k]);put(result.fill,p[k]);}for(int k=0;k<3;++k){put(result.lines,p[k]);put(result.lines,p[(k+1)%3]);}}
        }catch(const std::exception& error){result.error=error.what();result.receipt.reset();result.mesh={};result.fill.clear();result.lines.clear();}
        result.evaluationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        {std::lock_guard lock(mutex_);if(!stopping_&&request.gesture==gesture_&&request.generation==generation_)completed_=std::move(result);else ++statistics_.discarded;}
    }
}
}
