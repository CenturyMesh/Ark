// ============================================================================
//  ARK-Point :: ark/mesh.hpp
//  Display/export triangle meshes + parallel adaptive tessellation + tools.
//  Positions/normals are tightly packed f32 arrays (SIMD/asm-friendly).
// ===========================================================================
#pragma once
#include "ark/core.hpp"
#include <vector>

namespace ark {

struct Surface;

struct Vec3f {
    f32 x=0,y=0,z=0;
    Vec3f() = default;
    Vec3f(f32 x_,f32 y_,f32 z_):x(x_),y(y_),z(z_){}
    Vec3f(const Vec3& v):x((f32)v.x),y((f32)v.y),z((f32)v.z){}
    operator Vec3() const { return {x,y,z}; }
};

// ---------------------------------------------------------------------------
struct Mesh {
    std::vector<f32> pos;    // xyz per vertex
    std::vector<f32> nrm;    // xyz per vertex (smooth normals)
    std::vector<u32> idx;    // CCW triangles

    size_t vertCount() const { return pos.size()/3; }
    size_t triCount()   const { return idx.size()/3; }

    void addVertex(const Vec3& p){ pos.push_back((f32)p.x); pos.push_back((f32)p.y); pos.push_back((f32)p.z); }
    void addTri(u32 a, u32 b, u32 c){ idx.insert(idx.end(),{a,b,c}); }
    void addTri3(u32 a,u32 b,u32 c){ idx.push_back(a); idx.push_back(b); idx.push_back(c); }
    AABB computeBBox() const {
        AABB bb;
        for (size_t i = 0; i < vertCount(); ++i)
            bb.grow(Vec3{pos[i*3],pos[i*3+1],pos[i*3+2]});
        return bb;
    }
    void computeNormals();          // area-weighted smooth vertex normals
    void weld(f64 eps=1e-7);        // spatial-hash vertex dedup
    void flip(){ for(size_t i=0;i<idx.size();i+=3) std::swap(idx[i+1],idx[i]); }
    void append(const Mesh& o, const Mat4& X = Mat4::identity());
    void clear(){ pos.clear(); nrm.clear(); idx.clear(); }
};

// ---------------------------------------------------------------------------
// Adaptive tessellation -------------------------------------------------------
// Curvature-driven subdivision of the surface UV domain + trim handling.
// Parallelized across faces by the caller.
struct TessOptions {
    f64 tol       = 2e-3;   // chordal deviation (model units)
    f64 maxEdge   = 1e300;   // hard edge length cap
    f64 minAngleDeg = 8.0;   // refine skinny cells
    int maxLevels = 10;      // quadtree depth cap
};

Mesh tessellateSurface(const Surface& s, const TessOptions& opt);
void tessellateBodyFaces(const std::vector<const Surface*>& faces,
                         const std::vector<std::vector<std::vector<Vec2>>>& loopsPerFace,
                         const TessOptions& opt, Mesh& out);   // parallel

// force consistent outward orientation of a closed mesh (divergence theorem)
void meshOrientOutward(Mesh& m);

// point-in-trimmed-region test (even-odd over loops) in UV
bool uvInsideLoops(const std::vector<std::vector<Vec2>>& loops, Vec2 p);
std::vector<Vec2> densifyLoopUV(const std::function<Vec2(f64)>& param, int segs);

// ---------------------------------------------------------------------------
// CSG on closed meshes (BSP-based, robust epsilons) ---------------------------
enum class CsgOp { Union, Subtract, Intersect };
Mesh meshBoolean(const Mesh& A, const Mesh& B, CsgOp op);

// watertightness / orientation utilities
bool meshIsClosed(const Mesh& m);
i64  meshEulerCharacteristic(const Mesh& m);

// Triangulate a simple (possibly concave, non-self-intersecting) polygon by
// ear clipping. Returns flat CCW index triples into pts. Degenerate inputs
// fall back to a fan from vertex 0.
std::vector<u32> triangulatePolygon(const std::vector<Vec2>& pts);

} // namespace ark
