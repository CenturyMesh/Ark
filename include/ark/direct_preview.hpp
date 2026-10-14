#pragma once
#include "ark/agent.hpp"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace ark {
// One persistent evaluation thread, one latest pending recipe. No live document,
// window or GL objects are accessed by this worker.
class DirectPreviewWorker {
public:
    struct Result {
        u64 gesture=0,generation=0,revision=0,epoch=0,feature=0;
        double evaluationMs=0;
        std::string error;
        arkjson::Value report;
        Mesh mesh;
        std::vector<float> fill,lines;
        std::unique_ptr<AgentSession::PreparedTransfer> receipt;
    };
    struct Statistics {u64 submitted=0,evaluated=0,coalesced=0,discarded=0;};
    explicit DirectPreviewWorker(std::function<void()> beforeEvaluation={});
    ~DirectPreviewWorker();
    DirectPreviewWorker(const DirectPreviewWorker&)=delete;
    DirectPreviewWorker& operator=(const DirectPreviewWorker&)=delete;
    static std::shared_ptr<const Document> capture(const Document&);
    u64 begin(std::shared_ptr<const Document> baseline,u64 revision,u64 epoch);
    u64 request(arkjson::Value commands,u64 targetFeature,const Mat4& world);
    void cancel();
    void shutdown(); // Join before the host's global evaluation pool is destroyed.
    std::optional<Result> take();
    Statistics statistics() const;
private:
    struct Request {
        u64 gesture=0,generation=0,revision=0,epoch=0,feature=0;
        std::shared_ptr<const Document> baseline;
        arkjson::Value commands;
        Mat4 world;
    };
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    bool stopping_=false;
    u64 gesture_=0,generation_=0,revision_=0,epoch_=0;
    std::shared_ptr<const Document> baseline_;
    std::optional<Request> pending_;
    std::optional<Result> completed_;
    Statistics statistics_;
    std::function<void()> beforeEvaluation_;
    std::thread thread_;
    void run();
};
}
