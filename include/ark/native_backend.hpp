#pragma once
#include "ark/json.hpp"
#include "ark/mesh.hpp"
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
arkjson::Value nativeModelRequest(const arkjson::Value& request);
arkjson::Value nativeKernelInfo();
// Thread-local request allowance; returns previous value (0 inherits host setting).
int setNativeThreadBudget(int threads);
arkjson::Value exportNativeBrep(const std::string& brep,const std::string& path);
Mesh nativeResultMesh(const arkjson::Value& result);
}
