// ARK-Point :: nurbs.cpp — NURBS + analytic surface evaluation.
#include "ark/nurbs.hpp"

namespace ark {

// ---------------------------------------------------------------------------
// Curve base -----------------------------------------------------------------
AABB Curve::bbox() const {
    AABB bb;
    for (const auto& p : flatten(32, 1e-3)) bb.grow(p);
    return bb;
}
std::vector<Vec3> Curve::flatten(int minSegs, f64 tol) const {
    // adaptive chordal flattening: recursive midpoint refinement
    std::vector<Vec3> out;
    f64 t0 = tmin(), t1 = tmax();
    struct Seg { f64 a, b; };
    std::vector<Seg> stack{ {t0, t1} };
    int segs = 0, maxSegs = (minSegs<1?1:minSegs) * 4096;
    Vec3 p0 = eval(t0);
    out.push_back(p0);
    while (!stack.empty() && segs < maxSegs) {
        auto [ta, tb] = stack.back(); stack.pop_back();
        Vec3 pa = eval(ta), pb = eval(tb);
        f64 tm = (ta+tb)*0.5;
        Vec3 pm = eval(tm), mid = (pa+pb)*0.5;
        if ((segs < minSegs) || (pm-mid).len() > tol) {
            stack.push_back({tm, tb});
            stack.push_back({ta, tm});
        } else {
            out.push_back(pb); ++segs;
        }
    }
    return out;
}

ArcCurve::ArcCurve(Vec3 center, Vec3 normal, f64 r)
    : c(center), radius(r) {
    Vec3 n = normal.normalized();
    xdir = (std::fabs(n.x) < 0.9 ? Vec3(1,0,0) : Vec3(0,0,1)).cross(n).normalized();
    ydir = n.cross(xdir).normalized();
    a0 = 0; a1 = K_2PI;
}

// ---------------------------------------------------------------------------
// NURBS core (Piegl & Tiller A2.1/A2.2/A2.3) ------------------------------------
int nurbs_find_span(int n, int p, f64 u, const std::vector<f64>& U) {
    if (u >= U[n+1]) return n;
    int lo = p, hi = n+1, mid = (lo+hi)/2;
    while (u < U[mid] || u >= U[mid+1]) {
        if (u < U[mid]) hi = mid; else lo = mid;
        mid = (lo+hi)/2;
    }
    return mid;
}
void nurbs_basis(int span, f64 u, int p, const std::vector<f64>& U, f64* N) {
    static thread_local f64 left[32], right[32];
    N[0] = 1.0;
    for (int j = 1; j <= p; ++j) {
        left[j]  = u - U[span+1-j];
        right[j] = U[span+j] - u;
        f64 saved = 0.0;
        for (int r = 0; r < j; ++r) {
            f64 temp = N[r] / (right[r+1] + left[j-r]);
            N[r] = saved + right[r+1]*temp;
            saved = left[j-r]*temp;
        }
        N[j] = saved;
    }
}

int NurbsCurve::findSpan(f64 u) const { return nurbs_find_span(nCtrl()-1, p, u, U); }
void NurbsCurve::basis(f64 u, int span, f64* N) const { nurbs_basis(span, u, p, U, N); }



Vec3 nurbs_curve_point(const NurbsCurve& c, f64 u) {
    int span = c.findSpan(u);
    std::vector<f64> N(c.p+1);
    nurbs_basis(span, u, c.p, c.U, N.data());
    Vec4 acc;
    for (int i = 0; i <= c.p; ++i) {
        const Vec3& P = c.P[span-c.p+i];
        f64 w = c.W.empty()?1.0:c.W[span-c.p+i];
        f64 b = N[i]*w;
        acc.x += P.x*b; acc.y += P.y*b; acc.z += P.z*b; acc.w += b;
    }
    return {acc.x/acc.w, acc.y/acc.w, acc.z/acc.w};
}

Vec3 NurbsCurve::eval(f64 t) const { return nurbs_curve_point(*this, t); }
Vec3 NurbsCurve::tangent(f64 t) const {
    const f64 h = 1e-6;
    f64 t0 = t-h < tmin() ? tmin() : t+h > tmax() ? tmax()-2*h : t;
    return (eval(t0+2*h)-eval(t0));
}

NurbsCurve NurbsCurve::polyline(const std::vector<Vec3>& pts) {
    NurbsCurve c; c.p = 1;
    size_t n = pts.size();
    if (!n) return c;
    c.P = pts;
    c.W.assign(n, 1.0);
    c.U.assign(n+2, 0.0);                 // clamped degree-1: [0,...,n-1,n-1]
    for (size_t i = 1; i <= n; ++i) c.U[i] = (f64)(i-1);
    c.U[n+1] = (f64)(n-1);
    return c;
}

NurbsCurve NurbsCurve::interpolateCubic(const std::vector<Vec3>& pts) {
    // C1 cubic interpolation: Catmull-Rom tangents -> chain of cubic Bezier
    // spans joined by multiplicity-4 knots (duplicated joint control points).
    int n=(int)pts.size();
    NurbsCurve c; c.p=3;
    if(n==0) return c;
    if(n==1){ c.P.assign(4,pts[0]); c.W.assign(4,1); c.U={0,0,0,0,1,1,1,1}; return c; }
    std::vector<Vec3> T(n);
    for(int i=0;i<n;++i){
        Vec3 tp=i>0?pts[i]-pts[i-1]:pts[1]-pts[0];
        Vec3 tn=i<n-1?pts[i+1]-pts[i]:pts[n-1]-pts[n-2];
        T[i]=(tp+tn)*0.5;
    }
    int L=n-1;
    c.P.reserve((size_t)L*4); c.W.reserve((size_t)L*4); c.U.reserve((size_t)L*4+4);
    for(int k=0;k<L;++k){
        f64 d=(pts[k+1]-pts[k]).len();
        Vec3 m0=T[k]*(d/3.0), m1=T[k+1]*(d/3.0);
        c.P.push_back(pts[k]);
        c.P.push_back(pts[k]+m0);
        c.P.push_back(pts[k+1]-m1);
        c.P.push_back(pts[k+1]);
        for(int q=0;q<4;++q){ c.W.push_back(1.0); c.U.push_back((f64)k); }
    }
    for(int q=0;q<4;++q) c.U.push_back((f64)L);
    return c;
}

NurbsCurve NurbsCurve::circle(Vec3 center, Vec3 normal, f64 r) {
    // Exact rational circle: 9 control points, quadratic (Piegl & Tiller ex.7.4).
    // Inner control points sit at tangent intersections: c+(x+y)r etc;
    // their weights are cos(half central angle) = sqrt(.5).
    Vec3 n = normal.normalized();
    Vec3 x = (std::fabs(n.x)<0.9?Vec3(1,0,0):Vec3(0,0,1)).cross(n).normalized();
    Vec3 y = n.cross(x).normalized();
    NurbsCurve c; c.p = 2;
    f64 w = std::sqrt(0.5);
    Vec3 pts[9] = {
        center+x*r,
        center+(x+y)*r,          // tangent-intersection point (magnitude r*sqrt2)
        center+y*r,
        center+(y-x)*r,
        center-x*r,
        center-(x+y)*r,
        center-y*r,
        center+(x-y)*r,
        center+x*r
    };
    f64 ws[9] = {1,w,1,w,1,w,1,w,1};
    for (int i = 0; i < 9; ++i){ c.P.push_back(pts[i]); c.W.push_back(ws[i]); }
    c.U = {0,0,0, 0.25,0.25, 0.5,0.5, 0.75,0.75, 1,1,1};
    return c;
}

// ---------------------------------------------------------------------------
// Analytic surfaces --------------------------------------------------------------
CylinderSurface::CylinderSurface(Vec3 base_, Vec3 axis_, f64 r, f64 h)
    : base(base_), axis(axis_.normalized()), radius(r), height(h) {
    refX = (std::fabs(axis.x)<0.9?Vec3(1,0,0):Vec3(0,1,0)).cross(axis).normalized();
    refY = axis.cross(refX).normalized();
}
ConeSurface::ConeSurface(Vec3 base_, Vec3 axis_, f64 rb, f64 rt, f64 h)
    : base(base_), axis(axis_.normalized()), rBase(rb), rTop(rt), height(h) {
    refX = (std::fabs(axis.x)<0.9?Vec3(1,0,0):Vec3(0,1,0)).cross(axis).normalized();
    refY = axis.cross(refX).normalized();
}
TorusSurface::TorusSurface(Vec3 c, Vec3 ax, f64 R_, f64 r_)
    : center(c), axis(ax.normalized()), R(R_), r(r_) {
    refX = (std::fabs(axis.x)<0.9?Vec3(1,0,0):Vec3(0,1,0)).cross(axis).normalized();
    refY = axis.cross(refX).normalized();
}

AABB Surface::bbox() const {
    AABB bb;
    f64 u0,u1,v0,v1; domain(u0,u1,v0,v1);
    const int N = closedU()?48:24, M = closedV()?48:24;
    for (int i = 0; i <= N; ++i)
        for (int j = 0; j <= M; ++j)
            bb.grow(eval(lerp(u0,u1,(f64)i/N), lerp(v0,v1,(f64)j/M)));
    return bb;
}
Vec3 Surface::project(Vec3 pt) const {
    // seeded Newton projection in UV space
    f64 u0,u1,v0,v1; domain(u0,u1,v0,v1);
    f64 bestD = 1e300, bu=(u0+u1)*0.5, bv=(v0+v1)*0.5;
    // seed grid
    for (int i = 0; i <= 12; ++i)
        for (int j = 0; j <= 12; ++j) {
            f64 uu=lerp(u0,u1,i/12.0), vv=lerp(v0,v1,j/12.0);
            f64 d=(eval(uu,vv)-pt).lensq();
            if (d<bestD){bestD=d;bu=uu;bv=vv;}
        }
    // Newton refine
    for (int it = 0; it < 24; ++it) {
        Vec3 s,su,sv;
        if (auto* nb = dynamic_cast<const NurbsSurface*>(this)) {
            nb->evalDers(bu,bv,&s,&su,&sv);
        } else {
            const f64 h=1e-7;
            s=eval(bu,bv); su=(eval(bu+h,bv)-eval(bu-h,bv))*(0.5/h);
            sv=(eval(bu,bv+h)-eval(bu,bv-h))*(0.5/h);
            if(auto* cy=dynamic_cast<const CylinderSurface*>(this)){
                f64 th=bu*K_2PI;
                su=(-cy->refX*std::sin(th)+cy->refY*std::cos(th))*cy->radius*K_2PI;
            }
        }
        Vec3 r=s-pt;
        f64 gu=r.dot(su), gv=r.dot(sv);
        f64 a=su.dot(su), b=su.dot(sv), cv=sv.dot(sv);
        f64 det=a*cv-b*b;
        if(std::fabs(det)<K_EPS) break;
        f64 du=(-cv*gu+b*gv)/det, dv=(b*gu-a*gv)/det;
        bu=clamp(bu+du,u0,u1); bv=clamp(bv+dv,v0,v1);
        if(std::fabs(du)+std::fabs(dv)<1e-12) break;
    }
    return eval(bu,bv);
}

// ---------------------------------------------------------------------------
// NURBS surface ------------------------------------------------------------------
Vec3 NurbsSurface::eval(f64 u, f64 v) const {
    int spanU = nurbs_find_span(nu-1, pu, u, U);
    int spanV = nurbs_find_span(nv-1, pv, v, V);
    std::vector<f64> Nu(pu+1), Nv(pv+1);
    nurbs_basis(spanU, u, pu, U, Nu.data());
    nurbs_basis(spanV, v, pv, V, Nv.data());
    // homogeneous intermediate
    Vec4 Sw[pu+1];
    for (int i = 0; i <= pu; ++i) {
        Vec4 tmp;
        for (int j = 0; j <= pv; ++j) {
            int ii = spanU-pu+i, jj = spanV-pv+j;
            f64 w = wgt(ii,jj);
            const Vec3& Pij = cpt(ii,jj);
            f64 b = Nv[j]*w;
            tmp.x += Pij.x*b; tmp.y += Pij.y*b; tmp.z += Pij.z*b; tmp.w += b;
        }
        Sw[i] = tmp;
    }
    Vec4 r;
    for (int i = 0; i <= pu; ++i) {
        r.x += Sw[i].x*Nu[i]; r.y += Sw[i].y*Nu[i]; r.z += Sw[i].z*Nu[i]; r.w += Sw[i].w*Nu[i];
    }
    return {r.x/r.w, r.y/r.w, r.z/r.w};
}
void NurbsSurface::evalDers(f64 u, f64 v, Vec3* S, Vec3* Su, Vec3* Sv) const {
    // numeric partials are adequate here; exact derivative basis is used by
    // the curve path. (Documented tradeoff — see docs/ALGORITHMS.md)
    const f64 h = 1e-6;
    f64 u0=umin(),u1=umax(),v0=vmin(),v1=vmax();
    f64 ua=tmin(tmax(u,u0+h),u1-h), va=tmin(tmax(v,v0+h),v1-h);
    *S  = eval(ua,va);
    *Su = (eval(ua+h,va)-eval(ua-h,va))*(0.5/h);
    *Sv = (eval(ua,va+h)-eval(ua,va-h))*(0.5/h);
}
f64 NurbsSurface::curvatureEstimate(f64 u, f64 v) const {
    Vec3 s,su,sv; evalDers(u,v,&s,&su,&sv);
    const f64 h=1e-5;
    Vec3 suu=(eval(clamp(u+h,umin(),umax()),v)-2.0*s+eval(clamp(u-h,umin(),umax()),v))/(h*h);
    Vec3 svv=(eval(u,clamp(v+h,vmin(),vmax()))-2.0*s+eval(u,clamp(v-h,vmin(),vmax())))/(h*h);
    f64 ku=suu.len()/tmax(su.lensq(),1e-8);
    f64 kv=svv.len()/tmax(sv.lensq(),1e-8);
    return tmax(ku,kv);
}
std::unique_ptr<Surface> NurbsSurface::transformed(const Mat4& M) const {
    auto c = std::make_unique<NurbsSurface>(*this);
    for (auto& p : c->P) p = M.transformPoint(p);
    return c;
}

} // namespace ark
