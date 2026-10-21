#pragma once
#include "ark/agent.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ark {
// 046B background exact reads (A.04). An explicit allow-list of read-only tools
// may run off the document owner thread against a private session copied at the
// current revision. Writes, previews, exports, UI state, files and anything that
// must persist into the live session stay on the owner thread. Empty: allowed.
std::string exactJobRejection046B(const std::string& tool,const arkjson::Value& args);
inline bool exactJobAllowed046B(const std::string& tool,const arkjson::Value& args){return exactJobRejection046B(tool,args).empty();}
// Copy on the owner thread. Immutable evaluated geometry is shared; the live
// activity ledger and staged saved geometry are detached. The copy has no undo,
// candidates, failures or BOM review state, so its caches are discarded with it.
std::unique_ptr<AgentSession> captureExactSession046B(const Document& live,u64 revision);
// Result envelope labelled with the revision the read observed.
arkjson::Value exactJobLabel046B(arkjson::Value result,u64 id,u64 observedRevision,double queueMs,double runMs);
arkjson::Value exactJobError046B(const std::string& tool,u64 observedRevision,const std::string& code,const std::string& message);

class ExactJobScheduler046B {
public:
    static constexpr unsigned nativeRequestLimitMs=120000; // per native request inside a job; 30 s elsewhere
    static constexpr unsigned jobLimitMs=600000;           // whole job, the live MCP execution bound
    struct Completion {u64 id=0,revision=0,epoch=0;std::string tool;arkjson::Value result;double queueMs=0,runMs=0;bool cancelled=false;};
    struct Request {
        std::string tool,label;arkjson::Value args;  // without actor: the owner records activity
        std::unique_ptr<AgentSession> session;        // from captureExactSession046B
        u64 revision=0,epoch=0;
        std::function<bool()> cancelled;              // external stop/cancel flags; polled, thread-safe
        std::function<void(Completion)> done;         // worker thread; the host posts to its owner
    };
    struct Status {u64 id=0,revision=0;std::string tool,label;double elapsedMs=0;bool running=false,cancelRequested=false;};
    explicit ExactJobScheduler046B(unsigned workers=2);
    ~ExactJobScheduler046B();
    ExactJobScheduler046B(const ExactJobScheduler046B&)=delete;
    ExactJobScheduler046B& operator=(const ExactJobScheduler046B&)=delete;
    u64 submit(Request request); // throws on allow-list rejection or after shutdown
    bool cancel(u64 id);
    void cancelAll();
    void shutdown(); // queued jobs complete as cancelled; running jobs stop and join
    std::vector<Status> status() const;
    size_t active() const;
    void setTestDelayMs(unsigned ms){testDelayMs_=ms;} // test hook: cancellable wait before the call
private:
    struct Job {Request request;u64 id=0;std::chrono::steady_clock::time_point submitted,started;std::atomic_bool cancelRequested{false},running{false};};
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::shared_ptr<Job>> queue_;
    std::map<u64,std::shared_ptr<Job>> jobs_;
    u64 next_=0;
    bool stopping_=false;
    std::atomic<unsigned> testDelayMs_{0};
    std::vector<std::thread> workers_;
    void run();
    void execute(Job& job);
};
}
