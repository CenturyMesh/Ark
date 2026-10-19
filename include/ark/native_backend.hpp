#pragma once
#include <functional>
#include "ark/json.hpp"
#include "ark/mesh.hpp"
#include "ark/geometry_storage_032d.hpp"
#include "ark/document_container.hpp"
#include <memory>
namespace ark {
class Document;
struct NativeDocumentResultContext;
class NativeDocumentResultScope {
    std::shared_ptr<NativeDocumentResultContext> previous;
public:
    explicit NativeDocumentResultScope(std::shared_ptr<NativeDocumentResultContext> context);
    ~NativeDocumentResultScope();
    NativeDocumentResultScope(const NativeDocumentResultScope&)=delete;
    NativeDocumentResultScope& operator=(const NativeDocumentResultScope&)=delete;
};
// Optional portable native results remain derived data. Matching loaded entries
// pass through the current native worker's snapshot validation before reuse.
DocumentDerivedPayloads nativeDocumentResults(const Document& document);
arkjson::Value stageNativeDocumentResults(Document& document,const DocumentDerivedPayloads& records);
arkjson::Value nativeWorkerRequest(const arkjson::Value& request);
// Aggregate identity for a fully resolved host boolean sequence. On a miss one
// worker request runs every pairwise step; portable hits validate the saved
// final B-rep in the worker, never replaying the intermediate CSG.
arkjson::Value nativeBooleanSequenceRequest(const arkjson::Value& request);
arkjson::Value nativeModelRequest(const arkjson::Value& request);
arkjson::Value nativeKernelInfo();
// Opt the calling process out of Windows EcoQoS execution-speed throttling
// (native workers are opted out when started). No effect elsewhere.
void disableProcessPowerThrottling();
// Thread-local request allowance; returns previous value (0 inherits host setting).
int setNativeThreadBudget(int threads);
arkjson::Value exportNativeBrep(const std::string& brep,const std::string& path);
Mesh nativeResultMesh(const arkjson::Value& result);
inline Mesh nativeResultMesh(const NativePayload& result){return result.displayMesh();}
}
