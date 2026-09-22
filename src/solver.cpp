// ARK-Point :: solver.cpp â€” geometric constraint solver for sketches.
//
// Method: parametrize all sketch DOFs into x; each constraint contributes a
// scalar residual r_i(x). Solve  min ||r(x)||^2  with damped least squares
// (Levenberg-Marquardt), numeric central-difference Jacobian, backtracking on
// failure. Small dense systems solved by Gaussian elimination with partial
// pivoting. This is the standard approach in production CAD solvers
// (cf. "A Geometric Constraint Solver" v5, and Siemens/DS literature).
#include "ark/model.hpp"
#include <cmath>

namespace ark {

namespace {

struct Problem {
    const Sketch& sk;
    std::vector<f64> x;
    std::vector<size_t> free;
    int n=0;

    explicit Problem(const Sketch& s):sk(s){
        for(auto&e:s.ents)for(size_t i=0;i<e.dof.size();++i){
            if(i>=e.expressions.size()||e.expressions[i].empty())free.push_back(x.size());
            x.push_back(e.dof[i]);
        }
        n=(int)free.size();
    }

    void writeBack(Sketch& s) const {
        size_t k=0;
        for(auto&e:s.ents) for(f64& d:e.dof){ if(k<x.size()) d=x[k++]; }
    }

    // point accessor: entity idx + local point slot (line: 0=start 1=end)
    Vec2 point(int ent,int slot) const {
        const auto& e=sk.ents[(size_t)ent];
        switch(e.kind){
            case SketchEnt::Pnt: {
                size_t o=offsetOf(ent);
                return {x[o],x[o+1]};
            }
            case SketchEnt::Line:
            case SketchEnt::Spline: {
                size_t o=offsetOf(ent);
                return { x[o+2*slot], x[o+2*slot+1] };
            }
            case SketchEnt::Circle: {
                size_t o=offsetOf(ent);
                return {x[o],x[o+1]};
            }
        }
        return {};
    }
    f64 radius(int ent) const {
        size_t o=offsetOf(ent);
        return x[o+2];
    }
    size_t offsetOf(int ent) const {
        size_t o=0;
        for(int i=0;i<ent;++i) o+=sk.ents[(size_t)i].dof.size();
        return o;
    }

    // residual vector
    void residuals(std::vector<f64>& r) const {
        r.clear();
        for(auto&c:sk.cons){
            switch(c.type){
                case SketchCon::FixedX: r.push_back(point(c.a,c.pa).x - c.val); break;
                case SketchCon::FixedY: r.push_back(point(c.a,c.pa).y - c.val); break;
                case SketchCon::Fixed: {
                    Vec2 p=point(c.a,c.pa);
                    r.push_back(p.x - c.val);
                    r.push_back(p.y - c.val2);
                    break;
                }
                case SketchCon::DistPP: {
                    Vec2 p=point(c.a,c.pa), q=point(c.b,c.pb);
                    r.push_back((p-q).len() - c.val);
                    break;
                }
                case SketchCon::Horizontal:{
                    Vec2 p=point(c.a,0),q=point(c.a,1);
                    r.push_back(p.y-q.y);
                    break;
                }
                case SketchCon::Vertical:{
                    Vec2 p=point(c.a,0),q=point(c.a,1);
                    r.push_back(p.x-q.x);
                    break;
                }
                case SketchCon::Parallel:{
                    Vec2 a0=point(c.a,0),a1=point(c.a,1);
                    Vec2 b0=point(c.b,0),b1=point(c.b,1);
                    Vec2 da=a1-a0, db=b1-b0;
                    f64 la=da.len(),lb=db.len();
                    if(la<1e-12||lb<1e-12){ r.push_back(0); break; }
                    da/=la; db/=lb;
                    r.push_back(da.cross(db));
                    break;
                }
                case SketchCon::Perpendicular:{
                    Vec2 a0=point(c.a,0),a1=point(c.a,1);
                    Vec2 b0=point(c.b,0),b1=point(c.b,1);
                    Vec2 da=a1-a0, db=b1-b0;
                    f64 la=da.len(),lb=db.len();
                    if(la<1e-12||lb<1e-12){ r.push_back(0); break; }
                    r.push_back(da.normalized().dot(db.normalized()));
                    break;
                }
                case SketchCon::Radius:
                    r.push_back(radius(c.a)-c.val);
                    break;
                case SketchCon::Coincident:{
                    Vec2 p=point(c.a,c.pa), q=point(c.b,c.pb);
                    r.push_back(p.x-q.x);
                    r.push_back(p.y-q.y);
                    break;
                }
                case SketchCon::AngleLL:{
                    Vec2 a0=point(c.a,0),a1=point(c.a,1);
                    Vec2 b0=point(c.b,0),b1=point(c.b,1);
                    f64 aa=std::atan2(a1.y-a0.y,a1.x-a0.x);
                    f64 ab=std::atan2(b1.y-b0.y,b1.x-b0.x);
                    r.push_back(std::remainder(aa-ab-c.val,K_2PI));
                    break;
                }
            }
        }
    }
};

bool solveDense(std::vector<f64>& A,std::vector<f64>& b,int n){
    for(int col=0;col<n;++col){
        int piv=col;
        f64 best=std::fabs(A[(size_t)col*n+col]);
        for(int rr=col+1;rr<n;++rr){
            f64 v=std::fabs(A[(size_t)rr*n+col]);
            if(v>best){best=v;piv=rr;}
        }
        if(best<1e-14) return false;
        if(piv!=col){
            for(int j=0;j<n;++j) std::swap(A[(size_t)col*n+j],A[(size_t)piv*n+j]);
            std::swap(b[(size_t)col],b[(size_t)piv]);
        }
        f64 inv=1.0/A[(size_t)col*n+col];
        for(int rr=col+1;rr<n;++rr){
            f64 m=A[(size_t)rr*n+col]*inv;
            if(m==0) continue;
            for(int j=col;j<n;++j) A[(size_t)rr*n+j]-=m*A[(size_t)col*n+j];
            b[(size_t)rr]-=m*b[(size_t)col];
        }
    }
    for(int rr=n-1;rr>=0;--rr){
        f64 s=b[(size_t)rr];
        for(int j=rr+1;j<n;++j) s-=A[(size_t)rr*n+j]*b[(size_t)j];
        b[(size_t)rr]=s/(std::fabs(A[(size_t)rr*n+rr])<1e-300?1e-300:A[(size_t)rr*n+rr]);
    }
    return true;
}

} // namespace

f64 solveSketch(Sketch& sk, int* itersOut, int maxIters, SketchDiagnostics* diagnostics) {
    Problem P(sk);
    int n=P.n;

    std::vector<f64> r,g(n),dx(n);
    static thread_local std::vector<std::vector<f64>> Jcols;

    auto buildJ = [&](std::vector<f64>& rOut)->size_t {
        // central-difference numeric Jacobian, one column per DOF
        P.residuals(rOut);
        size_t m=rOut.size();
        Jcols.assign((size_t)n,{});
        for(int j=0;j<n;++j){
            size_t slot=P.free[(size_t)j];
            f64 h=1e-7*(1.0+std::fabs(P.x[slot]));
            f64 old=P.x[slot];
            P.x[slot]=old+h; P.residuals(r); std::vector<f64> rp=r;
            P.x[slot]=old-h; P.residuals(r); std::vector<f64>& rm=r;
            P.x[slot]=old;
            m=rm.size();
            Jcols[(size_t)j].resize(m);
            for(size_t i=0;i<m;++i)
                Jcols[(size_t)j][i]=(rp[i]-rm[i])/(2*h);
        }
        return m;
    };

    auto cost=[&](){
        P.residuals(r);
        f64 s=0; for(f64 v:r) s+=v*v;
        return s;
    };

    f64 c=cost();
    f64 lambda=1e-3;
    int iters=0;
    for(; n>0 && iters<maxIters && c>1e-16 && std::isfinite(c); ++iters){
        std::vector<f64> r0;
        size_t m=buildJ(r0);

        // normal equations  H = J^T J ,  g = J^T r
        std::vector<f64> JtJ((size_t)n*n,0.0);
        g.assign((size_t)n,0.0);
        for(int a2=0;a2<n;++a2)
            for(int b=a2;b<n;++b){
                f64 s=0;
                const auto&ca=Jcols[(size_t)a2]; const auto&cb=Jcols[(size_t)b];
                for(size_t i=0;i<m;++i) s+=ca[i]*cb[i];
                JtJ[(size_t)a2*n+b]=JtJ[(size_t)b*n+a2]=s;
            }
        for(size_t i=0;i<m;++i)
            for(int j=0;j<n;++j)
                g[(size_t)j]+=Jcols[(size_t)j][i]*r0[i];

        bool improved=false;
        for(int damp=0;damp<8 && !improved;++damp){
            std::vector<f64> Hm=JtJ;
            // LM damping must ADD a floor, not scale: DOFs absent from every
            // constraint have JtJ diagonal exactly 0 and scaling keeps them
            // singular forever.
            for(int j=0;j<n;++j)
                Hm[(size_t)j*n+j]=Hm[(size_t)j*n+j]*(1.0+lambda)+1e-12*(1.0+c);
            dx=g;
            if(!solveDense(Hm,dx,n)) { lambda*=10; continue; }
            std::vector<f64> xn=P.x;
            for(int j=0;j<n;++j) xn[P.free[(size_t)j]]-=dx[(size_t)j];
            auto old=P.x; P.x=xn;
            f64 c2=cost();
            if(c2<c){
                lambda=tmax(lambda*0.4,1e-9);
                c=c2;
                improved=true;
            } else {
                P.x=old;
                lambda*=10;
            }
        }
        if(!improved) break;
    }
    P.writeBack(sk);
    if(diagnostics){
        diagnostics->variables=n;diagnostics->rank=0;
        std::vector<f64> finalResidual;size_t m=buildJ(finalResidual);
        // Local numerical rank, not a proof of global uniqueness. Normalize
        // constraint rows so angular and length residuals do not set the scale.
        std::vector<std::vector<double>> rows(m,std::vector<double>((size_t)n));
        for(size_t i=0;i<m;++i){double scale=0;for(int j=0;j<n;++j)scale=std::max(scale,std::abs(Jcols[(size_t)j][i]));
            for(int j=0;j<n;++j)rows[i][(size_t)j]=scale>1e-12?Jcols[(size_t)j][i]/scale:0;
        }
        size_t rank=0;for(int j=0;j<n&&rank<m;++j){size_t pivot=rank;
            for(size_t i=rank+1;i<m;++i)if(std::abs(rows[i][(size_t)j])>std::abs(rows[pivot][(size_t)j]))pivot=i;
            if(std::abs(rows[pivot][(size_t)j])<1e-7)continue;
            std::swap(rows[rank],rows[pivot]);double p=rows[rank][(size_t)j];
            for(size_t i=rank+1;i<m;++i){double factor=rows[i][(size_t)j]/p;for(int k=j;k<n;++k)rows[i][(size_t)k]-=factor*rows[rank][(size_t)k];}++rank;
        }diagnostics->rank=(int)rank;
    }
    if(itersOut)*itersOut=iters;
    P.residuals(r);
    f64 mx=0;
    for(f64 v:r){if(!std::isfinite(v))return INFINITY;mx=tmax(mx,std::fabs(v));}
    return mx;
}

} // namespace ark
