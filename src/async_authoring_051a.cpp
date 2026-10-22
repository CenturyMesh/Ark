#include "ark/async_authoring.hpp"
#include "ark/native_backend.hpp"
#include <algorithm>
#include <stdexcept>
namespace ark {
namespace {
using V=arkjson::Value;
V reject(u64 revision,const std::string& code,const std::string& message){V data=V::makeObj(),result=V::makeObj(),parts=V::makeArr(),part=V::makeObj();data.set("error",V::makeStr(message));data.set("error_code",V::makeStr(code));data.set("revision",V::makeNum((double)revision));data.set("document_unchanged",V::makeBool(true));part.set("type",V::makeStr("text"));part.set("text",V::makeStr(data.dump()));parts.arr.push_back(part);result.set("content",parts);result.set("structuredContent",data);result.set("isError",V::makeBool(true));return result;}
}
AsyncAuthoringWorker::AsyncAuthoringWorker(std::function<void(const std::atomic_bool&)> hook):beforeEvaluation_(std::move(hook)),thread_([this]{run();}){}
AsyncAuthoringWorker::~AsyncAuthoringWorker(){shutdown();}
u64 AsyncAuthoringWorker::start(const Document& document,u64 revision,u64 epoch,const V& args){
    auto snapshot=std::make_shared<Document>(document);snapshot->agentActivity.reset();snapshot->pendingNativeGeometry.reset();snapshot->evaluatingBodies=nullptr;
    std::lock_guard lock(mutex_);if(stopping_)throw std::runtime_error("Authoring worker is shut down");if(current_&&!current_->cancelled->load())throw std::runtime_error("Authoring preparation already pending");
    Request request{++serial_,revision,epoch,args,std::move(snapshot),std::make_shared<std::atomic_bool>(false)};current_=request;pending_=std::move(request);result_.reset();progress_={serial_};ready_.notify_one();return serial_;
}
void AsyncAuthoringWorker::cancel(){std::lock_guard lock(mutex_);if(!current_)return;*current_->cancelled=true;pending_.reset();Result cancelled;cancelled.job=current_->job;cancelled.revision=current_->revision;cancelled.epoch=current_->epoch;cancelled.arguments=current_->arguments;cancelled.rejection=reject(current_->revision,"CANCELLED","Authoring preparation cancelled; no edit was published");result_=std::move(cancelled);}
void AsyncAuthoringWorker::shutdown(){
    {std::lock_guard lock(mutex_);stopping_=true;if(current_)*current_->cancelled=true;if(pending_)*pending_->cancelled=true;pending_.reset();result_.reset();}
    ready_.notify_one();if(thread_.joinable())thread_.join();
}
AsyncAuthoringWorker::Progress AsyncAuthoringWorker::progress() const{std::lock_guard lock(mutex_);return progress_;}
std::optional<AsyncAuthoringWorker::Result> AsyncAuthoringWorker::take(){std::lock_guard lock(mutex_);if(!result_)return {};auto value=std::move(result_);result_.reset();current_.reset();return value;}
V AsyncAuthoringWorker::publish(Result&& result,AgentSession& session,u64 epoch,bool cancelled){
    if(cancelled)result.rejection=reject(session.revision,"CANCELLED","Authoring preparation cancelled before publication; no edit was published");
    if(result.epoch!=epoch||result.revision!=session.revision)result.rejection=reject(session.revision,"REVISION_CONFLICT","Drawing revision or document epoch changed during preparation; inspect and resubmit. No edit was published");
    return session.completePreparedApply(result.arguments,std::move(result.transfer),result.rejection);
}
void AsyncAuthoringWorker::run(){
    for(;;){Request request;{std::unique_lock lock(mutex_);ready_.wait(lock,[&]{return stopping_||pending_.has_value();});if(stopping_)return;request=std::move(*pending_);pending_.reset();}
        Result result;result.job=request.job;result.revision=request.revision;result.epoch=request.epoch;result.arguments=request.arguments;const auto started=std::chrono::steady_clock::now();
        try{
            if(request.arguments.t!=V::Obj)throw std::runtime_error("Authoring arguments must be an object");
            NativeRequestControlScope control([cancel=request.cancelled]{return cancel->load();},nativeRequestLimitMs());
            struct Observer {EvaluationObserver old;~Observer(){setEvaluationObserver(std::move(old));}} observer{setEvaluationObserver([this,job=request.job](u64 id,const std::string& name,size_t index,size_t total,bool complete,bool reused){std::lock_guard lock(mutex_);if(current_&&current_->job==job)progress_={job,id,index,total,name,complete,reused};})};
            if(beforeEvaluation_)beforeEvaluation_(*request.cancelled);
            if(!request.cancelled->load()){
                AgentSession probe;probe.document=*request.snapshot;probe.revision=request.revision;probe.suppressFailureCapture();probe.suppressIterationHistory();
                V args=request.arguments;args.obj.erase(std::remove_if(args.obj.begin(),args.obj.end(),[](const auto& field){return field.first=="actor"||field.first=="motivation";}),args.obj.end());
                if(const auto* detail=args.find("detail");detail&&(detail->t!=V::Str||(detail->str!="full"&&detail->str!="changes"&&detail->str!="design")))throw std::runtime_error("Apply detail must be full, design or changes");
                args.set("detail",V::makeStr("design"));const auto preview=probe.preparePreview(args);
                if(!preview.find("valid")||!preview.find("valid")->b){V data=preview.find("candidate_result")?*preview.find("candidate_result"):V::makeObj();result.rejection=reject(request.revision,"PREPARATION_FAILED",data.find("error")?data.find("error")->asStr():"Invalid authoring candidate");result.rejection.set("structuredContent",data);V content=*result.rejection.find("content");content.arr.front().set("text",V::makeStr(data.dump()));result.rejection.set("content",content);}
                else if(!preview.find("candidate_cached")||!preview.find("candidate_cached")->b)result.rejection=reject(request.revision,"CANDIDATE_CAPACITY_EXCEEDED","Background authoring candidate exceeds the 128 MiB retained allocation capacity; no edit was published");
                else result.transfer=probe.takePreparedCandidate(preview.find("candidate_id")->asStr());
            }
        }catch(const std::exception& error){result.rejection=reject(request.revision,"PREPARATION_FAILED",error.what());}
        result.milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        std::lock_guard lock(mutex_);if(!stopping_&&current_&&current_->job==request.job&&!request.cancelled->load())result_=std::move(result);
    }
}
}
