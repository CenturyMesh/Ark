// ============================================================================
//  ARK-Point :: ark/nurbs.hpp
//  Geometry layer: parametric curves & surfaces.
//    * exact rational NURBS curves/surfaces  (Piegl & Tiller, "The NURBS Book")
//    * analytic surfaces (plane/cylinder/cone/sphere/torus)
//    * projections / closest-point (Newton refinement over sample seeds)
//  All evaluation supports homogeneous weights -> full conic coverage.
// ===========================================================================
#pragma once
#include "ark/core.hpp"
#include <vector>

namespace ark {

// ---------------------------------------------------------------------------
// Curves ------------------------------------------------------------------------
enum class CurveKind : u32 { Line, Arc, Nurbs };

struct Curve {                       // abstract parametric 3D curve
    virtual ~Curve() = default;
    virtual CurveKind kind() const = 0;
    virtual Vec3   eval(f64 t) const = 0;
    virtual Vec3   tangent(f64 t) const = 0;      // unnormalized
    virtual f64    tmin() const = 0;
    virtual f64    tmax() const = 0;
    virtual AABB   bbox() const;
    virtual std::vector<Vec3> flatten(int minSegs=16, f64 tol=1e-4) const; // adaptive polyline
};

struct LineCurve final : Curve {
    Vec3 a, b;
    LineCurve(Vec3 a_, Vec3 b_) : a(a_), b(b_) {}
    CurveKind kind() const override { return CurveKind::Line; }
    Vec3 eval(f64 t) const override { return a + (b-a)*t; }
    Vec3 tangent(f64) const override { return (b-a); }
    f64 tmin() const override { return 0; }
    f64 tmax() const override { return 1; }
};

struct ArcCurve final : Curve {     // circular arc: p(t)=c + r(cosθ x + sinθ y)
    Vec3 c, xdir, ydir;             // orthonormal frame, ydir = n×xdir typically
    f64  radius=1, a0=0, a1=K_2PI;  // angle range [a0,a1] rad
    ArcCurve() = default;
    ArcCurve(Vec3 center, Vec3 normal, f64 r);
    CurveKind kind() const override { return CurveKind::Arc; }
    Vec3 eval(f64 t) const override {
        f64 th = lerp(a0,a1,t), cs=std::cos(th), sn=std::sin(th);
        return c + (xdir*cs + ydir*sn)*radius;
    }
    Vec3 tangent(f64 t) const override {
        f64 th = lerp(a0,a1,t), dth=(a1-a0);
        return (-xdir*std::sin(th) + ydir*std::cos(th)) * radius * dth;
    }
    f64 tmin() const override { return 0; }
    f64 tmax() const override { return 1; }
};

struct NurbsCurve final : Curve {   // rational B-spline curve
    int p = 3;                      // degree
    std::vector<f64> U;             // knots, size m+1 = n+1 + p+1
    std::vector<Vec3> P;            // control points
    std::vector<f64> W;             // weights (all 1 => non-rational)

    CurveKind kind() const override { return CurveKind::Nurbs; }
    f64 tmin() const override { return U[p]; }
    f64 tmax() const override { return U[U.size()-p-1]; }

    static NurbsCurve polyline(const std::vector<Vec3>& pts);           // degree-1 fit
    static NurbsCurve interpolateCubic(const std::vector<Vec3>& pts);   // global cubic interp
    static NurbsCurve circle(Vec3 c, Vec3 normal, f64 r);               // exact rational circle

    int nCtrl() const { return (int)P.size(); }
    int findSpan(f64 u) const;
    void basis(f64 u, int span, f64* N) const;                 // N[0..p]
    Vec3 eval(f64 t) const override;
    Vec3 tangent(f64 t) const override;
};

// ---------------------------------------------------------------------------
// Surfaces -----------------------------------------------------------------------
enum class SurfKind : u32 { Plane, Cylinder, Cone, Sphere, Torus, Nurbs };

struct Surface {
    virtual ~Surface() = default;
    virtual SurfKind kind() const = 0;
    virtual Vec3   eval(f64 u, f64 v) const = 0;
    virtual Vec3   normalAt(f64 u, f64 v) const = 0;          // unit
    virtual void   domain(f64& u0,f64& u1,f64& v0,f64& v1) const = 0;
    virtual bool   closedU() const { return false; }
    virtual bool   closedV() const { return false; }
    virtual AABB   bbox() const;
    virtual Vec3   project(Vec3 p) const;                     // closest surface point
    virtual std::unique_ptr<Surface> transformed(const Mat4& M) const = 0;

    // second fundamental form magnitude (curvature) at uv — drives meshing density
    virtual f64 curvatureEstimate(f64 u, f64 v) const { (void)u;(void)v; return 0; }
};

struct PlaneSurface final : Surface {         // p(u,v)=origin + u*uDir + v*vDir
    Vec3 origin, uDir, vDir;
    f64  uExt=1, vExt=1;
    PlaneSurface() = default;
    PlaneSurface(Vec3 o, Vec3 ud, Vec3 vd, f64 ue=1, f64 ve=1)
        : origin(o), uDir(ud.normalized()), vDir(vd.normalized()), uExt(ue), vExt(ve) {}
    SurfKind kind() const override { return SurfKind::Plane; }
    Vec3 eval(f64 u,f64 v) const override { return origin + uDir*(u*uExt) + vDir*(v*vExt); }
    Vec3 normalAt(f64,f64) const override { return uDir.cross(vDir).normalized(); }
    void domain(f64&u0,f64&u1,f64&v0,f64&v1) const override { u0=0;u1=1;v0=0;v1=1; }
    std::unique_ptr<Surface> transformed(const Mat4& M) const override {
        return std::make_unique<PlaneSurface>(M.transformPoint(origin),
            M.transformDir(uDir), M.transformDir(vDir), uExt, vExt);
    }
};

struct CylinderSurface final : Surface {      // axis z of frame, v along axis
    Vec3 base; Vec3 axis; f64 radius=1, height=1;
    Vec3 refX, refY;                          // orthonormal ⊥ axis
    CylinderSurface() = default;
    CylinderSurface(Vec3 base_, Vec3 axis_, f64 r, f64 h);
    SurfKind kind() const override { return SurfKind::Cylinder; }
    Vec3 eval(f64 u,f64 v) const override {
        f64 th=u*K_2PI;
        return base + axis*(v*height) + (refX*std::cos(th)+refY*std::sin(th))*radius;
    }
    Vec3 normalAt(f64 u,f64) const override {
        f64 th=u*K_2PI; return (refX*std::cos(th)+refY*std::sin(th));
    }
    void domain(f64&u0,f64&u1,f64&v0,f64&v1) const override { u0=0;u1=1;v0=0;v1=1; }
    bool closedU() const override { return true; }
    f64 curvatureEstimate(f64,f64) const override { return 1.0/tmax(radius, K_EPS); }
    std::unique_ptr<Surface> transformed(const Mat4& M) const override {
        return std::make_unique<CylinderSurface>(M.transformPoint(base),
            M.transformDir(axis).normalized(), radius*M.transformDir(refX).len(),
            height*M.transformDir(axis).len());
    }
};

struct ConeSurface final : Surface {          // apex at top, base radius at bottom
    Vec3 base, axis; f64 rBase=1, rTop=0, height=1;
    Vec3 refX, refY;
    ConeSurface() = default;
    ConeSurface(Vec3 base_, Vec3 axis_, f64 rb, f64 rt, f64 h);
    SurfKind kind() const override { return SurfKind::Cone; }
    f64 radAt(f64 v) const { return lerp(rBase, rTop, v); }
    Vec3 eval(f64 u,f64 v) const override {
        f64 th=u*K_2PI, r=radAt(v);
        return base + axis*(v*height) + (refX*std::cos(th)+refY*std::sin(th))*r;
    }
    Vec3 normalAt(f64 u,f64 v) const override {
        f64 th=u*K_2PI;
        Vec3 radial=refX*std::cos(th)+refY*std::sin(th);
        f64 slope=(rBase-rTop)/tmax(height,K_EPS);
        return (radial - axis*slope).normalized();
    }
    void domain(f64&u0,f64&u1,f64&v0,f64&v1) const override { u0=0;u1=1;v0=0;v1=1; }
    bool closedU() const override { return true; }
    f64 curvatureEstimate(f64,f64) const override { return 1.0/tmax(rBase,K_EPS); }
    std::unique_ptr<Surface> transformed(const Mat4& M) const override {
        auto s=std::make_unique<ConeSurface>(M.transformPoint(base),
            M.transformDir(axis).normalized(), rBase, rTop, height);
        s->refX=M.transformDir(refX).normalized(); s->refY=M.transformDir(refY).normalized();
        return s;
    }
};

struct SphereSurface final : Surface {
    Vec3 center; f64 radius=1;
    explicit SphereSurface(Vec3 c={}, f64 r=1):center(c),radius(r){}
    SurfKind kind() const override { return SurfKind::Sphere; }
    Vec3 eval(f64 u,f64 v) const override {
        f64 th=u*K_2PI, ph=v*K_PI;
        f64 sp=std::sin(ph);
        return center + Vec3(sp*std::cos(th)*radius, std::cos(ph)*radius, sp*std::sin(th)*radius);
    }
    Vec3 normalAt(f64 u,f64 v) const override { return (eval(u,v)-center)/tmax(radius,K_EPS); }
    void domain(f64&u0,f64&u1,f64&v0,f64&v1) const override { u0=0;u1=1;v0=0;v1=1; }
    bool closedU() const override { return true; }
    f64 curvatureEstimate(f64,f64) const override { return 1.0/tmax(radius,K_EPS); }
    std::unique_ptr<Surface> transformed(const Mat4& M) const override {
        return std::make_unique<SphereSurface>(M.transformPoint(center),
            radius*M.transformDir({1,0,0}).len());
    }
};

struct TorusSurface final : Surface {
    Vec3 center; Vec3 axis{0,1,0}; f64 R=2, r=0.5;
    Vec3 refX, refY;
    TorusSurface() = default;
    TorusSurface(Vec3 c, Vec3 ax, f64 R_, f64 r_);
    SurfKind kind() const override { return SurfKind::Torus; }
    Vec3 eval(f64 u,f64 v) const override {
        f64 tu=u*K_2PI, tv=v*K_2PI;
        Vec3 ringC=center + axis*0.0; // ring plane ⟂ axis through center
        Vec3 radial=(refX*std::cos(tu)+refY*std::sin(tu));
        return ringC + radial*(R + r*std::cos(tv)) + axis*(r*std::sin(tv));
    }
    Vec3 normalAt(f64 u,f64 v) const override {
        f64 tv=v*K_2PI;
        Vec3 radial=refX*std::cos(u*K_2PI)+refY*std::sin(u*K_2PI);
        return (radial*std::cos(tv)+axis*std::sin(tv)).normalized();
    }
    void domain(f64&u0,f64&u1,f64&v0,f64&v1) const override { u0=0;u1=1;v0=0;v1=1; }
    bool closedU() const override { return true; }
    bool closedV() const override { return true; }
    f64 curvatureEstimate(f64,f64) const override { return 1.0/tmax(tmin(R,r),K_EPS); }
    std::unique_ptr<Surface> transformed(const Mat4& M) const override {
        auto t=std::make_unique<TorusSurface>(M.transformPoint(center),
            M.transformDir(axis).normalized(),R,r);
        t->refX=M.transformDir(refX).normalized(); t->refY=M.transformDir(refY).normalized();
        return t;
    }
};

// Tensor-product rational B-spline surface -------------------------------------
struct NurbsSurface final : Surface {
    int pu=3, pv=3;
    std::vector<f64> U, V;                 // knot vectors
    std::vector<Vec3> P;                   // nu x nv control net (row-major u-major)
    std::vector<f64>  W;                   // matching weights
    int nu=0, nv=0;

    SurfKind kind() const override { return SurfKind::Nurbs; }
    f64 umin() const { return U[pu]; }              f64 umax() const { return U[U.size()-pu-1]; }
    f64 vmin() const { return V[pv]; }              f64 vmax() const { return V[V.size()-pv-1]; }

    Vec3& cpt(int i,int j){ return P[(size_t)i*nv+j]; }
    const Vec3& cpt(int i,int j) const { return P[(size_t)i*nv+j]; }
    f64   wgt(int i,int j) const { return W.empty()?1.0:W[(size_t)i*nv+j]; }

    Vec3 eval(f64 u,f64 v) const override;
    void evalDers(f64 u,f64 v, Vec3* S, Vec3* Su, Vec3* Sv) const;   // point + partials
    Vec3 normalAt(f64 u,f64 v) const override { Vec3 s,su,sv; evalDers(u,v,&s,&su,&sv); return su.cross(sv).normalized(); }
    void domain(f64&u0,f64&u1,f64&v0,f64&v1) const override { u0=umin();u1=umax();v0=vmin();v1=vmax(); }
    f64 curvatureEstimate(f64 u,f64 v) const override;
    std::unique_ptr<Surface> transformed(const Mat4& M) const override;
};

// shared helpers ---------------------------------------------------------------
int  nurbs_find_span(int n, int p, f64 u, const std::vector<f64>& U);
void nurbs_basis(int span, f64 u, int p, const std::vector<f64>& U, f64* N);
Vec3 nurbs_curve_point(const NurbsCurve& c, f64 u);

} // namespace ark
