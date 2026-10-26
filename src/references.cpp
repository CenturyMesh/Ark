#include "ark/references.hpp"
#include "ark/native_backend.hpp"
#include "ark/native_features.hpp"
#include <cmath>
#include <stdexcept>

namespace ark {
GeometryReference resolveGeometryReference(const Document& document, u64 feature,
                                           const std::string& name, double fraction) {
    if (name.empty() || !std::isfinite(fraction) || fraction < 0 || fraction > 1)
        throw std::runtime_error("Invalid semantic geometry reference");
    const Body* body = nullptr;
    auto& mutableDocument = const_cast<Document&>(document);
    auto& mutableBodies = document.evaluatingBodies
        ? *const_cast<std::vector<Body>*>(document.evaluatingBodies)
        : mutableDocument.evalBodies;
    ensureNativeBody(mutableDocument, feature, mutableBodies);
    const auto& bodies = mutableBodies;
    for (const auto& candidate : bodies)
        if (candidate.id == feature) { body = &candidate; break; }
    if (!body) throw std::runtime_error("Reference producer is not evaluated");
    const auto* brep = body->nativeData.find("brep_base64");
    const auto* topology = body->nativeData.find("topology");
    if (!brep || brep->t != arkjson::Value::Str || !topology)
        throw std::runtime_error("Reference producer has no exact B-rep topology");
    const std::string sample=name+":"+arkjson::Value::makeNum(fraction).dump();
    // NativePayload is immutable/COW. Custody of that exact owner proves both
    // B-rep and topology identity without dumping/comparing megabytes for every
    // mate endpoint. A weak owner neither retains unloaded geometry nor permits
    // a recycled address to reuse an old reference frame.
    if(document.referenceFrameCache.size()>=32&&!document.referenceFrameCache.count(feature))document.referenceFrameCache.clear();
    auto& cache=document.referenceFrameCache[feature];
    const auto owner=cache.owner.lock();
    if(!owner||owner.get()!=body->nativeData.ownerIdentity()){cache={};cache.owner=body->nativeData.weakOwner();}
    auto existing=cache.frames.find(sample);
    arkjson::Value response;
    if(existing!=cache.frames.end())response=existing->second;
    else{
        // Construct large native transport copies only on an actual cache miss.
        arkjson::Value request=arkjson::Value::makeObj(),worker=arkjson::Value::makeObj();
        request.set("input_brep",*brep);request.set("topology",*topology);
        request.set("reference",arkjson::Value::makeStr(name));request.set("fraction",arkjson::Value::makeNum(fraction));
        worker.set("operation",arkjson::Value::makeStr("reference_frame"));worker.set("request",std::move(request));response=nativeWorkerRequest(worker);
    }
    const auto* frame = response.find("frame");
    const auto* kind = response.find("kind");
    if (!frame || frame->t != arkjson::Value::Arr || frame->arr.size() != 16 ||
        !kind || kind->t != arkjson::Value::Str)
        throw std::runtime_error("Invalid native reference frame response");
    GeometryReference result;
    result.kind = kind->str;
    for (size_t i = 0; i < 16; ++i) {
        if (frame->arr[i].t != arkjson::Value::Num || !std::isfinite(frame->arr[i].num))
            throw std::runtime_error("Non-finite native reference frame");
        result.frame.m[i] = frame->arr[i].num;
    }
    if (const auto* radius = response.find("radius")) {
        if (radius->t != arkjson::Value::Num || !std::isfinite(radius->num))
            throw std::runtime_error("Invalid native reference radius");
        result.radius = radius->num;
    }
    if(existing==cache.frames.end()){if(cache.frames.size()>=64)cache.frames.clear();cache.frames[sample]=response;}
    return result;
}
}
