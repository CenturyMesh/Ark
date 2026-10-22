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
// Entries beyond the derived capacity are omitted in document order rather than
// failing the save; omitted bodies are evaluated normally on the next open.
struct NativeDocumentResultsReport {
    size_t entries=0,bytes=0,omittedEntries=0,omittedBytes=0;
    size_t capacityBytes=maxDocumentDerivedBytes,capacityEntries=maxDocumentDerivedEntries;
};
DocumentDerivedPayloads nativeDocumentResults(const Document& document,NativeDocumentResultsReport* report=nullptr,
                                              size_t capacityBytes=maxDocumentDerivedBytes,size_t capacityEntries=maxDocumentDerivedEntries);
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
// 046B: thread-local control of native requests made by this thread. A true
// predicate stops the isolated worker (polled every 50 ms) and throws an error
// containing NATIVE_CANCELLED; limitMs replaces the default 30000 ms per-request
// watchdog only inside the scope. A timeout keeps the "worker limit" wording.
// Evaluation children inherit a copy of this policy. The predicate may run
// concurrently on those threads: captured state must be thread-safe and live
// until all requests using the policy have finished.
class NativeRequestControlScope {
    const std::function<bool()>* previousCancel;unsigned previousLimit;std::function<bool()> cancel;
public:
    NativeRequestControlScope(std::function<bool()> cancelled,unsigned limitMs);
    ~NativeRequestControlScope();
    NativeRequestControlScope(const NativeRequestControlScope&)=delete;
    NativeRequestControlScope& operator=(const NativeRequestControlScope&)=delete;
};
// Own a copy of the calling thread's policy for a child evaluation task. The
// scope on that task restores the pool thread's previous policy on every exit.
struct NativeRequestControl {std::function<bool()> cancelled;unsigned limitMs=30000;};
NativeRequestControl captureNativeRequestControl();
unsigned nativeRequestLimitMs(); // effective per-request limit on this thread
arkjson::Value exportNativeBrep(const std::string& brep,const std::string& path);
Mesh nativeResultMesh(const arkjson::Value& result);
inline Mesh nativeResultMesh(const NativePayload& result){return result.displayMesh();}
}
