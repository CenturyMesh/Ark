// ARK-Point :: core.cpp — math impl, CPU dispatch, thread pool.
#include "ark/core.hpp"
#include <cstdlib>
#include <exception>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <intrin.h>
#endif

namespace ark {

bool g_has_avx2 = false;
bool g_has_fma  = false;

void cpu_detect() {
#if defined(_MSC_VER)
    int regs[4];
    __cpuid(regs, 0);
    int maxLeaf = regs[0];
    if (maxLeaf >= 1) {
        __cpuid(regs, 1);
        g_has_fma = (regs[2] & (1<<12)) != 0;
        // XSAVE + OSXSAVE needed to use AVX state
        bool xsave = (regs[2] & (1<<27)) != 0 && (_xgetbv(0) & 0x6) == 0x6;
        if (maxLeaf >= 7) {
            int ext[4]; __cpuidex(ext, 7, 0);
            g_has_avx2 = xsave && (ext[1] & (1<<5)) != 0;
        }
    }
#elif defined(__i386__) || defined(__x86_64__)
    u32 a,b,c,d;
    auto cpuid=[&](u32 leaf,u32 sub){ a=b=c=d=0;
        #if defined(__GNUC__)
        __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(leaf),"c"(sub));
        #endif
    };
    cpuid(0,0);
    u32 maxLeaf=a; bool osx=false;
    if(maxLeaf>=1){ cpuid(1,0);
        g_has_fma = (c&(1u<<12))!=0;
        #if defined(__GNUC__)
        if(c&(1u<<27)){ u32 eax_,edx_; __asm__ volatile("xgetbv":"=a"(eax_),"=d"(edx_):"c"(0)); osx=(eax_&0x6)==0x6; }
        #endif
        if(maxLeaf>=7){ cpuid(7,0); g_has_avx2 = osx && ((b&(1u<<5))!=0); }
    }
#endif
}

// ---------------------------------------------------------------------------
// Mat4 ------------------------------------------------------------------------
Mat4 Mat4::operator*(const Mat4& b) const {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            f64 s = 0;
            for (int k = 0; k < 4; ++k) s += m[k*4+rr] * b.m[c*4+k];
            r.m[c*4+rr] = s;
        }
    return r;
}
f64 Mat4::determinant() const {
    const f64* a=m;
    f64 c0 = a[5]*(a[10]*a[15]-a[11]*a[14]) - a[9]*(a[6]*a[15]-a[7]*a[14]) + a[13]*(a[6]*a[11]-a[7]*a[10]);
    f64 c4 = a[1]*(a[10]*a[15]-a[11]*a[14]) - a[9]*(a[2]*a[15]-a[3]*a[14]) + a[13]*(a[2]*a[11]-a[3]*a[10]);
    f64 c8 = a[1]*(a[6]*a[15]-a[7]*a[14]) - a[5]*(a[2]*a[15]-a[3]*a[14]) + a[13]*(a[2]*a[7] -a[3]*a[6]);
    f64 c12= a[1]*(a[6]*a[11]-a[7]*a[10]) - a[5]*(a[2]*a[11]-a[3]*a[10]) + a[9] *(a[2]*a[7] -a[3]*a[6]);
    return a[0]*c0 - a[4]*c4 + a[8]*c8 - a[12]*c12;
}
Mat4 Mat4::inverted() const {
    f64 det = determinant();
    if (std::fabs(det) < K_EPS) return identity();
    Mat4 inv{};
    inv.m[0]= (m[5]*(m[10]*m[15]-m[11]*m[14]) - m[9]*(m[6]*m[15]-m[7]*m[14]) + m[13]*(m[6]*m[11]-m[7]*m[10]))/det;
    inv.m[4]=-(m[4]*(m[10]*m[15]-m[11]*m[14]) - m[8]*(m[6]*m[15]-m[7]*m[14]) + m[12]*(m[6]*m[11]-m[7]*m[10]))/det;
    inv.m[8]= (m[4]*(m[9]*m[15]-m[11]*m[13]) - m[8]*(m[5]*m[15]-m[7]*m[13]) + m[12]*(m[5]*m[11]-m[7]*m[9]))/det;
    inv.m[12]=-(m[4]*(m[9]*m[14]-m[10]*m[13]) - m[8]*(m[5]*m[14]-m[6]*m[13]) + m[12]*(m[5]*m[10]-m[6]*m[9]))/det;
    inv.m[1]=-(m[1]*(m[10]*m[15]-m[11]*m[14]) - m[9]*(m[2]*m[15]-m[3]*m[14]) + m[13]*(m[2]*m[11]-m[3]*m[10]))/det;
    inv.m[5]= (m[0]*(m[10]*m[15]-m[11]*m[14]) - m[8]*(m[2]*m[15]-m[3]*m[14]) + m[12]*(m[2]*m[11]-m[3]*m[10]))/det;
    inv.m[9]=-(m[0]*(m[9]*m[15]-m[11]*m[13]) - m[8]*(m[1]*m[15]-m[3]*m[13]) + m[12]*(m[1]*m[11]-m[3]*m[9]))/det;
    inv.m[13]= (m[0]*(m[9]*m[14]-m[10]*m[13]) - m[8]*(m[1]*m[14]-m[2]*m[13]) + m[12]*(m[1]*m[10]-m[2]*m[9]))/det;
    inv.m[2]= (m[1]*(m[6]*m[15]-m[7]*m[14]) - m[5]*(m[2]*m[15]-m[3]*m[14]) + m[13]*(m[2]*m[7]-m[3]*m[6]))/det;
    inv.m[6]=-(m[0]*(m[6]*m[15]-m[7]*m[14]) - m[4]*(m[2]*m[15]-m[3]*m[14]) + m[12]*(m[2]*m[7]-m[3]*m[6]))/det;
    inv.m[10]= (m[0]*(m[5]*m[15]-m[7]*m[13]) - m[4]*(m[1]*m[15]-m[3]*m[13]) + m[12]*(m[1]*m[7]-m[3]*m[5]))/det;
    inv.m[14]=-(m[0]*(m[5]*m[14]-m[6]*m[13]) - m[4]*(m[1]*m[14]-m[2]*m[13]) + m[12]*(m[1]*m[6]-m[2]*m[5]))/det;
    inv.m[3]=-(m[1]*(m[6]*m[11]-m[7]*m[10]) - m[5]*(m[2]*m[11]-m[3]*m[10]) + m[9]*(m[2]*m[7]-m[3]*m[6]))/det;
    inv.m[7]= (m[0]*(m[6]*m[11]-m[7]*m[10]) - m[4]*(m[2]*m[11]-m[3]*m[10]) + m[8]*(m[2]*m[7]-m[3]*m[6]))/det;
    inv.m[11]=-(m[0]*(m[5]*m[11]-m[7]*m[9]) - m[4]*(m[1]*m[11]-m[3]*m[9]) + m[8]*(m[1]*m[7]-m[3]*m[5]))/det;
    inv.m[15]= (m[0]*(m[5]*m[10]-m[6]*m[9]) - m[4]*(m[1]*m[10]-m[2]*m[9]) + m[8]*(m[1]*m[6]-m[2]*m[5]))/det;
    return inv;
}

// ---------------------------------------------------------------------------
// SIMD-dispatched kernels ------------------------------------------------------
void mat4_mul_f32(f32* out, const f32* a, const f32* b) {
    ark_mat4_mul_f32(out, a, b);
}
void transform_points_f32(const Mat4& M, const Vec3* in, Vec3* out, size_t n) {
    if (!n) return;
    // Vec3 is aligned f64; never reinterpret it as packed f32.
    for (size_t i = 0; i < n; ++i) out[i] = M.transformPoint(in[i]);
}

// ---------------------------------------------------------------------------
// ThreadPool -------------------------------------------------------------------
static thread_local bool inCoreWorker=false;
static thread_local bool outerParallelRegion=false;
bool setOuterParallelRegion(bool active){bool previous=outerParallelRegion;outerParallelRegion=active;return previous;}
ThreadPool& ThreadPool::get() {
    static ThreadPool tp;
    return tp;
}
ThreadPool::ThreadPool() {
    unsigned n = std::thread::hardware_concurrency();
    if (!n) n = 4;
    const unsigned hardware=n;n=std::min(n,32u);if(const auto setting=std::getenv("ARK_CPU_THREADS")){char* end=nullptr;long requested=std::strtol(setting,&end,10);if(end&&!*end&&requested>=1&&requested<=64)n=std::min(hardware,(unsigned)requested);}
    workers_.reserve(n);
    for (unsigned i = 0; i < n; ++i)
        workers_.emplace_back([this]() {
            inCoreWorker=true;
            for (;;) {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lk(mu_);
                    cv_work_.wait(lk, [&]{ return stop_ || !queue_.empty(); });
                    if (stop_ && queue_.empty()) return;
                    job = std::move(queue_.back());
                    queue_.pop_back();
                }
                job();
                if (--pending_ == 0) { std::lock_guard<std::mutex> lk(mu_); cv_idle_.notify_all(); }
            }
        });
}
ThreadPool::~ThreadPool() {
    stop_ = true;
    cv_work_.notify_all();
    for (auto& t : workers_) if (t.joinable()) t.join();
}
void ThreadPool::submit(std::function<void()> fn) {
    ++pending_;
    { std::lock_guard<std::mutex> lk(mu_); queue_.push_back(std::move(fn)); }
    cv_work_.notify_one();
}
void ThreadPool::waitAll() {
    std::unique_lock<std::mutex> lk(mu_);
    cv_idle_.wait(lk, [&]{ return pending_ == 0; });
}
void parallel_for(i64 begin, i64 end, const std::function<void(i64,i64)>& body) {
    if (begin >= end) return;
    if(inCoreWorker||outerParallelRegion||end-begin<64){body(begin,end);return;}
    i64 total = end - begin;
    auto& tp = ThreadPool::get();
    const i64 chunks=std::min<i64>((total+63)/64,(i64)tp.workers()*4);
    const i64 chunk=(total+chunks-1)/chunks;
    std::atomic<i64> remaining((i64)((total + chunk - 1)/chunk));std::mutex mutex;std::condition_variable done;std::exception_ptr failure;
    for (i64 s = begin; s < end; s += chunk) {
        i64 e = tmin(s + chunk, end);
        tp.submit([s,e,&body,&remaining,&mutex,&done,&failure]() {
            try{body(s,e);}catch(...){std::lock_guard<std::mutex> guard(mutex);if(!failure)failure=std::current_exception();}
            {std::lock_guard<std::mutex> guard(mutex);--remaining;done.notify_one();}
        });
    }
    std::unique_lock<std::mutex> lock(mutex);done.wait(lock,[&]{return remaining.load()==0;});if(failure)std::rethrow_exception(failure);
}

} // namespace ark
