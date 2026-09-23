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
    arkjson::Value request = arkjson::Value::makeObj();
    request.set("input_brep", *brep);
    request.set("topology", *topology);
    request.set("reference", arkjson::Value::makeStr(name));
    request.set("fraction", arkjson::Value::makeNum(fraction));
    arkjson::Value worker = arkjson::Value::makeObj();
    worker.set("operation", arkjson::Value::makeStr("reference_frame"));
    worker.set("request", std::move(request));
    auto response = nativeWorkerRequest(worker);
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
    return result;
}
}
