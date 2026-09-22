// ARK-Point :: csg.cpp — mesh booleans via surface splitting + parity
// classification.
//
// Method: both inputs must be closed oriented triangle meshes (our primitive/
// sweep builders guarantee this). Every triangle of A is recursively split by
// the planes of B (and vice versa); surviving fragments are classified
// inside/outside the other solid by even-odd ray-cast parity. Boundary is
// assembled from complementary fragments, giving exact shared seams; a
// T-junction healing pass closes residual cracks from fp misses.
#include "ark/mesh.hpp"
#include <array>
#include <cstdlib>
#include <unordered_map>

namespace ark {

namespace {

constexpr f64 kEpsRel = 1e-9;      // plane-distance epsilon (relative)

struct Poly {
    std::vector<Vec3> v;
    Vec3 n{}; f64 d=0;
    bool dead=false;
    void computePlane(){
        Vec3 nn;
        for(size_t i=0;i<v.size();++i){
            const Vec3&a=v[i];const Vec3&b=v[(i+1)%v.size()];
            nn.x+=(a.y-b.y)*(a.z+b.z);
            nn.y+=(a.z-b.z)*(a.x+b.x);
            nn.z+=(a.x-b.x)*(a.y+b.y);
        }
        n=nn.normalized();
        d=n.dot(v[0]);
    }
};

// AABB per poly for broadphase
static void polyBounds(const Poly&p,f64*mn,f64*mx){
    mn[0]=mn[1]=mn[2]=1e300; mx[0]=mx[1]=mx[2]=-1e300;
    for(auto&q:p.v)for(int k=0;k<3;++k){
        f64 c=k==0?q.x:k==1?q.y:q.z;
        mn[k]=tmin(mn[k],c); mx[k]=tmax(mx[k],c);
    }
}

struct SplitOut { std::vector<Poly> front,back; };

// Split poly by plane (n,d). Pieces strictly front -> o.front, back -> o.back.
// Coplanar pieces follow their facing: same->front, opposite->back.
static void splitPoly(const Poly& p,const Vec3&n,f64 d,SplitOut&o,f64 eps){
    int nt=(int)p.v.size();
    std::vector<int> ty(nt);
    int nf=0,nb=0,nc=0;
    for(int i=0;i<nt;++i){
        f64 t=n.dot(p.v[i])-d;
        ty[i]= t>eps?0 : t<-eps?1 : 2;
        nf+=ty[i]==0; nb+=ty[i]==1; nc+=ty[i]==2;
    }
    if(nc==nt){
        Poly q=p;
        (std::fabs(n.dot(p.n))>=0 ? (n.dot(p.n)>0?&o.front:&o.back)
                                  : (n.dot(p.n)>0?&o.front:&o.back))->push_back(q);
        // facing decides: same normal => front side, else back
        o.front.clear(); o.back.clear();          // redo cleanly below
        if(n.dot(p.n)>0) o.front.push_back(q); else o.back.push_back(q);
        return;
    }
    if(nf==0){ o.back.push_back(p); return; }
    if(nb==0){ o.front.push_back(p); return; }

    std::vector<Vec3> fv,bv;
    for(int i=0;i<nt;++i){
        int j=(i+1)%nt;
        Vec3 vi=p.v[i],vj=p.v[j];
        if(ty[i]==0) fv.push_back(vi);
        else if(ty[i]==1) bv.push_back(vi);
        else { fv.push_back(vi); bv.push_back(vi); }
        if((ty[i]==0&&ty[j]==1)||(ty[i]==1&&ty[j]==0)){
            f64 t=(d-n.dot(vi))/(n.dot(vj-vi));
            Vec3 x=vi+(vj-vi)*clamp(t,0.0,1.0);
            fv.push_back(x); bv.push_back(x);
        }
    }
    if(fv.size()>=3){ Poly q; q.v=fv; q.computePlane(); o.front.push_back(q); }
    if(bv.size()>=3){ Poly q; q.v=bv; q.computePlane(); o.back.push_back(q); }
}

// Split 'src' against every triangle-plane of 'other' (broadphase filtered),
// producing convex-ish fragments guaranteed not to cross 'other's surface.
static void splitAgainst(const Poly& src,const std::vector<Poly>& other,
                         const std::vector<std::array<f64,6>>& bounds,
                         std::vector<Poly>& out,f64 eps){
    std::vector<Poly> work{src};
    for(size_t bi=0;bi<other.size();++bi){
        f64 bmn[3],bmx[3];
        bmn[0]=bounds[bi][0];bmn[1]=bounds[bi][1];bmn[2]=bounds[bi][2];
        bmx[0]=bounds[bi][3];bmx[1]=bounds[bi][4];bmx[2]=bounds[bi][5];
        std::vector<Poly> next;
        for(auto&w:work){
            f64 wmn[3],wmx[3]; polyBounds(w,wmn,wmx);
            bool overlap=!(wmx[0]<bmn[0]-eps||wmn[0]>bmx[0]+eps||
                           wmx[1]<bmn[1]-eps||wmn[1]>bmx[1]+eps||
                           wmx[2]<bmn[2]-eps||wmn[2]>bmx[2]+eps);
            if(!overlap){ next.push_back(w); continue; }
            SplitOut so;
            splitPoly(w,other[bi].n,other[bi].d,so,eps);
            for(auto&q:so.front) next.push_back(q);
            for(auto&q:so.back)  next.push_back(q);
        }
        work.swap(next);
    }
    out.swap(work);
}

// even-odd parity of point vs closed mesh: ray-march along +X using
// Moller-Trumbore with jittered retries for borderline hits.
static f64 diag_hint=1.0;
static bool pointInside(const Vec3& pt,const std::vector<Poly>& solid,f64 eps){
    Vec3 d{1,0,0};
    static const f64 J[6][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,-1}};
    for(int attempt=0;attempt<7;++attempt){
        Vec3 o=pt;
        if(attempt){
            f64 s=eps*64.0*(f64)attempt;
            o=o+Vec3{0,J[(attempt-1)%6][0]*s,J[(attempt-1)%6][1]*s};
        }
        i64 hits=0; bool bad=false;
        for(auto&t:solid){
            const Vec3 &v0=t.v[0],&v1=t.v[1],&v2=t.v[2];
            Vec3 e1=v1-v0,e2=v2-v0;
            Vec3 pv=d.cross(e2);
            f64 det=e1.dot(pv);
            if(std::fabs(det)<1e-14){               // ray parallel to plane
                Vec3 pn=e1.cross(e2);
                f64 area=pn.len();
                if(area<1e-20) continue;            // degenerate tri
                f64 dist=pn.dot(v0-o)/area;         // signed dist o->plane? use o
                (void)dist;
                // ray lies in plane only if origin is on it; jitter otherwise skip
                if(std::fabs(pn.normalized().dot(o-v0))>eps*8.0) continue;
                bad=true; break;
            }
            f64 inv=1.0/det;
            Vec3 tv=o-v0;
            f64 u=tv.dot(pv)*inv;
            Vec3 qv=tv.cross(e1);
            f64 v=d.dot(qv)*inv;
            if(u<-1e-12||v<-1e-12||u+v>1+1e-12) continue;      // miss
            if(u<1e-9||v<1e-9||u+v>1-1e-9){ bad=true; break; } // borderline
            f64 tt=e2.dot(qv)*inv;
            if(tt>eps) hits++;
        }
        if(!bad) return (hits&1)==1;
    }
    return false;
}

static void polysToMesh(const std::vector<Poly>& frags,Mesh& m){
    for(auto&p:frags){
        u32 base=(u32)m.vertCount();
        for(auto&v:p.v) m.addVertex(v);
        for(u32 i=1;i+1<(u32)p.v.size();++i)
            m.addTri3(base,base+i,base+i+1);
    }
}

static std::vector<Poly> meshToPolys(const Mesh& m){
    std::vector<Poly> ps; ps.reserve(m.triCount());
    for(size_t t=0;t<m.idx.size();t+=3){
        u32 ia=m.idx[t],ib=m.idx[t+1],ic=m.idx[t+2];
        Poly p;
        p.v={ Vec3{m.pos[ia*3],m.pos[ia*3+1],m.pos[ia*3+2]},
              Vec3{m.pos[ib*3],m.pos[ib*3+1],m.pos[ib*3+2]},
              Vec3{m.pos[ic*3],m.pos[ic*3+1],m.pos[ic*3+2]} };
        p.computePlane();
        if(p.n.len()>0.5) ps.push_back(std::move(p));
    }
    return ps;
}

// heal T-junction micro-cracks --------------------------------------------------
static void healTJunctions(Mesh& m,f64 eps){
    auto key=[](u32 a,u32 b){ return ((u64)a<<32)|(u64)b; };
    for(int pass=0;pass<8;++pass){
        std::unordered_map<u64,char> dir;
        dir.reserve(m.idx.size()*3);
        for(size_t t=0;t<m.idx.size();t+=3)
            for(int e=0;e<3;++e){
                u32 a=m.idx[t+e],b=m.idx[t+(e+1)%3];
                if(a!=b) dir[key(a,b)]=1;
            }
        std::unordered_map<u32,char> isB;
        for(auto&kv:dir){
            u32 b=(u32)(kv.first&0xffffffffull),a=(u32)(kv.first>>32);
            if(!dir.count(key(b,a))){ isB[a]=1; isB[b]=1; }
        }
        if(isB.empty()) return;
        std::vector<u32> bverts;
        for(auto&kv:isB) bverts.push_back(kv.first);

        bool changed=false;
        std::vector<u32> out; out.reserve(m.idx.size());
        auto onSeg=[&](u32 a,u32 b,u32 p)->bool{
            if(p==a||p==b) return false;
            const f32*pa=&m.pos[a*3],*pb=&m.pos[b*3],*pp=&m.pos[p*3];
            f64 ax=pb[0]-pa[0],ay=pb[1]-pa[1],az=pb[2]-pa[2];
            f64 bx=pp[0]-pa[0],by=pp[1]-pa[1],bz=pp[2]-pa[2];
            f64 L2=ax*ax+ay*ay+az*az; if(L2<1e-24) return false;
            f64 t=(bx*ax+by*ay+bz*az)/L2;
            if(t<=2e-3||t>=1-2e-3) return false;
            f64 dx=bx-ax*t,dy=by-ay*t,dz=bz-az*t;
            return dx*dx+dy*dy+dz*dz < (eps*eps);
        };
        for(size_t t=0;t<m.idx.size();t+=3){
            u32 tri[3]={m.idx[t],m.idx[t+1],m.idx[t+2]};
            bool did=false;
            for(int e=0;e<3&&!did;++e){
                u32 a=tri[e],b=tri[(e+1)%3];
                if(dir.count(key(a,b))&&dir.count(key(b,a))) continue; // paired
                for(u32 p:bverts){
                    if(onSeg(a,b,p)){
                        u32 c=tri[(e+2)%3];
                        if(p!=c){ out.insert(out.end(),{a,p,c}); out.insert(out.end(),{p,b,c}); did=true; }
                        break;
                    }
                }
            }
            if(!did) out.insert(out.end(),tri,tri+3); else changed=true;
        }
        m.idx.swap(out);
        if(!changed) break;
    }
}

} // namespace

Mesh meshBoolean(const Mesh& A, const Mesh& B, CsgOp op) {
    AABB bb=A.computeBBox(); bb.grow(B.computeBBox());
    const f64 diag=tmax(bb.diagonal(),1e-9);
    diag_hint=diag;
    const f64 eps=kEpsRel*diag;

    Mesh aN=A,bN=B;
    for(auto&x:aN.pos)x/=diag;
    for(auto&x:bN.pos)x/=diag;

    std::vector<Poly> pa=meshToPolys(aN), pb=meshToPolys(bN);
    std::vector<std::array<f64,6>> ba(pb.size()),bb2(pa.size());
    for(size_t i=0;i<pb.size();++i){ f64 mn[3],mx[3];polyBounds(pb[i],mn,mx);
        ba[i]={mn[0],mn[1],mn[2],mx[0],mx[1],mx[2]}; }
    for(size_t i=0;i<pa.size();++i){ f64 mn[3],mx[3];polyBounds(pa[i],mn,mx);
        bb2[i]={mn[0],mn[1],mn[2],mx[0],mx[1],mx[2]}; }

    // fragment acceptance table ------------------------------------------
    //   union     : A outside B , B outside A
    //   intersect : A inside  B , B inside  A
    //   subtract  : A outside B , B inside  A (flipped)
    const bool aWantIn = op==CsgOp::Intersect;
    const bool bWantIn = op!=CsgOp::Union;
    const bool flipB   = op==CsgOp::Subtract;

    std::vector<Poly> fragA,fragB;
    { std::vector<Poly> acc;
      for(auto&t:pa){
        std::vector<Poly> fr;
        splitAgainst(t,pb,ba,fr,eps/diag);
        for(auto&q:fr){
            Vec3 c; for(auto&v:q.v)c=c+v; c=c*(1.0/(f64)q.v.size());
            bool inside=pointInside(c,pb,eps/diag);
            if(inside==aWantIn) acc.push_back(q);
        }
      }
      fragA.swap(acc);
    }
    { std::vector<Poly> acc;
      for(auto&t:pb){
        std::vector<Poly> fr;
        splitAgainst(t,pa,bb2,fr,eps/diag);
        for(auto&q:fr){
            Vec3 c; for(auto&v:q.v)c=c+v; c=c*(1.0/(f64)q.v.size());
            bool inside=pointInside(c,pa,eps/diag);
            if(inside==bWantIn){
                if(flipB){ std::reverse(q.v.begin(),q.v.end()); q.computePlane(); }
                acc.push_back(q);
            }
        }
      }
      fragB.swap(acc);
    }

    Mesh out;
    polysToMesh(fragA,out);
    polysToMesh(fragB,out);

    for(auto&x:out.pos)x*=diag;
    out.weld(2e-5*diag);
    healTJunctions(out,4e-4*diag);
    meshOrientOutward(out);
    out.computeNormals();
    return out;
}


} // namespace ark
