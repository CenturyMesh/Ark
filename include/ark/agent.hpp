#pragma once
#include "ark/serialize.hpp"
#include "ark/mesh_query.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/failure_record.hpp"
#include <chrono>
#include <set>
#include <map>
namespace ark {
struct Image;
bool metadataOnlyApply(const Document&,const arkjson::Value& args);
arkjson::Value snapshotImageResult(const Image&,const arkjson::Value& visualEvidence,const std::string& path={});
// A session owns its document. UI and MCP call the same command implementation.
// Calls are serialized by the host; no process-global document state.
class AgentSession {
    struct Prepared;
public:
    // A worker can hand off the validated receipt, never a bare foreign ID.
    // Hosts must serialize adoption with live session writes.
    class PreparedTransfer {
    public:
        PreparedTransfer(PreparedTransfer&&) noexcept = default;
        PreparedTransfer& operator=(PreparedTransfer&&) noexcept = default;
        PreparedTransfer(const PreparedTransfer&) = delete;
        PreparedTransfer& operator=(const PreparedTransfer&) = delete;
        const Document& document() const;
        const std::string& id() const;
        const arkjson::Value& result() const;
    private:
        friend class AgentSession;
        explicit PreparedTransfer(std::shared_ptr<Prepared> value):value_(std::move(value)){}
        std::shared_ptr<Prepared> value_;
          mutable std::unique_ptr<Document> materialized_;
    };
    Document document;
    u64 revision=0;
    // Unsaved sessions have a private ephemeral memory scope. Persistent CAD
    // identities use their saved project key; no scope is written to .ark.
    std::string memorySessionScope;
    std::string memoryActor;
    std::string memoryProjectOverride; // Host-selected coding project; never a model argument.
    arkjson::Value call(const std::string& tool,const arkjson::Value& args);
    arkjson::Value info() const;
    arkjson::Value reviewBom(const arkjson::Value& args);
    arkjson::Value bomCompletionCheck(bool onlyIfAuthored=false,bool allowTaskReview=false) const;
    // Host-owned boundary, never resettable through an agent tool.
    void beginBomTask();
    arkjson::Value bomTaskScope() const;
    void markBomAuthored(){bomAuthored_=true;}
    void resetBomReview(){bomAuthored_=false;bomReview_={};bomTaskReview_={};bomTaskActive_=false;bomTaskBaseline_.clear();bomTaskError_.clear();bomInspectedHash_.clear();bomInspectedItems_.clear();bomInspectedRevision_=~u64(0);}
    void observeBomInspection(const arkjson::Value& bom);
    static arkjson::Value tools(bool detailed=false);
    arkjson::Value preparePreview(const arkjson::Value& args);
    arkjson::Value preparedRecipe(const std::string& id) const;
    arkjson::Value proposeManufacturing(const arkjson::Value& args);
    arkjson::Value commitPrepared(const arkjson::Value& args);
    arkjson::Value snapshotCandidate(const arkjson::Value& args);
    std::unique_ptr<PreparedTransfer> takePreparedCandidate(const std::string& id);
    void adoptPreparedCandidate(PreparedTransfer&&, u64 expectedRevision);
    // Host-only completion: run the original request through normal transaction,
    // history and failure guards while dispatch consumes a private worker receipt.
    arkjson::Value completePreparedApply(const arkjson::Value& originalArgs,std::unique_ptr<PreparedTransfer> transfer,const arkjson::Value& rejection={});
    arkjson::Value failureInfo(const arkjson::Value& args) const;
    arkjson::Value failureExport(const arkjson::Value& args) const;
    arkjson::Value failureReplay(const arkjson::Value& args);
    void suppressFailureCapture(bool value=true){suppressFailures_=value;}
    // Scratch geometry probes opt out; only committed document edits are journaled.
    void suppressIterationHistory(bool value=true){suppressHistory_=value;}
private:
    bool bomAuthored_=false;
    arkjson::Value bomReview_;
    arkjson::Value bomTaskReview_;
    bool bomTaskActive_=false;
    std::map<u64,std::string> bomTaskBaseline_;
    std::string bomTaskError_;
    std::string bomInspectedHash_;
    u64 bomInspectedRevision_=~u64(0);
    std::set<u64> bomInspectedItems_;
    arkjson::Value contextGraphCache_;
    std::string contextGraphSource_;
    std::string contextGraphStamp_;
    arkjson::Value dispatch(const std::string& tool,const arkjson::Value& args);
    bool suppressHistory_=false;
    bool suppressFailures_=false;
    std::vector<FailureRecord> failures_;
    arkjson::Value captureFailure(const std::string&,const arkjson::Value&,const arkjson::Value&,const std::string&,u64,arkjson::Value);
    size_t callDepth_=0;
    std::string hostPreparedApplyId_;
    arkjson::Value hostPreparedApplyRejection_;
    struct Prepared {
        std::string id, baseline;
        u64 revision=0;
        std::shared_ptr<Document> document;
          std::vector<u64> bodyOrder;
        std::vector<u64> deferredMeshes;
        arkjson::Value result;
        arkjson::Value recipe,readScope;
        std::string refreshUnsupported;
        std::chrono::steady_clock::time_point expires;
        size_t bytes=0;
        bool metadataOnly=false;
        bool requiresDfmAcceptance=false;
        arkjson::Value dfmComparison;
    };
    std::vector<Prepared> prepared_;
      static Document materializePrepared(const Prepared&);
      void compactPrepared(Prepared&);
      static size_t retainedValueBytes(const arkjson::Value&);
      size_t retainedCandidateBytes(const Prepared* extra=nullptr,bool includeExisting=true) const;
      void trimPrepared();
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
// Full saved-authoring serializations performed by context_graph. A repeated
// read of unchanged authoring does not increment this.
size_t contextGraphAuthoringSerializations();
}
