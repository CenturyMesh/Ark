// ============================================================================
//  ARK-Point :: ark/core.hpp
//  Core numeric foundation: SIMD-dispatched linear algebra, thread pool,
//  deterministic RNG. Kernel math is f64 (CAD-grade); render paths use f32
//  use packed f32. Historical assembly is retained for future parity profiling.
// ===========================================================================
#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <algorithm>
#include <memory>

namespace ark {

using f32 = float;  using f64 = double;
using i32 = int32_t; using i64 = int64_t;
using u8 = uint8_t; using u16 = uint16_t;
using u32 = uint32_t; using u64 = uint64_t;

// ---------------------------------------------------------------------------
// CPU capability detection (populated once at startup by cpu_detect()).
extern bool g_has_avx2;   // asm kernels require AVX2+FMA
extern bool g_has_fma;

void cpu_detect();

// ---------------------------------------------------------------------------
// Scalar helpers -------------------------------------------------------------
constexpr f64 K_EPS   = 1e-12;
constexpr f64 K_TOL   = 1e-9;      // geometric tolerance (model units)
constexpr f64 K_PI    = 3.14159265358979323846;
constexpr f64 K_2PI   = 6.28318530717958647692;
constexpr f64 K_HALF_PI = 1.57079632679489661923;

template<class T> constexpr T tmin(T a, T b) { return a < b ? a : b; }
template<class T> constexpr T tmax(T a, T b) { return a > b ? a : b; }
template<class T> constexpr T clamp(T v, T lo, T hi) { return v<lo?lo:(v>hi?hi:v); }
inline f64 lerp(f64 a, f64 b, f64 t) { return a + (b-a)*t; }
inline bool feq(f64 a, f64 b, f64 eps = K_TOL) { return std::fabs(a-b) <= eps; }
inline f64 deg2rad(f64 d){ return d * (K_PI/180.0); }
inline f64 rad2deg(f64 r){ return r * (180.0/K_PI); }

// ---------------------------------------------------------------------------
// Vec2 -----------------------------------------------------------------------
struct Vec2 {
    f64 x=0, y=0;
    constexpr Vec2() = default;
    constexpr Vec2(f64 x_, f64 y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x+o.x, y+o.y}; }
    Vec2 operator-(Vec2 o) const { return {x-o.x, y-o.y}; }
    Vec2 operator*(f64 s)  const { return {x*s, y*s}; }
    Vec2 operator/(f64 s)  const { return {x/s, y/s}; }
    Vec2 operator-() const { return {-x,-y}; }
    Vec2& operator+=(Vec2 o) { x+=o.x; y+=o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x-=o.x; y-=o.y; return *this; }
    Vec2& operator*=(f64 s) { x*=s; y*=s; return *this; }
    Vec2& operator/=(f64 s) { x/=s; y/=s; return *this; }
    f64 dot(Vec2 o) const { return x*o.x + y*o.y; }
    f64 cross(Vec2 o) const { return x*o.y - y*o.x; }
    f64 lensq() const { return x*x + y*y; }
    f64 len() const { return std::sqrt(lensq()); }
    Vec2 normalized() const { f64 l=len(); return l>K_EPS ? Vec2{x/l,y/l} : Vec2{0,0}; }
    bool approx(Vec2 o, f64 eps=K_TOL) const { return std::fabs(x-o.x)<eps && std::fabs(y-o.y)<eps; }
};

// ---------------------------------------------------------------------------
// Vec3 / Vec4 -----------------------------------------------------------------
struct alignas(32) Vec3 {
    f64 x=0, y=0, z=0;
    constexpr Vec3() = default;
    constexpr Vec3(f64 x_, f64 y_, f64 z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(Vec3 o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3 operator-(Vec3 o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3 operator*(f64 s)  const { return {x*s, y*s, z*s}; }
    Vec3 operator/(f64 s)  const { return {x/s, y/s, z/s}; }
    Vec3 operator-() const { return {-x,-y,-z}; }
    Vec3& operator+=(Vec3 o){ x+=o.x;y+=o.y;z+=o.z; return *this; }
    Vec3& operator-=(Vec3 o){ x-=o.x;y-=o.y;z-=o.z; return *this; }
    Vec3& operator*=(f64 s){ x*=s;y*=s;z*=s; return *this; }
    bool operator==(Vec3 o) const { return x==o.x && y==o.y && z==o.z; }
    f64 dot(Vec3 o) const { return x*o.x+y*o.y+z*o.z; }
    Vec3 cross(Vec3 o) const {
        return { y*o.z - z*o.y, z*o.x - x*o.z, x*o.y - y*o.x };
    }
    f64 lensq() const { return x*x+y*y+z*z; }
    f64 len()   const { return std::sqrt(lensq()); }
    Vec3 normalized() const { f64 l=len(); return l>K_EPS ? (*this)*(1.0/l) : Vec3{}; }
    bool approx(Vec3 o, f64 eps=K_TOL) const {
        return std::fabs(x-o.x)<eps && std::fabs(y-o.y)<eps && std::fabs(z-o.z)<eps;
    }
    static Vec3 lerpV(Vec3 a, Vec3 b, f64 t){ return a + (b-a)*t; }
};
inline Vec3 operator*(f64 s, Vec3 v){ return v*s; }

struct Vec4 {
    f64 x=0,y=0,z=0,w=0;
    constexpr Vec4() = default;
    constexpr Vec4(f64 x_,f64 y_,f64 z_,f64 w_) : x(x_),y(y_),z(z_),w(w_) {}
};

// ---------------------------------------------------------------------------
// Mat4 (row-major storage; transform() uses row-vector convention v*M? No:
// we use column-vector convention: p' = M*p, m[col*4+row]) --------------------
struct alignas(32) Mat4 {
    // column-major: m[c*4+r]
    f64 m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

    static Mat4 identity(){ return Mat4{}; }

    static Mat4 translation(Vec3 t){
        Mat4 r{}; r.m[12]=t.x; r.m[13]=t.y; r.m[14]=t.z; return r;
    }
    static Mat4 scale(f64 sx,f64 sy,f64 sz){
        Mat4 r{}; r.m[0]=sx; r.m[5]=sy; r.m[10]=sz; return r;
    }
    static Mat4 rotX(f64 a){
        f64 c=std::cos(a),s=std::sin(a); Mat4 r{};
        r.m[5]=c;  r.m[6]=s;  r.m[9]=-s; r.m[10]=c; return r;
    }
    static Mat4 rotY(f64 a){
        f64 c=std::cos(a),s=std::sin(a); Mat4 r{};
        r.m[0]=c;  r.m[2]=-s; r.m[8]=s;  r.m[10]=c; return r;
    }
    static Mat4 rotZ(f64 a){
        f64 c=std::cos(a),s=std::sin(a); Mat4 r{};
        r.m[0]=c;  r.m[1]=s;  r.m[4]=-s; r.m[5]=c; return r;
    }
    static Mat4 axisAngle(Vec3 axis, f64 a){
        axis = axis.normalized();
        f64 c=std::cos(a), s=std::sin(a), t=1-c;
        f64 x=axis.x, y=axis.y, z=axis.z;
        Mat4 r{};
        r.m[0]=t*x*x+c;    r.m[1]=t*x*y+s*z;  r.m[2]=t*x*z-s*y;
        r.m[4]=t*x*y-s*z;  r.m[5]=t*y*y+c;    r.m[6]=t*y*z+s*x;
        r.m[8]=t*x*z+s*y;  r.m[9]=t*y*z-s*x;  r.m[10]=t*z*z+c;
        return r;
    }
    // right-handed look-at (view matrix)
    static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up){
        Vec3 f=(center-eye).normalized();
        Vec3 s=f.cross(up).normalized();
        Vec3 u=s.cross(f);
        Mat4 r{};
        r.m[0]=s.x;  r.m[4]=s.y;  r.m[8]=s.z;
        r.m[1]=u.x;  r.m[5]=u.y;  r.m[9]=u.z;
        r.m[2]=-f.x; r.m[6]=-f.y; r.m[10]=-f.z;
        r.m[12]=-s.dot(eye); r.m[13]=-u.dot(eye); r.m[14]=f.dot(eye);
        return r;
    }
    static Mat4 perspective(f64 fovY, f64 aspect, f64 zn, f64 zf){
        f64 f=1.0/std::tan(fovY/2.0);
        Mat4 r{};
        r.m[0]=f/aspect; r.m[5]=f;
        r.m[10]=(zf+zn)/(zn-zf); r.m[11]=-1;
        r.m[14]=2.0*zf*zn/(zn-zf); r.m[15]=0;
        return r;
    }
    static Mat4 ortho(f64 l,f64 r_,f64 b,f64 t,f64 zn,f64 zf){
        Mat4 o{};
        o.m[0]=2.0/(r_-l); o.m[5]=2.0/(t-b);
        o.m[10]=-2.0/(zf-zn);
        o.m[12]=-(r_+l)/(r_-l); o.m[13]=-(t+b)/(t-b); o.m[14]=-(zf+zn)/(zf-zn);
        return o;
    }

    Mat4 operator*(const Mat4& b) const;
    Vec3 transformPoint(Vec3 p) const {
        return { m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],
                 m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                 m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14] };
    }
    Vec3 transformDir(Vec3 d) const {
        return { m[0]*d.x+m[4]*d.y+m[8]*d.z,
                 m[1]*d.x+m[5]*d.y+m[9]*d.z,
                 m[2]*d.x+m[6]*d.y+m[10]*d.z };
    }
    Mat4 transposed() const {
        Mat4 r{};
        for(int c=0;c<4;c++) for(int rr=0;rr<4;rr++) r.m[rr*4+c]=m[c*4+rr];
        return r;
    }
    f64 determinant() const;
    Mat4 inverted() const;   // full inverse; singular => identity
};

// ---------------------------------------------------------------------------
// Quat ------------------------------------------------------------------------
struct Quat {
    f64 w=1,x=0,y=0,z=0;
    Quat conjugated() const { return {w,-x,-y,-z}; }
    Quat operator*(const Quat& q) const {
        return { w*q.w - x*q.x - y*q.y - z*q.z,
                 w*q.x + x*q.w + y*q.z - z*q.y,
                 w*q.y - x*q.z + y*q.w + z*q.x,
                 w*q.z + x*q.y - y*q.x + z*q.w };
    }
    Vec3 rotate(Vec3 v) const {
        Quat p{0,v.x,v.y,v.z}, c=conjugated();
        Quat r=(*this)*p*c;
        return {r.x,r.y,r.z};
    }
    void normalize(){
        f64 l=std::sqrt(w*w+x*x+y*y+z*z);
        if(l>K_EPS){ w/=l;x/=l;y/=l;z/=l; } else { w=1;x=y=z=0; }
    }
    static Quat fromAxisAngle(Vec3 ax, f64 a){
        ax=ax.normalized(); f64 h=a*0.5, s=std::sin(h);
        return {std::cos(h), ax.x*s, ax.y*s, ax.z*s};
    }
};

// ---------------------------------------------------------------------------
// AABB -------------------------------------------------------------------------
struct AABB {
    Vec3 mn{ 1e300, 1e300, 1e300};
    Vec3 mx{-1e300,-1e300,-1e300};
    bool valid() const { return mn.x<=mx.x && mn.y<=mx.y && mn.z<=mx.z; }
    void grow(Vec3 p){ mn.x=tmin(mn.x,p.x); mn.y=tmin(mn.y,p.y); mn.z=tmin(mn.z,p.z);
                       mx.x=tmax(mx.x,p.x); mx.y=tmax(mx.y,p.y); mx.z=tmax(mx.z,p.z); }
    void grow(const AABB& b){ if(b.valid()){ grow(b.mn); grow(b.mx);} }
    Vec3 center() const { return (mn+mx)*0.5; }
    Vec3 extent() const { return (mx-mn); }
    f64 diagonal() const { return extent().len(); }
    AABB transformed(const Mat4& M) const {
        if(!valid()) return {};
        AABB r; Vec3 e=extent(), c=center();
        for(int ix=0;ix<2;ix++)for(int iy=0;iy<2;iy++)for(int iz=0;iz<2;iz++){
            Vec3 corner(c.x+(ix? e.x/2 : -e.x/2),
                        c.y+(iy? e.y/2 : -e.y/2),
                        c.z+(iz? e.z/2 : -e.z/2));
            r.grow(M.transformPoint(corner));
        }
        return r;
    }
};

// Ray ----------------------------------------------------------------------------
struct Ray { Vec3 o, d; };

// Infinite plane n.p = d ----------------------------------------------------------
struct Plane {
    Vec3 n{0,1,0}; f64 d=0;
    static Plane fromPointNormal(Vec3 p, Vec3 n_){ Plane pl; pl.n=n_.normalized(); pl.d=pl.n.dot(p); return pl; }
    f64 dist(Vec3 p) const { return n.dot(p)-d; }
    bool intersectLine(Vec3 a, Vec3 b, f64& t) const {
        f64 den = n.dot(b-a);
        if(std::fabs(den) < K_EPS) return false;
        t = (d - n.dot(a)) / den;
        return true;
    }
};

// ---------------------------------------------------------------------------
// Deterministic RNG (xorshift64*) ----------------------------------------------
struct Rng {
    u64 s;
    explicit Rng(u64 seed=0x9E3779B97F4A7C15ull):s(seed?seed:88172645463325252ull){}
    u64 next(){ s^=s>>12; s^=s<<25; s^=s>>27; return s*2685821657736338717ull; }
    f64 unit(){ return (f64)(next()>>11) * (1.0/9007199254740992.0); }
    f64 range(f64 a,f64 b){ return a+(b-a)*unit(); }
};

// ---------------------------------------------------------------------------
// Parallelism: global work-stealing-free task pool sized to hardware threads.
// submit(fn) enqueues; waitAll drains. Nested submits allowed but discouraged.
// ---------------------------------------------------------------------------
class ThreadPool {
public:
    static ThreadPool& get();
    void submit(std::function<void()> fn);
    void waitAll();
    unsigned workers() const { return (unsigned)workers_.size(); }
private:
    ThreadPool();
    ~ThreadPool();
    std::vector<std::thread> workers_;
    std::vector<std::function<void()>> queue_;
    std::mutex mu_;
    std::condition_variable cv_work_, cv_idle_;
    std::atomic<i64> pending_{0};
    std::atomic<bool> stop_{false};
};

// parallel_for over [begin,end) split in ~4 chunks per worker
void parallel_for(i64 begin, i64 end, const std::function<void(i64,i64)>& body);
bool setOuterParallelRegion(bool active);

// ---------------------------------------------------------------------------
// Portable packed-array kernels. Historical assembly is not enabled by default.
// ---------------------------------------------------------------------------
extern "C" {
void ark_mat4_mul_f32(f32* out, const f32* a, const f32* b);            // 4x4 row-major
void ark_transform_points_f32(const f32* mat, const f32* in, f32* out, u64 n); // p'=M*p, w=1
f64  ark_dot_batch_f64(const f64* a, const f64* b, u64 n);              // sum a[i].dot b[i] triplets
}

void mat4_mul_f32(f32* out, const f32* a, const f32* b);
void transform_points_f32(const Mat4& M, const Vec3* in, Vec3* out, size_t n);

} // namespace ark
