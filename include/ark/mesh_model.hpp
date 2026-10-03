#pragma once
#include "ark/model.hpp"

namespace ark {

// Evaluates mesh_body and the mesh_* features. Exact B-rep inputs are rejected
// unless p.sample is 1; the result then stays a sampled mesh and carries no B-rep.
Body evaluateMeshFeature(const Feature& feature, const ExprEnv& env,
                         const std::vector<Body>& preceding, const TessOptions& tessellation);

// Shared limits for authored and imported triangle sets.
constexpr size_t kMeshModelMaxVertices = 12288;
constexpr size_t kMeshModelMaxTriangles = 4096;

Mesh meshFromPayload(const arkjson::Value& mesh);
arkjson::Value meshToPayload(const Mesh& mesh);

} // namespace ark
