// ARK-Point :: mesh.cpp â€” adaptive parallel tessellation + mesh utilities.
#include "ark/mesh.hpp"
#include "ark/compute.hpp"
#include <stdexcept>
#include "ark/nurbs.hpp"
#include <unordered_map>
#include <map>
#include <array>

namespace ark {

// ---------------------------------------------------------------------------
// Mesh tools -------------------------------------------------------------------
void Mesh::computeNormals() {
    nrm.assign(pos.size(), 0.f);
    for (size_t t = 0; t < idx.size(); t += 3) {
        u32 i = idx[t], j = idx[t+1], k = idx[t+2];
        const f32* p0=&pos[i*3]; const f32* p1=&pos[j*3]; const f32* p2=&pos[k*3];
        f64 ax=p1[0]-p0[0], ay=p1[1]-p0[1], az=p1[2]-p0[2];
        f64 bx=p2[0]-p0[0], by=p2[1]-p0[1], bz=p2[2]-p0[2];
        f64 nx=ay*bz-az*by, ny=az*bx-ax*bz, nz=ax*by-ay*bx;
        for (int q = 0; q < 3; ++q) {
            u32 vi = idx[t+q];
            nrm[vi*3]  += (f32)nx; nrm[vi*3+1] += (f32)ny; nrm[vi*3+2] += (f32)nz;
        }
    }
    for (size_t i = 0; i < vertCount(); ++i) {
        f64 x=nrm[i*3], y=nrm[i*3+1], z=nrm[i*3+2];
        f64 l=std::sqrt(x*x+y*y+z*z);
        if (l > 1e-20f) { nrm[i*3]=(f32)(x/l); nrm[i*3+1]=(f32)(y/l); nrm[i*3+2]=(f32)(z/l); }
        else { nrm[i*3]=0; nrm[i*3+1]=1; nrm[i*3+2]=0; }
    }
}

void Mesh::weld(f64 eps) {
    const f64 inv = 1.0 / tmax(eps, 1e-12);
    std::map<std::array<i64,3>,u32> table;
    std::vector<f32> newPos; newPos.reserve(pos.size());
    std::vector<u32> remap(vertCount());
    for (size_t i = 0; i < vertCount(); ++i) {
        std::array<i64,3> kv{ (i64)llround((f64)pos[i*3]  *inv),
                              (i64)llround((f64)pos[i*3+1]*inv),
                              (i64)llround((f64)pos[i*3+2]*inv) };
        auto it=table.find(kv);
        if(it!=table.end()){ remap[i]=it->second; continue; }
        u32 ni=(u32)(newPos.size()/3);
        table.emplace(kv,ni);
        newPos.push_back(pos[i*3]); newPos.push_back(pos[i*3+1]); newPos.push_back(pos[i*3+2]);
        remap[i]=ni;
    }
    pos.swap(newPos);
    for (auto& ix : idx) ix = remap[ix];
    // drop fully-degenerate tris
    std::vector<u32> ni; ni.reserve(idx.size());
    for (size_t t=0;t<idx.size();t+=3){
        u32 a=idx[t],b=idx[t+1],c=idx[t+2];
        if(a!=b&&b!=c&&a!=c){ ni.push_back(a);ni.push_back(b);ni.push_back(c);} }
    idx.swap(ni);
}

void Mesh::append(const Mesh& o, const Mat4& X) {
    u32 base=(u32)vertCount();
    std::vector<f32> positions;Mat4 identity;
    if(std::equal(X.m,X.m+16,identity.m))pos.insert(pos.end(),o.pos.begin(),o.pos.end());
    else {if(!transformPacked(o.pos,positions,X))throw std::runtime_error("Invalid packed mesh positions");pos.insert(pos.end(),positions.begin(),positions.end());}
    for(auto ix:o.idx) idx.push_back(base+ix);
}

bool meshIsClosed(const Mesh& m) {
    // closed iff every directed edge appears exactly once and has its opposite
    auto hk=[](u32 a,u32 b){ return ((u64)a<<32)|(u64)b; };
    std::unordered_map<u64,i32> dir;
    dir.reserve(m.idx.size()*3);
    for (size_t t=0;t<m.idx.size();t+=3)
        for(int e=0;e<3;++e){
            u32 a=m.idx[t+e], b=m.idx[t+(e+1)%3];
            if(a==b) return false;
            if(++dir[hk(a,b)]>1) return false;   // duplicate directed edge
        }
    for(auto&kv:dir){
        u32 b=(u32)(kv.first&0xffffffffull), a=(u32)(kv.first>>32);
        if(!dir.count(hk(b,a))) return false;
    }
    return true;
}

i64 meshEulerCharacteristic(const Mesh& m) {
    std::unordered_map<u64,std::pair<u32,u32>> edges; // min,max -> count
    auto hk=[](u32 a,u32 b){ return a<b ? a*1000003ull+b : b*1000003ull+a; };
    for (size_t t=0;t<m.idx.size();t+=3)
        for(int e=0;e<3;++e){
            u32 a=m.idx[t+e], b=m.idx[t+(e+1)%3];
            if(a==b) continue;
            edges[hk(a,b)].second++;
        }
    return (i64)m.vertCount() - (i64)edges.size() + (i64)m.triCount();
}

// ---------------------------------------------------------------------------
// Ear-clipping polygon triangulation ------------------------------------------
std::vector<u32> triangulatePolygon(const std::vector<Vec2>& pts) {
    std::vector<u32> out;
    i64 n=(i64)pts.size();
    if(n<3) return out;

    // signed area -> force CCW index order
    f64 area=0;
    for(i64 i=0;i<n;++i){
        const Vec2&a=pts[(size_t)i]; const Vec2&b=pts[(size_t)((i+1)%n)];
        area += a.cross(b);
    }
    std::vector<u32> idx((size_t)n);
    if(area>0){ for(i64 i=0;i<n;++i) idx[(size_t)i]=(u32)i; }
    else      { for(i64 i=0;i<n;++i) idx[(size_t)i]=(u32)(n-1-i); }

    auto side=[](const Vec2&a,const Vec2&b,const Vec2&p){
        return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);
    };
    auto strictlyInside=[&](const Vec2&p,const Vec2&a,const Vec2&b,const Vec2&c){
        f64 d1=side(a,b,p), d2=side(b,c,p), d3=side(c,a,p);
        return (d1>1e-14&&d2>1e-14&&d3>1e-14)||(d1<-1e-14&&d2<-1e-14&&d3<-1e-14);
    };

    size_t guard=(size_t)n*(size_t)n+16;
    while(idx.size()>3 && guard--){
        bool clipped=false;
        size_t m=idx.size();
        for(size_t k=0;k<m;++k){
            u32 i0=idx[(k+m-1)%m], i1=idx[k], i2=idx[(k+1)%m];
            const Vec2&a=pts[i0],&b=pts[i1],&c=pts[i2];
            if(side(a,b,c)<=1e-14) continue;           // reflex / degenerate corner
            bool ear=true;
            for(u32 j:idx){
                if(j==i0||j==i1||j==i2) continue;
                if(strictlyInside(pts[j],a,b,c)){ ear=false; break; }
            }
            if(ear){
                out.push_back(i0); out.push_back(i1); out.push_back(i2);
                idx.erase(idx.begin()+(i64)k);
                clipped=true;
                break;
            }
        }
        if(!clipped) break;
    }
    if(idx.size()==3){
        out.push_back(idx[0]); out.push_back(idx[1]); out.push_back(idx[2]);
        return out;
    }
    // fallback: fan from first remaining vertex
    out.clear();
    for(size_t k=1;k+1<idx.size();++k){
        out.push_back(idx[0]); out.push_back(idx[k]); out.push_back(idx[k+1]);
    }
    return out;
}

void meshOrientOutward(Mesh& m) {    f64 vol=0;
    for(size_t t=0;t<m.idx.size();t+=3){
        u32 a=m.idx[t],b=m.idx[t+1],c=m.idx[t+2];
        f64 ax=m.pos[a*3],ay=m.pos[a*3+1],az=m.pos[a*3+2];
        f64 bx=m.pos[b*3],by=m.pos[b*3+1],bz=m.pos[b*3+2];
        f64 cx=m.pos[c*3],cy=m.pos[c*3+1],cz=m.pos[c*3+2];
        vol += ax*(by*cz-bz*cy) + ay*(bz*cx-bx*cz) + az*(bx*cy-by*cx);
    }
    if (vol<0) { m.flip(); }
}

// ---------------------------------------------------------------------------
// Trim tests ---------------------------------------------------------------------
bool uvInsideLoops(const std::vector<std::vector<Vec2>>& loops, Vec2 p) {
    bool inside=false;
    for (auto& l : loops) {
        size_t n=l.size();
        if(n<3) continue;
        for (size_t i=0,j=n-1;i<n;j=i++) {
            Vec2 a=l[i], b=l[j];
            if ((a.y>p.y)!=(b.y>p.y)) {
                f64 xint=a.x+(p.y-a.y)*(b.x-a.x)/(b.y-a.y);
                if (p.x<xint) inside=!inside;
            }
        }
    }
    return inside;
}

std::vector<Vec2> densifyLoopUV(const std::function<Vec2(f64)>& param, int segs) {
    std::vector<Vec2> out;
    out.reserve((size_t)segs);
    for (int i=0;i<segs;++i) out.push_back(param((f64)i/segs));
    return out;
}

// ---------------------------------------------------------------------------
// Adaptive quadtree tessellation of one surface ---------------------------------
namespace {

struct FaceTess {
    const Surface* surf;
    const std::vector<std::vector<Vec2>>* loops;
    TessOptions opt;
    Mesh out;

    f64 u0=0,u1=1,v0=0,v1=1;

    struct Cell { f64 u,v,du,dv; int depth; };

    bool closedU() const { return surf->closedU(); }
    bool closedV() const { return surf->closedV(); }

    f64 wrapU(f64 u) const {
        if (!closedU()) return u;
        f64 t=(u-u0)/(u1-u0); t-=std::floor(t);
        return u0+t*(u1-u0);
    }
    f64 wrapV(f64 v) const {
        if (!closedV()) return v;
        f64 t=(v-v0)/(v1-v0); t-=std::floor(t);
        return v0+t*(v1-v0);
    }
    Vec3 pt(f64 u,f64 v) const { return surf->eval(wrapU(u),wrapV(v)); }

    void run() {
        surf->domain(u0,u1,v0,v1);
        AABB bb=surf->bbox();
        f64 scale=tmax(bb.diagonal(),1e-6);
        if (!(opt.tol>0)) opt.tol=scale*1e-4;
        std::vector<Cell> stack;
        stack.push_back({u0,v0,u1-u0,v1-v0,0});
        while(!stack.empty()){
            Cell c=stack.back(); stack.pop_back();
            if (needSplit(c,scale)) {
                if (c.depth>=opt.maxLevels){ emit(c); continue; }
                f64 hu=c.u+c.du*0.5, hv=c.v+c.dv*0.5;
                stack.push_back({c.u,c.v,c.du*0.5,c.dv*0.5,c.depth+1});
                stack.push_back({hu,c.v,c.du*0.5,c.dv*0.5,c.depth+1});
                stack.push_back({c.u,hv,c.du*0.5,c.dv*0.5,c.depth+1});
                stack.push_back({hu,hv,c.du*0.5,c.dv*0.5,c.depth+1});
            } else emit(c);
        }
        finalize();
    }

    bool needSplit(const Cell& c, f64 scale) {
        if (c.depth<1) return true;
        f64 eu=c.u+c.du, ev=c.v+c.dv;
        Vec3 p00=pt(c.u,c.v), p10=pt(eu,c.v), p01=pt(c.u,ev), p11=pt(eu,ev);
        Vec3 pm0=pt(c.u+c.du*0.5,c.v), pm1=pt(c.u+c.du*0.5,ev);
        Vec3 pn0=pt(c.u,c.v+c.dv*0.5), pn1=pt(eu,c.v+c.dv*0.5);
        f64 err=0;
        err=tmax(err,(pm0-(p00+p10)*0.5).len());
        err=tmax(err,(pm1-(p01+p11)*0.5).len());
        err=tmax(err,(pn0-(p00+p01)*0.5).len());
        err=tmax(err,(pn1-(p10+p11)*0.5).len());
        if (err > opt.tol*scale) return true;
        if (tmax(c.du,c.dv) > 0.05 && opt.minAngleDeg>0) {
            // normal-deviation criterion
            Vec3 n00=surf->normalAt(wrapU(c.u),wrapV(c.v));
            Vec3 n11=surf->normalAt(wrapU(eu),wrapV(ev));
            f64 dot=clamp(n00.dot(n11),-1.0,1.0);
            if (rad2deg(std::acos(dot)) > opt.minAngleDeg*4) return true;
        }
        return false;
    }

    void emit(const Cell& c) {
        f64 eu=c.u+c.du, ev=c.v+c.dv;
        Vec3 P[4]={pt(c.u,c.v),pt(eu,c.v),pt(eu,ev),pt(c.u,ev)};
        // skip fully degenerate slivers at poles
        f64 area6=((P[1]-P[0]).cross(P[2]-P[0])).len();
        u32 base=(u32)out.vertCount();
        for(int i=0;i<4;++i) out.addVertex(P[i]);
        if (area6>1e-14){
            if ((P[0]-P[2]).lensq() <= (P[1]-P[3]).lensq()) {
                out.addTri3(base,base+1,base+2); out.addTri3(base,base+2,base+3);
            } else {
                out.addTri3(base,base+1,base+3); out.addTri3(base+1,base+2,base+3);
            }
        } else {
            // pole fan: single triangle if any pair distinct
            if ((P[0]-P[1]).lensq()>1e-28 && (P[0]-P[2]).lensq()>1e-28) out.addTri3(base,base+1,base+2);
            else if ((P[0]-P[2]).lensq()>1e-28 && (P[0]-P[3]).lensq()>1e-28) out.addTri3(base,base+2,base+3);
            else if ((P[0]-P[1]).lensq()>1e-28 && (P[1]-P[3]).lensq()>1e-28) out.addTri3(base,base+1,base+3);
        }
    }

    void finalize() {
        if (loops && !loops->empty()) {
            // Trimmed-face path: keep triangles whose centroid (in UV) lies
            // inside the even-odd trim region. Adaptive subdivision already
            // densified boundary cells, giving clean edges.
            Mesh kept;
            kept.pos.reserve(out.pos.size());
            std::vector<u32> remap(out.vertCount(), UINT32_MAX);
            auto mapV=[&](u32 vi)->u32{
                if(remap[vi]==UINT32_MAX){
                    remap[vi]=(u32)kept.vertCount();
                    kept.addVertex(Vec3{out.pos[vi*3],out.pos[vi*3+1],out.pos[vi*3+2]});
                }
                return remap[vi];
            };
            for(size_t t=0;t<out.idx.size();t+=3){
                u32 a=out.idx[t],b=out.idx[t+1],c=out.idx[t+2];
                Vec3 pa{out.pos[a*3],out.pos[a*3+1],out.pos[a*3+2]};
                Vec3 pb{out.pos[b*3],out.pos[b*3+1],out.pos[b*3+2]};
                Vec3 pc{out.pos[c*3],out.pos[c*3+1],out.pos[c*3+2]};
                Vec3 cen=(pa+pb+pc)*(1.0/3.0);
                Vec2 uvc=projectUV(cen);
                if(uvInsideLoops(*loops,uvc)){
                    u32 x=mapV(a),y=mapV(b),z=mapV(c);
                    if(x!=y&&y!=z&&x!=z) kept.addTri3(x,y,z);
                }
            }
            out=std::move(kept);
        }
        out.weld(1e-7);
        out.computeNormals();
    }

    Vec2 projectUV(Vec3 p) const {
        // seeded grid search + Newton refinement (used only near trim boundaries)
        f64 bu=(u0+u1)*0.5,bv=(v0+v1)*0.5,bestD=1e300;
        for(int i=0;i<=16;++i)for(int j=0;j<=16;++j){
            f64 uu=u0+(u1-u0)*i/16.0, vv=v0+(v1-v0)*j/16.0;
            f64 d=(surf->eval(wrapU(uu),wrapV(vv))-p).lensq();
            if(d<bestD){bestD=d;bu=uu;bv=vv;}
        }
        for(int it=0;it<12;++it){
            f64 h=1e-6;
            Vec3 s=surf->eval(wrapU(bu),wrapV(bv));
            Vec3 su=(surf->eval(wrapU(bu+h),wrapV(bv))-surf->eval(wrapU(bu-h),wrapV(bv)))*(0.5/h);
            Vec3 sv=(surf->eval(wrapU(bu),wrapV(bv+h))-surf->eval(wrapU(bu),wrapV(bv-h)))*(0.5/h);
            Vec3 r=s-p;
            f64 gu=r.dot(su),gv=r.dot(sv);
            f64 a=su.dot(su),b2=su.dot(sv),cv=sv.dot(sv);
            f64 det=a*cv-b2*b2;
            if(std::fabs(det)<K_EPS) break;
            f64 du=(-cv*gu+b2*gv)/det, dv=(b2*gu-a*gv)/det;
            bu=clamp(bu+du,u0,u1); bv=clamp(bv+dv,v0,v1);
            if(std::fabs(du)+std::fabs(dv)<1e-13) break;
        }
        return {bu,bv};
    }
};

} // namespace

Mesh tessellateSurface(const Surface& s, const TessOptions& opt) {
    FaceTess ft; ft.surf=&s; ft.loops=nullptr; ft.opt=opt;
    ft.run();
    return std::move(ft.out);
}

void tessellateBodyFaces(const std::vector<const Surface*>& faces,
                         const std::vector<std::vector<std::vector<Vec2>>>& loopsPerFace,
                         const TessOptions& opt, Mesh& outMesh) {
    size_t nf=faces.size();
    std::vector<Mesh> parts(nf);
    parallel_for(0,(i64)nf,[&](i64 a,i64 e){
        for(i64 i=a;i<e;++i){
            FaceTess ft; ft.surf=faces[i];
            ft.loops=loopsPerFace[i].empty()?nullptr:&loopsPerFace[i];
            ft.opt=opt;
            ft.run();
            parts[i]=std::move(ft.out);
        }
    });

    // Consistent outward winding: orient each face's triangles so its surface
    // normal points away from the body centroid (exact for convex bodies and
    // correct in practice for swept solids). Global volume check afterwards.
    AABB all;
    for(auto&f:faces) if(f) all.grow(f->bbox());
    Vec3 bc=all.valid()?all.center():Vec3{};
    for(size_t i=0;i<nf;++i){
        const Surface* s=faces[i]; auto&pm=parts[i];
        if(!s||pm.triCount()==0) continue;
        f64 u0,u1,v0,v1; s->domain(u0,u1,v0,v1);
        Vec3 P=s->eval((u0+u1)*0.5,(v0+v1)*0.5);
        Vec3 N=s->normalAt((u0+u1)*0.5,(v0+v1)*0.5);
        bool flip=N.dot(P-bc)<0;
        if(flip){
            for(size_t k=0;k<pm.idx.size();k+=3) std::swap(pm.idx[k],pm.idx[k+1]);
        }
    }

    size_t vTot=0,iTot=0;
    for(auto&p:parts){vTot+=p.vertCount(); iTot+=p.triCount();}
    outMesh.clear();
    outMesh.pos.reserve(vTot*3); outMesh.idx.reserve(iTot*3);
    for(auto&p:parts) outMesh.append(p);
    meshOrientOutward(outMesh);
    outMesh.computeNormals();
}

} // namespace ark
