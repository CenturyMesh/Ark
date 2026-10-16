// ============================================================================
//  ARK-Point :: ark/brep.hpp
//  Boundary representation solids â€” Parasolid-flavoured:
//    Body -> Faces (analytic/NURBS surface + UV trim loops) -> stable IDs.
//  Feature history re-evaluates exact geometry; booleans bake watertight
//  meshes while preserving the parametric intent in the document DAG.
// ===========================================================================
#pragma once
#include "ark/core.hpp"
#include "ark/nurbs.hpp"
#include "ark/mesh.hpp"
#include "ark/json.hpp"
#include "ark/geometry_storage_032d.hpp"
#include <memory>

namespace ark {

using FaceId = i32;

// Trim loop: closed polyline in surface-UV space. Outer loops CCW, holes CW
// (enforced by normalizeLoops).
struct TrimLoop {
    std::vector<Vec2> uv;
    bool outer = true;
};

struct Face {
    u64 stableId = 0;                                  // topological naming anchor
    std::shared_ptr<Surface> surf;
    std::vector<TrimLoop> loops;                       // empty => full domain
    std::string role;                                  // "wall","cap","fillet",...

    AABB bbox() const {
        if (!surf) return {};
        AABB bb = surf->bbox();
        return bb;
    }
};

// ---------------------------------------------------------------------------
// 2D profile (sketch) segments used by extrude/revolve ------------------------
struct ProfileSeg {
    enum Type { Line, Arc } type = Line;
    Vec2 p0, p1;                    // line endpoints (profile space)
    Vec2 c; f64 r=0, a0=0, a1=0;    // arc center/radius/angles
};
struct SketchProfile {
    Mat4 frame = Mat4::identity();   // maps profile XY plane into model space
    std::vector<ProfileSeg> segs;    // closed loop
    std::vector<Vec2> flattenPts(int perArcSegs=24) const;
};

struct Body {
    Body()=default;
    Body(const Body& b):faces(b.faces),baked(b.baked),nativeData(b.nativeData),cachedBox(b.cachedBox),id(b.id),dirty(b.dirty),kind(b.kind),pd1(b.pd1),pd2(b.pd2),profile(b.profile),prismHoles(b.prismHoles),sweepDeg(b.sweepDeg){}
    Body(Body&&)=default;
    Body& operator=(Body&&)=default;
    Body& operator=(const Body& b){if(this!=&b){Body copy(b);*this=std::move(copy);}return *this;}
    std::vector<Face> faces;                           // exact B-rep faces
    MeshSnapshot baked;                               // shared immutable display geometry
    NativePayload nativeData;                         // shared exact producer payload, packed display channels
    mutable AABB cachedBox;
    u64 id = 0;
    bool dirty = true;

    // watertight fast-path descriptor: tessellate() uses analytic builders
    // for primitives/sweeps so display & CSG meshes are crack-free.
    std::string kind;                                  // "",box,cylinder,sphere,cone,torus,extrude,revolve
    Vec3 pd1{}, pd2{};                                 // dims / (r,h) / r / (rb,rt,h) / (R,r)
    SketchProfile profile;                             // sweeps
    std::vector<std::vector<Vec2>> prismHoles;           // sampled through-cut loops in profile coordinates
    f64 sweepDeg = 360;

    Mesh buildWatertight() const;
    AABB bbox() const {
        if (cachedBox.valid()) return cachedBox;
        AABB bb;
        if (baked) bb.grow(baked->computeBBox());
        for (auto& f : faces) bb.grow(f.bbox());
        cachedBox = bb;
        return bb;
    }
    Mesh tessellate(const TessOptions& opt) const;
    void invalidate(){ dirty=true; cachedBox=AABB{}; }
};

// ---------------------------------------------------------------------------
// Primitive factories (exact analytic faces) ----------------------------------
Body makeBox(Vec3 dims);
Body makeCylinder(f64 radius, f64 height);
Body makeSphere(f64 radius);
Body makeCone(f64 rBase, f64 rTop, f64 height);
Body makeTorus(f64 R, f64 r);

// Swept features ----------------------------------------------------------------
Body extrudeProfile(const SketchProfile& prof, f64 height, bool both=false);
Body revolveProfile(const SketchProfile& prof, f64 angleDeg);   // around profile Y axis

// Transforms ---------------------------------------------------------------------
Body transformBody(const Body& b, const Mat4& M);

u64 nextStableFaceId();   // global monotonic

} // namespace ark
