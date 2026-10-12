#pragma once
#include "ark/serialize.hpp"
#include "ark/mesh_query.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/failure_record.hpp"
#include <chrono>
#include <set>
namespace ark {
struct Image;
bool metadataOnlyApply(const Document&,const arkjson::Value& args);
arkjson::Value snapshotImageResult(const Image&,const arkjson::Value& visualEvidence,const std::string& path={});
// A session owns its document. UI and MCP call the same command implementation.
// Calls are serialized by the host; no process-global document state.
class AgentSession {
public:
    Document document;
    u64 revision=0;
    arkjson::Value call(const std::string& tool,const arkjson::Value& args);
    arkjson::Value info() const;
    arkjson::Value reviewBom(const arkjson::Value& args);
    arkjson::Value bomCompletionCheck(bool onlyIfAuthored=false) const;
    void markBomAuthored(){bomAuthored_=true;}
    void resetBomReview(){bomAuthored_=false;bomReview_={};bomInspectedHash_.clear();bomInspectedItems_.clear();bomInspectedRevision_=~u64(0);}
    void observeBomInspection(const arkjson::Value& bom);
    static arkjson::Value tools(bool detailed=false);
    arkjson::Value preparePreview(const arkjson::Value& args);
    arkjson::Value commitPrepared(const arkjson::Value& args);
    arkjson::Value snapshotCandidate(const arkjson::Value& args);
    arkjson::Value failureInfo(const arkjson::Value& args) const;
    arkjson::Value failureExport(const arkjson::Value& args) const;
    arkjson::Value failureReplay(const arkjson::Value& args);
    void suppressFailureCapture(bool value=true){suppressFailures_=value;}
    // Scratch geometry probes opt out; only committed document edits are journaled.
    void suppressIterationHistory(bool value=true){suppressHistory_=value;}
private:
    bool bomAuthored_=false;
    arkjson::Value bomReview_;
    std::string bomInspectedHash_;
    u64 bomInspectedRevision_=~u64(0);
    std::set<u64> bomInspectedItems_;
    arkjson::Value dispatch(const std::string& tool,const arkjson::Value& args);
    bool suppressHistory_=false;
    bool suppressFailures_=false;
    std::vector<FailureRecord> failures_;
    arkjson::Value captureFailure(const std::string&,const arkjson::Value&,const arkjson::Value&,const std::string&,u64,arkjson::Value);
    size_t callDepth_=0;
    struct Prepared {
        std::string id, baseline;
        u64 revision=0;
        std::shared_ptr<Document> document;
        arkjson::Value result;
        std::chrono::steady_clock::time_point expires;
        size_t bytes=0;
        bool metadataOnly=false;
    };
    std::vector<Prepared> prepared_;
    std::vector<std::string> undo_,redo_;
    void remember(bool projectBoundary=false);
    struct Cache { Mesh mesh; MeshReport report; std::unique_ptr<MeshBVH> bvh; };
    std::unordered_map<u64,Cache> cache_;
    u64 cacheRevision_=~u64(0);
    AssemblyMotionProgram motionProgram_;
    u64 motionRevision_=~u64(0);
    Cache& meshCache(const arkjson::Value& args);
};
Document makeDemoDocument();
}
