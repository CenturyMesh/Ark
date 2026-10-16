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
// Guards indexed selections against upstream topology changes, while allowing
// the same connectivity to follow dimensional/coordinate changes.
std::string meshTopologyFingerprint(const Mesh& mesh);

// Bounded complete triangle-pair contact diagnosis. Shared indexed vertices and
// edges alone are legal adjacency; contact beyond that simplex is reported.
// Rejects invalid/degenerate inputs or exhausted limits instead of partial proof.
arkjson::Value inspectMeshSelfIntersections(const Mesh& mesh);

} // namespace ark
