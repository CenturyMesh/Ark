#pragma once
#include "ark/json.hpp"
#include "ark/mesh.hpp"

namespace ark {
// Read-only collision evidence on the supplied indexed triangle meshes.
// Bounded BVH traversal, fixed 1e-6 mm contact tolerance, explicit rigid frames.
// Boundary intersection and closed-oriented containment remain separate facts.
arkjson::Value inspectMeshCollision(const Mesh& a,const Mat4& aWorld,
                                    const Mesh& b,const Mat4& bWorld);
}
