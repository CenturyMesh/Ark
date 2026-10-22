#pragma once
#include "ark/agent.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
namespace ark {
// Host captures and publishes under its session serialization. Evaluation owns
// only a private snapshot; no host document, UI or GL access is permitted.
class AsyncAuthoringWorker {
public:
    struct Progress {u64 job=0,feature=0;size_t index=0,total=0;std::string name;bool completed=false,reused=false;};
    struct Result {u64 job=0,revision=0,epoch=0;double milliseconds=0;arkjson::Value arguments,rejection;std::unique_ptr<AgentSession::PreparedTransfer> transfer;};
    explicit AsyncAuthoringWorker(std::function<void(const std::atomic_bool&)> beforeEvaluation={});
    ~AsyncAuthoringWorker();
    u64 start(const Document&,u64 revision,u64 epoch,const arkjson::Value& args);
    void cancel();
    void shutdown();
    Progress progress() const;
    std::optional<Result> take();
    static arkjson::Value publish(Result&&,AgentSession&,u64 currentEpoch,bool cancelled=false);
private:
    struct Request {u64 job=0,revision=0,epoch=0;arkjson::Value arguments;std::shared_ptr<const Document> snapshot;std::shared_ptr<std::atomic_bool> cancelled;};
    mutable std::mutex mutex_;std::condition_variable ready_;bool stopping_=false;u64 serial_=0;
    std::optional<Request> pending_,current_;std::optional<Result> result_;Progress progress_;
    std::function<void(const std::atomic_bool&)> beforeEvaluation_;std::thread thread_;
    void run();
};
}
