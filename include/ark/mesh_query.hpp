#pragma once
#include "ark/mesh.hpp"
namespace ark {
struct MeshHit { bool hit=false; f64 distance=0; u32 triangle=0; Vec3 point,normal; };
// Median-split AABB hierarchy. Built once per mesh revision, queried read-only.
class MeshBVH {
public:
    explicit MeshBVH(const Mesh& mesh);
    MeshHit raycast(const Mesh& mesh,const Ray& ray) const;
    size_t nodes() const{return nodes_.size();}
private:
    struct Node {AABB box;u32 start=0,count=0,left=0,right=0;};
    std::vector<Node> nodes_;
    std::vector<u32> triangles_;
    u32 build(const Mesh&,u32 start,u32 count);
};
}
