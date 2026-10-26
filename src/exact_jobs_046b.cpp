#include "ark/exact_jobs_046b.hpp"
#include "ark/native_backend.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
using Clock=std::chrono::steady_clock;
double elapsedMs(Clock::time_point from,Clock::time_point to){return std::chrono::duration<double,std::milli>(to-from).count();}
// Source-reviewed read-only handlers. Each reads the session document and may
// warm only caches of that private copy (centroids, reference frames, lazily
// promoted native bodies, mesh BVH); none writes files, UI state, candidates,
// BOM review gates or the context-graph index.
const std::set<std::string> exactReadTools{"transmission_inspect","transmission_design","manufacturing_simulate","surface_control","native_inspect","geometry_query","reference_inspect","assembly_info","collision_inspect","physics_inspect","mesh_inspect","motion_info","step_inspect"};
V withPayload(V result,const V& payload){
    result.set("structuredContent",payload);
    if(const auto* content=result.find("content");content&&content->t==V::Arr){V parts=*content;for(auto& part:parts.arr)if(part.find("type")&&part.find("type")->asStr()=="text")part.set("text",V::makeStr(payload.dump()));result.set("content",parts);}
    return result;
}
}
std::string exactJobRejection046B(const std::string& tool,const V& args){
    if(args.t!=V::Obj)return "arguments must be an object";
    if(tool=="query_batch"){
        const auto* rows=args.find("queries");if(!rows||rows->t!=V::Arr||rows->arr.empty())return "query_batch needs queries";
        for(const auto& row:rows->arr){const auto* name=row.find("tool");const auto* values=row.find("args");
            if(!name||name->t!=V::Str||!values||values->t!=V::Obj)return "query_batch row is malformed";
            if(!exactReadTools.count(name->str))return "query_batch row "+name->str+" is not on the 046B background exact-read allow-list";}
        return {};
    }
    if(!exactReadTools.count(tool))return tool+" is not on the 046B background exact-read allow-list";
    return {};
}
std::unique_ptr<AgentSession> captureExactSession046B(const Document& live,u64 revision){
    auto session=std::make_unique<AgentSession>();session->document=live;
    // The ledger is shared by ordinary copies; staged saved geometry is consumed
    // by recompute. Both belong to the live document only.
    session->document.agentActivity.reset();session->document.pendingNativeGeometry.reset();session->document.evaluatingBodies=nullptr;
    session->revision=revision;session->suppressFailureCapture();session->suppressIterationHistory();return session;
}
V exactJobLabel046B(V result,u64 id,u64 observedRevision,double queueMs,double runMs){
    V payload=result.find("structuredContent")?*result.find("structuredContent"):V::makeObj();if(payload.t!=V::Obj)return result;
    V job=V::makeObj();job.set("id",V::makeNum((double)id));job.set("execution",V::makeStr("background_snapshot"));job.set("observed_revision",V::makeNum((double)observedRevision));
    job.set("queue_ms",V::makeNum(queueMs));job.set("run_ms",V::makeNum(runMs));
    job.set("native_request_limit_ms",V::makeNum(ExactJobScheduler046B::nativeRequestLimitMs));job.set("job_limit_ms",V::makeNum(ExactJobScheduler046B::jobLimitMs));
    payload.set("observed_revision",V::makeNum((double)observedRevision));payload.set("exact_job",job);return withPayload(std::move(result),payload);
}
V exactJobError046B(const std::string& tool,u64 observedRevision,const std::string& code,const std::string& message){
    V error=V::makeObj();error.set("error",V::makeStr(message));error.set("error_code",V::makeStr(code));error.set("tool",V::makeStr(tool));
    error.set("revision",V::makeNum((double)observedRevision));error.set("document_unchanged",V::makeBool(true));
    V result=V::makeObj(),content=V::makeArr(),part=V::makeObj();part.set("type",V::makeStr("text"));part.set("text",V::makeStr(error.dump()));content.arr.push_back(part);
    result.set("content",content);result.set("structuredContent",error);result.set("isError",V::makeBool(true));return result;
}
ExactJobScheduler046B::ExactJobScheduler046B(unsigned workers){
    if(workers<1||workers>4)throw std::runtime_error("Exact job workers must be 1..4");
    for(unsigned i=0;i<workers;++i)workers_.emplace_back([this]{run();});
}
ExactJobScheduler046B::~ExactJobScheduler046B(){shutdown();}
u64 ExactJobScheduler046B::submit(Request request){
    if(const auto reason=exactJobRejection046B(request.tool,request.args);!reason.empty())throw std::runtime_error("EXACT_JOB_NOT_ALLOWED: "+reason);
    if(!request.session||!request.done)throw std::runtime_error("Exact job needs a captured session and a completion");
    auto job=std::make_shared<Job>();job->request=std::move(request);job->submitted=Clock::now();
    {std::lock_guard lock(mutex_);if(stopping_)throw std::runtime_error("Exact job scheduler is shut down");job->id=++next_;jobs_[job->id]=job;queue_.push_back(job);}
    ready_.notify_one();return job->id;
}
bool ExactJobScheduler046B::cancel(u64 id){std::lock_guard lock(mutex_);auto found=jobs_.find(id);if(found==jobs_.end())return false;found->second->cancelRequested=true;return true;}
void ExactJobScheduler046B::cancelAll(){std::lock_guard lock(mutex_);for(auto& job:jobs_)job.second->cancelRequested=true;}
void ExactJobScheduler046B::shutdown(){
    {std::lock_guard lock(mutex_);stopping_=true;for(auto& job:jobs_)job.second->cancelRequested=true;}
    ready_.notify_all();for(auto& worker:workers_)if(worker.joinable())worker.join();workers_.clear();
}
std::vector<ExactJobScheduler046B::Status> ExactJobScheduler046B::status() const{
    std::lock_guard lock(mutex_);std::vector<Status> rows;const auto now=Clock::now();
    for(const auto& [id,job]:jobs_){Status row;row.id=id;row.revision=job->request.revision;row.tool=job->request.tool;row.label=job->request.label;row.running=job->running;row.cancelRequested=job->cancelRequested;row.elapsedMs=elapsedMs(job->running?job->started:job->submitted,now);rows.push_back(std::move(row));}
    return rows;
}
size_t ExactJobScheduler046B::active() const{std::lock_guard lock(mutex_);return jobs_.size();}
void ExactJobScheduler046B::run(){
    for(;;){
        std::shared_ptr<Job> job;
        {std::unique_lock lock(mutex_);ready_.wait(lock,[&]{return stopping_||!queue_.empty();});if(queue_.empty())return;job=queue_.front();queue_.pop_front();job->started=Clock::now();job->running=true;}
        execute(*job);
    }
}
void ExactJobScheduler046B::execute(Job& job){
    Completion done;done.id=job.id;done.revision=job.request.revision;done.epoch=job.request.epoch;done.tool=job.request.tool;done.queueMs=elapsedMs(job.submitted,job.started);
    const auto deadline=job.started+std::chrono::milliseconds(jobLimitMs);std::atomic_bool overtime{false};
    const auto& external=job.request.cancelled;
    auto cancelled=[&]{return job.cancelRequested.load()||(external&&external());};
    auto stop=[&]{if(cancelled())return true;if(Clock::now()>=deadline)overtime=true;return overtime.load();};
    try{
        NativeRequestControlScope control(stop,nativeRequestLimitMs);
        for(unsigned waited=0;waited<testDelayMs_.load()&&!stop();waited+=5)std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if(!stop()){
            // Actor attribution is recorded by the owner on the live ledger.
            V args=job.request.args;args.obj.erase(std::remove_if(args.obj.begin(),args.obj.end(),[](const auto& field){return field.first=="actor";}),args.obj.end());
            done.result=job.request.session->call(job.request.tool,args);
        }
    }catch(const std::exception& error){done.result=exactJobError046B(job.request.tool,job.request.revision,"EVALUATION_FAILED",error.what());}
    done.runMs=elapsedMs(job.started,Clock::now());
    // A read stopped before delivery never returns partial evidence.
    if(cancelled()||overtime){done.cancelled=true;done.result=exactJobError046B(job.request.tool,job.request.revision,overtime?"RESOURCE_LIMIT":"CANCELLED",overtime?"Exact background job exceeded its 600-second job limit; repeating the same request will exceed it again":"Exact read cancelled before completion; no partial result is returned");}
    done.result=exactJobLabel046B(std::move(done.result),job.id,job.request.revision,done.queueMs,done.runMs);
    job.request.session.reset();auto callback=std::move(job.request.done);
    {std::lock_guard lock(mutex_);jobs_.erase(job.id);}
    try{callback(std::move(done));}catch(...){}
}
}
