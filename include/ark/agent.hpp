#pragma once
#include "ark/serialize.hpp"
#include "ark/mesh_query.hpp"
#include "ark/mesh_analysis.hpp"
#include <chrono>
namespace ark {
// A session owns its document. UI and MCP call the same command implementation.
// Calls are serialized by the host; no process-global document state.
class AgentSession {
public:
    Document document;
    u64 revision=0;
    arkjson::Value call(const std::string& tool,const arkjson::Value& args);
    arkjson::Value info() const;
    static arkjson::Value tools(bool detailed=false);
    arkjson::Value preparePreview(const arkjson::Value& args);
    arkjson::Value commitPrepared(const arkjson::Value& args);
private:
    struct Prepared {
        std::string id, baseline;
        u64 revision=0;
        std::shared_ptr<Document> document;
        arkjson::Value result;
        std::chrono::steady_clock::time_point expires;
        size_t bytes=0;
    };
    std::vector<Prepared> prepared_;
    std::vector<std::string> undo_,redo_;
    void remember();
    struct Cache { Mesh mesh; MeshReport report; std::unique_ptr<MeshBVH> bvh; };
    std::unordered_map<u64,Cache> cache_;
    u64 cacheRevision_=~u64(0);
    Cache& meshCache(const arkjson::Value& args);
};
Document makeDemoDocument();
}
