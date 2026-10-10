// ARK-Point :: solver.cpp â€” geometric constraint solver for sketches.
//
// Method: parametrize all sketch DOFs into x; each constraint contributes a
// scalar residual r_i(x). Solve  min ||r(x)||^2  with damped least squares
// (Levenberg-Marquardt), forward-mode differentiated Jacobian, backtracking on
// failure. Small dense systems solved by Gaussian elimination with partial
// pivoting. This is the standard approach in production CAD solvers
// (cf. "A Geometric Constraint Solver" v5, and Siemens/DS literature).
#include "ark/sketch.hpp"
#include <cmath>

namespace ark {

namespace {
// A directional dual number. Each residual only touches its own entities;
// derivatives do not perturb geometry or depend on a finite-difference step.
struct Dual {double v=0,d=0;Dual()=default;Dual(double value,double derivative=0):v(value),d(derivative){}};
Dual operator+(Dual a,Dual b){return {a.v+b.v,a.d+b.d};}
Dual operator-(Dual a,Dual b){return {a.v-b.v,a.d-b.d};}
Dual operator*(Dual a,Dual b){return {a.v*b.v,a.d*b.v+a.v*b.d};}
Dual operator/(Dual a,Dual b){return {a.v/b.v,(a.d*b.v-a.v*b.d)/(b.v*b.v)};}
Dual root(Dual a){double v=std::sqrt(a.v);return {v,v>1e-15?a.d/(2*v):0};}
Dual absolute(Dual a){return {std::abs(a.v),a.v>0?a.d:a.v<0?-a.d:0};}
Dual angle(Dual y,Dual x){double n=x.v*x.v+y.v*y.v;return {std::atan2(y.v,x.v),n>1e-24?(x.v*y.d-y.v*x.d)/n:0};}
struct DPoint {Dual x,y;DPoint operator-(DPoint b)const{return {x-b.x,y-b.y};}Dual length()const{return root(x*x+y*y);}Dual cross(DPoint b)const{return x*b.y-y*b.x;}Dual dot(DPoint b)const{return x*b.x+y*b.y;}DPoint divided(Dual n)const{return {x/n,y/n};}};

struct Problem {
    const Sketch& sk;
    std::vector<f64> x;
    std::vector<size_t> free;
    int n=0;
    std::vector<size_t> offsets;

    explicit Problem(const Sketch& s):sk(s){
        for(auto&e:s.ents){
            offsets.push_back(x.size());
            const bool driven=e.kind==SketchEnt::Project||e.kind==SketchEnt::Offset||e.arcRecipe.t==arkjson::Value::Obj;
            for(size_t i=0;i<e.dof.size();++i){
                if(!driven&&(i>=e.expressions.size()||e.expressions[i].empty()))free.push_back(x.size());
                x.push_back(e.dof[i]);
            }
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
            case SketchEnt::Circle:
            case SketchEnt::Arc: {
                size_t o=offsetOf(ent);
                return {x[o],x[o+1]};
            }
            case SketchEnt::Wire:
            case SketchEnt::Project:
            case SketchEnt::Offset: {
                if(e.segments.empty()){
                    if(e.dof.size()<4) return {};
                    size_t o=offsetOf(ent);
                    return {x[o+2*(size_t)slot],x[o+2*(size_t)slot+1]};
                }
                int index=sketchVertexDof(e,slot);
                size_t o=offsetOf(ent);
                if(index<0||o+(size_t)index+1>=x.size()) return {};
                return {x[o+(size_t)index],x[o+(size_t)index+1]};
            }
        }
        return {};
    }
    f64 radius(int ent) const {
        size_t o=offsetOf(ent);
        return x[o+2];
    }
    size_t offsetOf(int ent) const {
        return offsets[(size_t)ent];
    }
    std::vector<std::vector<double>> jacobian()const{
        std::vector<std::vector<double>> columns;
        for(size_t freeSlot:free){
            auto value=[&](size_t i){return Dual(x[i],i==freeSlot?1:0);};
            auto pt=[&](int ent,int slot){const auto& e=sk.ents[(size_t)ent];size_t o=offsetOf(ent);if(e.kind==SketchEnt::Line||e.kind==SketchEnt::Spline)o+=2*(size_t)slot;else if(e.kind==SketchEnt::Wire||e.kind==SketchEnt::Project||e.kind==SketchEnt::Offset)o+=e.segments.empty()?2*(size_t)slot:(size_t)sketchVertexDof(e,slot);return DPoint{value(o),value(o+1)};};
            auto rad=[&](int ent){return value(offsetOf(ent)+2);};
            std::vector<double> col;auto push=[&](Dual a){col.push_back(a.d);};
            for(const auto& c:sk.cons){switch(c.type){
                case SketchCon::FixedX:push(pt(c.a,c.pa).x-c.val);break;
                case SketchCon::FixedY:push(pt(c.a,c.pa).y-c.val);break;
                case SketchCon::Fixed:{auto p=pt(c.a,c.pa);push(p.x-c.val);push(p.y-c.val2);break;}
                case SketchCon::DistPP:push((pt(c.a,c.pa)-pt(c.b,c.pb)).length()-c.val);break;
                case SketchCon::Horizontal:push(pt(c.a,0).y-pt(c.a,1).y);break;
                case SketchCon::Vertical:push(pt(c.a,0).x-pt(c.a,1).x);break;
                case SketchCon::Parallel:
                case SketchCon::Perpendicular:{auto a=pt(c.a,1)-pt(c.a,0),b=pt(c.b,1)-pt(c.b,0);auto la=a.length(),lb=b.length();if(la.v<1e-12||lb.v<1e-12){push(0);break;}a=a.divided(la);b=b.divided(lb);push(c.type==SketchCon::Parallel?a.cross(b):a.dot(b));break;}
                case SketchCon::Radius:push(rad(c.a)-c.val);break;
                case SketchCon::Coincident:{auto delta=pt(c.a,c.pa)-pt(c.b,c.pb);push(delta.x);push(delta.y);break;}
                case SketchCon::AngleLL:{auto a=pt(c.a,1)-pt(c.a,0),b=pt(c.b,1)-pt(c.b,0);push(angle(a.y,a.x)-angle(b.y,b.x));break;}
                case SketchCon::Tangent:{bool aLine=sk.ents[(size_t)c.a].kind==SketchEnt::Line;int line=aLine?c.a:c.b,curve=aLine?c.b:c.a;auto p=pt(line,0),d=pt(line,1)-p,center=pt(curve,0);auto length=d.length();push((length.v<1e-12?(center-p).length():absolute((center-p).cross(d))/length)-rad(curve));break;}
            }}columns.push_back(std::move(col));
        }return columns;
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
                case SketchCon::Tangent:{
                    const auto& ea=sk.ents[(size_t)c.a];
                    const auto& eb=sk.ents[(size_t)c.b];
                    const bool aLine=ea.kind==SketchEnt::Line, bLine=eb.kind==SketchEnt::Line;
                    const bool aRound=ea.kind==SketchEnt::Circle||ea.kind==SketchEnt::Arc;
                    const bool bRound=eb.kind==SketchEnt::Circle||eb.kind==SketchEnt::Arc;
                    if(!((aLine&&bRound)||(bLine&&aRound))){ r.push_back(0); break; }
                    const int line=aLine?c.a:c.b, curve=aLine?c.b:c.a;
                    Vec2 p0=point(line,0), p1=point(line,1), center=point(curve,0);
                    Vec2 direction=p1-p0;
                    f64 span=direction.len();
                    f64 distance=span<1e-12?(center-p0).len():std::fabs((center-p0).cross(direction))/span;
                    r.push_back(distance-radius(curve));
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
bool solveIndependentBlocks(std::vector<double>& matrix,std::vector<double>& rhs,int n){
    std::vector<bool> seen((size_t)n,false);
    for(int seed=0;seed<n;++seed){if(seen[(size_t)seed])continue;std::vector<int> component{seed};seen[(size_t)seed]=true;
        for(size_t k=0;k<component.size();++k)for(int j=0;j<n;++j)if(!seen[(size_t)j]&&matrix[(size_t)component[k]*n+j]!=0){seen[(size_t)j]=true;component.push_back(j);}
        int size=(int)component.size();std::vector<double> block((size_t)size*size),b((size_t)size);
        for(int i=0;i<size;++i){b[(size_t)i]=rhs[(size_t)component[(size_t)i]];for(int j=0;j<size;++j)block[(size_t)i*size+j]=matrix[(size_t)component[(size_t)i]*n+component[(size_t)j]];}
        if(!solveDense(block,b,size))return false;for(int i=0;i<size;++i)rhs[(size_t)component[(size_t)i]]=b[(size_t)i];
    }return true;
}

// The same row ordering is used for each constraint's rank contribution. A
// second orthogonalization pass limits spurious rank gains from roundoff.
int appendIndependentRows(const std::vector<std::vector<f64>>& rows,
                          std::vector<std::vector<f64>>& basis){
    int gained=0;
    for(auto row:rows){
        f64 norm2=0;for(auto x:row)norm2+=x*x;
        const f64 original=std::sqrt(norm2);
        if(!std::isfinite(original)||original<1e-12)continue;
        for(int pass=0;pass<2;++pass)for(const auto& b:basis){
            f64 dot=0;for(size_t j=0;j<row.size();++j)dot+=row[j]*b[j];
            for(size_t j=0;j<row.size();++j)row[j]-=dot*b[j];
        }
        norm2=0;for(auto x:row)norm2+=x*x;
        const f64 remaining=std::sqrt(norm2);
        if(remaining<=original*1e-7)continue;
        for(auto& x:row)x/=remaining;
        basis.push_back(std::move(row));++gained;
    }
    return gained;
}

size_t residualCount(SketchCon::Type type){
    return type==SketchCon::Fixed||type==SketchCon::Coincident?2u:1u;
}

std::vector<std::vector<f64>> normalizedJacobianRows(Problem& p,const std::vector<f64>& residual){
    std::vector<std::vector<f64>> rows(residual.size(),std::vector<f64>((size_t)p.n,0));
    const auto columns=p.jacobian();
    for(int j=0;j<p.n;++j)for(size_t i=0;i<residual.size();++i)rows[i][(size_t)j]=columns[(size_t)j][i];
    for(auto& row:rows){
        f64 scale=0;for(auto v:row)scale=std::max(scale,std::abs(v));
        if(std::isfinite(scale)&&scale>1e-12)for(auto& v:row)v/=scale;
        else std::fill(row.begin(),row.end(),0.);
    }
    return rows;
}

} // namespace

SketchLinearization linearizeSketch(const Sketch& sketch){Problem p(sketch);SketchLinearization out;p.residuals(out.residuals);out.freeSlots=p.free;out.jacobianColumns=p.jacobian();return out;}

f64 solveSketch(Sketch& sk, int* itersOut, int maxIters, SketchDiagnostics* diagnostics) {
    Problem P(sk);
    int n=P.n;

    std::vector<f64> r,g(n),dx(n);
    static thread_local std::vector<std::vector<f64>> Jcols;

    auto buildJ = [&](std::vector<f64>& rOut)->size_t {
        P.residuals(rOut);
        size_t m=rOut.size();
        Jcols=P.jacobian();
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
        for(size_t i=0;i<m;++i){
            std::vector<int> active;for(int j=0;j<n;++j)if(Jcols[(size_t)j][i]!=0)active.push_back(j);
            for(int a:active){g[(size_t)a]+=Jcols[(size_t)a][i]*r0[i];for(int b:active)JtJ[(size_t)a*n+b]+=Jcols[(size_t)a][i]*Jcols[(size_t)b][i];}
        }

        bool improved=false;
        for(int damp=0;damp<8 && !improved;++damp){
            std::vector<f64> Hm=JtJ;
            // LM damping must ADD a floor, not scale: DOFs absent from every
            // constraint have JtJ diagonal exactly 0 and scaling keeps them
            // singular forever.
            for(int j=0;j<n;++j)
                Hm[(size_t)j*n+j]=Hm[(size_t)j*n+j]*(1.0+lambda)+1e-12*(1.0+c);
            dx=g;
            if(!solveIndependentBlocks(Hm,dx,n)) { lambda*=10; continue; }
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
        diagnostics->variables=n;
        std::vector<f64> finalResidual;P.residuals(finalResidual);
        auto rows=normalizedJacobianRows(P,finalResidual);
        std::vector<std::vector<f64>> basis;
        diagnostics->rank=appendIndependentRows(rows,basis);
    }
    if(itersOut)*itersOut=iters;
    P.residuals(r);
    f64 mx=0;
    for(f64 v:r){if(!std::isfinite(v))return INFINITY;mx=tmax(mx,std::fabs(v));}
    return mx;
}

arkjson::Value diagnoseSketch(const Sketch& sketch,double residualTolerance){
    using V=arkjson::Value;
    if(!std::isfinite(residualTolerance)||residualTolerance<=0)throw std::runtime_error("Diagnosis residual tolerance must be positive and finite");
    Problem problem(sketch);std::vector<f64> residual;problem.residuals(residual);
    const auto rows=normalizedJacobianRows(problem,residual);
    std::vector<std::vector<f64>> basis;
    V report=V::makeObj(),items=V::makeArr();
    size_t offset=0;f64 overall=0;int redundant=0,violated=0;
    for(size_t ci=0;ci<sketch.cons.size();++ci){
        const auto& con=sketch.cons[ci];size_t count=residualCount(con.type);
        if(offset+count>residual.size())throw std::runtime_error("Sketch residual mapping is inconsistent");
        V item=V::makeObj(),values=V::makeArr();
        f64 maxResidual=0;bool nonfinite=false;
        std::vector<std::vector<f64>> group;
        for(size_t k=0;k<count;++k){
            f64 value=residual[offset+k];
            if(std::isfinite(value)){values.arr.push_back(V::makeNum(value));maxResidual=std::max(maxResidual,std::abs(value));}
            else{values.arr.push_back(V{});nonfinite=true;}
            group.push_back(rows[offset+k]);
        }
        offset+=count;
        int gain=appendIndependentRows(group,basis);
        bool bad=nonfinite||maxResidual>residualTolerance;
        bool duplicate=gain==0;
        overall=std::max(overall,maxResidual);
        if(bad)++violated;if(duplicate)++redundant;
        item.set("index",V::makeNum((double)ci));
        if(!con.id.empty())item.set("id",V::makeStr(con.id));
        item.set("type",V::makeStr(constraintTypeName(con.type)));
        item.set("a",V::makeNum(con.a));item.set("b",V::makeNum(con.b));
        item.set("residuals",std::move(values));
        item.set("max_residual",nonfinite?V{}:V::makeNum(maxResidual));
        item.set("rank_gain",V::makeNum(gain));
        item.set("locally_redundant",V::makeBool(duplicate));
        item.set("violated",V::makeBool(bad));
        item.set("status",V::makeStr(bad?"conflict_candidate":duplicate?"locally_redundant":"satisfied"));
        items.arr.push_back(std::move(item));
    }
    if(offset!=residual.size())throw std::runtime_error("Sketch residual mapping is inconsistent");
    report.set("variables",V::makeNum(problem.n));
    report.set("jacobian_rank",V::makeNum((double)basis.size()));
    report.set("remaining_dofs",V::makeNum(problem.n-(double)basis.size()));
    report.set("max_residual",V::makeNum(overall));
    report.set("violated_constraints",V::makeNum(violated));
    report.set("locally_redundant_constraints",V::makeNum(redundant));
    report.set("constraint_status",V::makeStr(violated?"conflicting":basis.size()<(size_t)problem.n?"underconstrained":"locally_fully_constrained"));
    report.set("rank_scope",V::makeStr("local_numeric_jacobian_at_current_coordinates"));
    report.set("jacobian_method",V::makeStr("forward_automatic_differentiation"));
    report.set("conflict_scope",V::makeStr("violated residuals after attempted solve; not a minimal conflict set"));
    report.set("constraints",std::move(items));
    return report;
}

} // namespace ark
