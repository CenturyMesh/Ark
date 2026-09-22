#pragma once
#include "ark/mesh.hpp"
#include "ark/json.hpp"
namespace ark {
struct MeshReport {
    size_t vertices=0, triangles=0, edges=0, components=0;
    size_t boundaryEdges=0, nonManifoldEdges=0, inconsistentEdges=0;
    size_t degenerateTriangles=0, invalidIndices=0, nonFiniteVertices=0;
    f64 area=0, signedVolume=0;
    AABB bounds;
    bool closedOriented() const;
    arkjson::Value json() const;
};
// Read-only indexed-topology audit. No silent welding, healing or tolerance changes.
// Does not test self-intersections, vertex-manifoldness or solid containment.
MeshReport analyzeMesh(const Mesh& mesh);
}
