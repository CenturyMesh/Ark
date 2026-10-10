// ARK-Point :: brep.cpp — topology, primitives, swept features.
// Faces carry exact analytic surfaces + UV trim loops; loops normalized
// CCW-outer / CW-inner. Swept faces align their cylindrical/conical UV frames
// with the sketch plane so trim angles map 1:1 onto profile angles.
#include "ark/brep.hpp"

namespace ark {

static std::atomic<u64> g_faceIdCounter{1};
u64 nextStableFaceId(){ return g_faceIdCounter++; }

// ---------------------------------------------------------------------------
static TrimLoop rectLoop(Vec2 a, Vec2 b) {
    TrimLoop l; l.outer = true;
    l.uv = { a, Vec2{b.x,a.y}, b, Vec2{a.x,b.y} };
    return l;
}
static TrimLoop discLoopUV(int segs = 48) {
    TrimLoop l; l.outer = true;
    for (int i = 0; i < segs; ++i) {
        f64 t = K_2PI * (f64)i/segs;
        l.uv.push_back({ 0.5+0.5*std::cos(t), 0.5+0.5*std::sin(t) });
    }
    return l;
}
static f64 signedArea(const std::vector<Vec2>& pts) {
    f64 a = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec2& p = pts[i]; const Vec2& q = pts[(i+1)%pts.size()];
        a += p.cross(q);
    }
    return a * 0.5;
}
static void normalizeLoops(std::vector<TrimLoop>& loops) {
    for (auto& l : loops)
        if ((signedArea(l.uv) > 0) != l.outer)
            std::reverse(l.uv.begin(), l.uv.end());
}
static void addFace(Body& b, std::shared_ptr<Surface> s, std::vector<TrimLoop> loops,
                    const char* role) {
    Face f; f.stableId = nextStableFaceId(); f.surf = std::move(s); f.role = role;
    normalizeLoops(loops);
    f.loops = std::move(loops);
    b.faces.push_back(std::move(f));
}

// ---------------------------------------------------------------------------
// Watertight-by-construction mesh builders -------------------------------------
// Shared seam sampling guarantees crack-free closure; orientOutward then
// fixes global winding (valid because the meshes are provably closed).
static constexpr int K_ARC_SEGS = 48;      // per full circle / arc resolution

Mesh Body::buildWatertight() const {
    Mesh m;
    auto quad=[&](u32 a,u32 b,u32 c,u32 d){
        if(a!=b&&b!=c&&a!=c) m.addTri3(a,b,c);
        if(a!=c&&c!=d&&a!=d) m.addTri3(a,c,d);
    };
    if(kind=="box"){
        f64 X=pd1.x,Y=pd1.y,Z=pd1.z;
        Vec3 P[8]={
            {0,0,0},{X,0,0},{X,Y,0},{0,Y,0},
            {0,0,Z},{X,0,Z},{X,Y,Z},{0,Y,Z}};
        for(auto&p:P)m.addVertex(p);
        // +Z,-Z,+X,-X,+Y,-Y
        m.addTri3(0,1,2); m.addTri3(0,2,3);          // bottom
        m.addTri3(4,6,5); m.addTri3(4,7,6);          // top
        m.addTri3(1,5,6); m.addTri3(1,6,2);          // x+
        m.addTri3(0,3,7); m.addTri3(0,7,4);          // x-
        m.addTri3(3,2,6); m.addTri3(3,6,7);          // y+
        m.addTri3(0,4,5); m.addTri3(0,5,1);          // y-
    }
    else if(kind=="cylinder"||kind=="cone"){
        f64 r0,r1,H;
        if(kind=="cylinder"){ r0=pd1.x; r1=pd1.x; H=pd1.y; }
        else                { r0=pd1.x; r1=pd2.x; H=pd2.y; }
        int N=K_ARC_SEGS;
        for(int i=0;i<N;++i){
            f64 t=K_2PI*i/N,c=std::cos(t),s=std::sin(t);
            m.addVertex({c*r0,0,s*r0});              // ring bottom (axis = +Y)
            m.addVertex({c*r1,H,s*r1});              // ring top
        }
        for(int i=0;i<N;++i){
            u32 b0=(u32)(i*2),t0=(u32)(i*2+1);
            u32 j=(i+1)%N;
            u32 b1=(u32)(j*2),t1=(u32)(j*2+1);
            quad(b0,b1,t1,t0);
        }
        // caps as fans around centers
        u32 cb=(u32)m.vertCount(); m.addVertex({0,0,0});
        u32 ct=(u32)m.vertCount(); m.addVertex({0,0,H});
        for(int i=0;i<N;++i){
            u32 j=(i+1)%N;
            if(r0>1e-12){ u32 b0=(u32)(i*2),b1=(u32)(j*2); m.addTri3(cb,b1,b0); }
            if(r1>1e-12){ u32 t0=(u32)(i*2+1),t1=(u32)(j*2+1); m.addTri3(ct,t0,t1); }
        }
    }
    else if(kind=="sphere"){
        f64 R=pd1.x;
        int N=K_ARC_SEGS,M=N/2;
        u32 pt=(u32)m.vertCount(); m.addVertex({0,R,0});      // north pole
        for(int j=1;j<M;++j){
            f64 ph=K_PI*j/M, sp=std::sin(ph), cpz=std::cos(ph);
            for(int i=0;i<N;++i){
                f64 th=K_2PI*i/N;
                m.addVertex({sp*std::cos(th)*R,cpz*R,sp*std::sin(th)*R});
            }
        }
        u32 pbm=(u32)m.vertCount(); m.addVertex({0,-R,0});    // south pole
        // top fan (ring1 starts at global index 1)
        for(int i=0;i<N;++i){
            u32 i0=(u32)(1+i), i2=(u32)(1+(i+1)%N);
            m.addTri3(pt,i2,i0);
        }
        // interior bands (ring j: base index 1+(j-1)*N for j=1..M-1)
        for(int j=1;j<M-1;++j){
            u32 r0=(u32)(1+(j-1)*N), r1=(u32)(1+j*N);
            for(int i=0;i<N;++i){
                u32 i2=(i+1)%N;
                quad(r0+i,r0+i2,r1+i2,r1+i);
            }
        }
        // bottom fan
        u32 rl=(u32)(1+(M-2)*N);
        for(int i=0;i<N;++i){
            u32 i2=(i+1)%N;
            m.addTri3(pbm,rl+i,rl+i2);
        }
    }
    else if(kind=="torus"){
        f64 R=pd1.x,r=pd1.y;
        int N=K_ARC_SEGS,M=K_ARC_SEGS/2;
        for(int j=0;j<=M;++j){
            f64 tv=K_2PI*j/M;
            for(int i=0;i<N;++i){
                f64 tu=K_2PI*i/N;
                m.addVertex({ (R+r*std::cos(tv))*std::cos(tu),
                               r*std::sin(tv)+r,
                              (R+r*std::cos(tv))*std::sin(tu) });
            }
        }
        for(int j=0;j<M;++j)
            for(int i=0;i<N;++i){
                u32 i2=(i+1)%N;
                u32 a=(u32)(j*N+i),b=(u32)(j*N+i2),
                    c2=(u32)((j+1)*N+i2),d=(u32)((j+1)*N+i);
                quad(a,b,c2,d);
            }
    }
    else if(kind=="extrude"){
        auto poly=profile.flattenPts(K_ARC_SEGS);
        size_t n=poly.size();
        Mat4 F=profile.frame;
        std::vector<Vec3> lo(n),hi(n);
        for(size_t i=0;i<n;++i){
            lo[i]=F.transformPoint(Vec3{poly[i].x,poly[i].y,0});
            hi[i]=F.transformPoint(Vec3{poly[i].x,poly[i].y,pd1.x});   // height in pd1.x
        }
        for(auto&p:lo) m.addVertex(p);
        for(auto&p:hi) m.addVertex(p);
        for(size_t i=0;i<n;++i){
            size_t j=(i+1)%n;
            quad((u32)i,(u32)j,(u32)(n+j),(u32)(n+i));                 // wall
        }
        // caps: ear-clipped so concave profiles tessellate correctly.
        // triangulatePolygon returns CCW triples w.r.t. the profile plane.
        auto tris=triangulatePolygon(poly);
        for(size_t t=0;t+2<tris.size();t+=3){
            u32 a=tris[t],b=tris[t+1],c2=tris[t+2];
            m.addTri3(a,c2,b);                                          // bottom (-Z)
            m.addTri3((u32)(n+a),(u32)(n+b),(u32)(n+c2));               // top (+Z)
        }
    }
    else if(kind=="revolve"){
        auto poly=profile.flattenPts(K_ARC_SEGS);
        int n=(int)poly.size();
        bool full=std::fabs(sweepDeg-360)<1e-9;
        int ns=full?K_ARC_SEGS:tmax(8,(int)std::ceil(std::fabs(deg2rad(sweepDeg))/0.06));
        Mat4 F=profile.frame;
        // rings: ns rings of n verts (rings are rotated copies of profile poly)
        for(int k=0;k<ns;++k){
            f64 ang=deg2rad(sweepDeg)*(f64)k/(f64)(full?ns:(ns-1));
            Mat4 R=F*Mat4::rotY(ang);
            for(int i=0;i<n;++i)
                m.addVertex(R.transformPoint(Vec3{poly[(size_t)i].x,poly[(size_t)i].y,0}));
        }
        for(int k=0;k<ns;++k){
            int k2=(k+1)%ns;
            if(!full&&k+1>=ns) break;
            for(int i=0;i<n;++i){
                int j=(i+1)%n;
                u32 a=(u32)(k*n+i),b=(u32)(k*n+j),
                    c2=(u32)(k2*n+j),d=(u32)(k2*n+i);
                quad(a,b,c2,d);
            }
        }
        if(!full){
            // end caps: ear-clipped per end ring, oriented against the ring
            // plane normal so both wedge faces point outward.
            for(int end=0;end<2;++end){
                int ring=end==0?0:ns-1;
                f64 ang=deg2rad(sweepDeg)*(end==0?0.0:1.0);
                Mat4 R=F*Mat4::rotY(ang);
                Vec3 ex=R.transformDir(Vec3{1,0,0});
                Vec3 ey=R.transformDir(Vec3{0,1,0});
                Vec3 nz=R.transformDir(Vec3{0,0,1});
                std::vector<Vec2> pp((size_t)n);
                for(int i=0;i<n;++i){
                    const f32* p=&m.pos[(size_t)(ring*n+i)*3];
                    Vec3 v{p[0],p[1],p[2]};
                    pp[(size_t)i]={v.dot(ex),v.dot(ey)};
                }
                auto ts=triangulatePolygon(pp);
                for(size_t t=0;t+2<ts.size();t+=3){
                    u32 a=(u32)(ring*n+ts[t]),
                        b=(u32)(ring*n+ts[t+1]),
                        c2=(u32)(ring*n+ts[t+2]);
                    const f32*pa=&m.pos[(size_t)a*3];const f32*pb=&m.pos[(size_t)b*3];const f32*pc=&m.pos[(size_t)c2*3];
                    Vec3 ab{pb[0]-pa[0],pb[1]-pa[1],pb[2]-pa[2]};
                    Vec3 ac{pc[0]-pa[0],pc[1]-pa[1],pc[2]-pa[2]};
                    if(ab.cross(ac).dot(nz)<0) std::swap(b,c2);
                    m.addTri3(a,b,c2);
                }
            }
        }
    }
    if(m.triCount()==0) return m;
    m.weld(1e-9);
    meshOrientOutward(m);
    m.computeNormals();
    return m;
}

// ---------------------------------------------------------------------------
Mesh Body::tessellate(const TessOptions& opt) const {
    if(baked){ Mesh m=*baked; return m; }
    if(!kind.empty()) return buildWatertight();
    Mesh m;
    std::vector<const Surface*> surfs;
    std::vector<std::vector<std::vector<Vec2>>> loops;
    surfs.reserve(faces.size()); loops.reserve(faces.size());
    for (auto& f : faces) {
        surfs.push_back(f.surf.get());
        std::vector<std::vector<Vec2>> ls;
        for (auto& l : f.loops) ls.push_back(l.uv);
        loops.push_back(std::move(ls));
    }
    tessellateBodyFaces(surfs, loops, opt, m);
    return m;
}

// ---------------------------------------------------------------------------
Body makeBox(Vec3 dims) {
    Body b;
    b.kind="box"; b.pd1=dims;
    f64 x=dims.x, y=dims.y, z=dims.z;

    auto bottom = std::make_shared<PlaneSurface>(Vec3{}, Vec3{1,0,0}, Vec3{0,1,0}, x, y);
    addFace(b, bottom, {rectLoop({0,0},{1,1})}, "cap");
    auto top = std::make_shared<PlaneSurface>(Vec3{0,y,0}, Vec3{1,0,0}, Vec3{0,-1,0}, x, y);
    addFace(b, top, {rectLoop({0,0},{1,1})}, "cap");

    struct Wall { Vec3 o,u,v; f64 eu,ev; };
    Wall ws[4] = {
        { Vec3{x,0,0}, Vec3{0,1,0},  Vec3{0,0,1}, y, z },  // x=X
        { Vec3{0,y,0}, Vec3{1,0,0},  Vec3{0,0,1}, x, z },  // y=Y
        { Vec3{0,0,0}, Vec3{0,-1,0}, Vec3{0,0,1}, y, z },  // y=0
        { Vec3{0,0,0}, Vec3{1,0,0},  Vec3{0,0,1}, x, z },  // x=0
    };
    for (auto& w : ws)
        addFace(b, std::make_shared<PlaneSurface>(w.o,w.u,w.v,w.eu,w.ev),
                {rectLoop({0,0},{1,1})}, "wall");
    return b;
}

Body makeCylinder(f64 radius, f64 height) {
    Body b;
    b.kind="cylinder"; b.pd1={radius,height,0};
    auto lat = std::make_shared<CylinderSurface>(Vec3{}, Vec3{0,1,0}, radius, height);
    addFace(b, lat, {}, "wall");

    auto capB = std::make_shared<PlaneSurface>(Vec3{}, Vec3{1,0,0}, Vec3{0,0,-1}, radius, radius);
    addFace(b, capB, {discLoopUV()}, "cap");
    auto capT = std::make_shared<PlaneSurface>(Vec3{0,height,0}, Vec3{1,0,0}, Vec3{0,0,1}, radius, radius);
    addFace(b, capT, {discLoopUV()}, "cap");
    return b;
}

Body makeSphere(f64 radius) {
    Body b;
    b.kind="sphere"; b.pd1={radius,0,0};
    auto s = std::make_shared<SphereSurface>(Vec3{}, radius);
    addFace(b, s, {}, "lateral");
    return b;
}

Body makeCone(f64 rBase, f64 rTop, f64 height) {
    Body b;
    b.kind="cone"; b.pd1={rBase,0,0}; b.pd2={rTop,height,0};
    auto lat = std::make_shared<ConeSurface>(Vec3{}, Vec3{0,1,0}, rBase, rTop, height);
    addFace(b, lat, {}, "wall");
    if (rBase > 1e-9) {
        auto cb = std::make_shared<PlaneSurface>(Vec3{}, Vec3{1,0,0}, Vec3{0,0,-1}, rBase, rBase);
        addFace(b, cb, {discLoopUV()}, "cap");
    }
    if (rTop > 1e-9) {
        auto ct = std::make_shared<PlaneSurface>(Vec3{0,height,0}, Vec3{1,0,0}, Vec3{0,0,1}, rTop, rTop);
        addFace(b, ct, {discLoopUV()}, "cap");
    }
    return b;
}

Body makeTorus(f64 R, f64 r) {
    Body b;
    b.kind="torus"; b.pd1={R,r,0};
    auto t = std::make_shared<TorusSurface>(Vec3{0,r,0}, Vec3{0,1,0}, R, r);
    addFace(b, t, {}, "lateral");
    return b;
}

// ---------------------------------------------------------------------------
// Sketch profile flattening ------------------------------------------------------
std::vector<Vec2> SketchProfile::flattenPts(int perArcSegs) const {
    std::vector<Vec2> out;
    for (auto& s : segs) {
        if (s.type == ProfileSeg::Line) {
            if (!out.empty() && !out.back().approx(s.p0)) out.push_back(s.p0);
            out.push_back(s.p1);
        } else {
            int n = perArcSegs;
            for (int i = 0; i < n; ++i) {
                f64 t = lerp(s.a0, s.a1, (f64)i/n);
                out.push_back(s.c + Vec2{std::cos(t),std::sin(t)}*s.r);
            }
        }
    }
    while (out.size()>1 && out.front().approx(out.back(),1e-9)) out.pop_back();
    return out;
}

// Build a planar cap face carrying the closed profile polygon as its trim.
// Plane spans the profile bounding box; trim coords normalized to [0,1]^2.
static void addProfileCap(Body& b, const Mat4& F, const std::vector<Vec2>& poly,
                          f64 planeOffset, Vec3 planeN, bool outerIsHole,
                          const char* role) {
    Vec2 mn(1e300,1e300), mx(-1e300,-1e300);
    for (auto& p : poly){ mn.x=tmin(mn.x,p.x); mn.y=tmin(mn.y,p.y);
                          mx.x=tmax(mx.x,p.x); mx.y=tmax(mx.y,p.y); }
    Vec3 ux = F.transformDir(Vec3{1,0,0}).normalized();
    Vec3 uy = F.transformDir(Vec3{0,1,0}).normalized();
    auto pl = std::make_shared<PlaneSurface>();
    pl->origin = F.transformPoint(Vec3{mn.x, mn.y, 0}) + planeN*planeOffset;
    pl->uDir = ux; pl->vDir = uy;
    pl->uExt = tmax(mx.x-mn.x, 1e-9);
    pl->vExt = tmax(mx.y-mn.y, 1e-9);
    Face f; f.stableId=nextStableFaceId(); f.surf=pl; f.role=role;
    TrimLoop tl; tl.outer=!outerIsHole;
    for (auto& p : poly) tl.uv.push_back({(p.x-mn.x)/pl->uExt, (p.y-mn.y)/pl->vExt});
    f.loops.push_back(std::move(tl));
    normalizeLoops(f.loops);
    b.faces.push_back(std::move(f));
}

// Align a swept-surface UV frame with the sketch plane so profile angles
// correspond directly to u*2π.
static void alignRadialFrame(Surface& s, const Mat4& F, Vec3 axis) {
    Vec3 px = F.transformDir(Vec3{1,0,0});
    px = (px - axis*px.dot(axis));
    if (px.lensq() < K_EPS*K_EPS) px = F.transformDir(Vec3{0,0,1});
    px = px.normalized();
    Vec3 py = axis.cross(px).normalized();
    if (auto* cy = dynamic_cast<CylinderSurface*>(&s)) { cy->refX=px; cy->refY=py; }
    else if (auto* co = dynamic_cast<ConeSurface*>(&s)) { co->refX=px; co->refY=py; }
    else if (auto* to = dynamic_cast<TorusSurface*>(&s)) { to->refX=px; to->refY=py; }
}
static TrimLoop angularTrim(f64 a0, f64 sweep) {
    // partial-revolution trim: u ∈ [a0/2π mod 1 .. +sweep/2π], v ∈ [0,1]
    TrimLoop tl; tl.outer = true;
    f64 start = std::fmod(a0/K_2PI, 1.0); if (start < 0) start += 1.0;
    f64 end = start + sweep/K_2PI;
    int n = tmax(4, (int)std::ceil(sweep/0.15));
    for (int i = 0; i <= n; ++i) tl.uv.push_back({ start+(end-start)*i/n, 0.0 });
    for (int i = 0; i <= n; ++i) tl.uv.push_back({ end-(end-start)*i/n, 1.0 });
    return tl;
}

static Body extrudeOneSide(const SketchProfile& prof, f64 height) {
    Body b;
    b.kind="extrude"; b.profile=prof; b.pd1={height,0,0};
    Mat4 F = prof.frame;
    Vec3 nrm  = F.transformDir(Vec3{0,0,1}).normalized();
    auto poly = prof.flattenPts();

    // bottom cap at z=0, top cap at z=h (top loop reversed -> hole orientation)
    addProfileCap(b, F, poly, 0,     Vec3{}, false, "cap");
    addProfileCap(b, F, poly, height, Vec3{}, false, "cap");

    for (auto& s : prof.segs) {
        if (s.type == ProfileSeg::Line) {
            Vec3 A = F.transformPoint(Vec3{s.p0.x,s.p0.y,0});
            Vec3 B = F.transformPoint(Vec3{s.p1.x,s.p1.y,0});
            Vec3 along = (B-A);
            f64 elen = along.len();
            if (elen < K_EPS) continue;
            auto pl = std::make_shared<PlaneSurface>(A, along*(1.0/elen), nrm, elen, height);
            addFace(b, pl, {rectLoop({0,0},{1,1})}, "wall");
        } else {
            Vec3 C = F.transformPoint(Vec3{s.c.x,s.c.y,0});
            f64 ang = s.a1 - s.a0;
            auto cyl = std::make_shared<CylinderSurface>(C, nrm, s.r, height);
            alignRadialFrame(*cyl, F, nrm);
            std::vector<TrimLoop> loops;
            if (std::fabs(ang-K_2PI) >= 1e-6) loops.push_back(angularTrim(s.a0, ang));
            addFace(b, cyl, std::move(loops), "wall");
        }
    }
    return b;
}

Body extrudeProfile(const SketchProfile& prof, f64 height, bool both) {
    if (!both) return extrudeOneSide(prof, height);
    SketchProfile p2 = prof;
    p2.frame = prof.frame * Mat4::translation({0,0,-height*0.5});
    return extrudeOneSide(p2, height);
}

// ---------------------------------------------------------------------------
Body revolveProfile(const SketchProfile& prof, f64 angleDeg) {
    // Sweep profile (local XY) around local Y axis. Exact faces:
    //   line ∥ Y -> cylinder patch   line ⟂ Y -> annular plane
    //   line oblique -> cone patch   arc -> torus patch
    Body b;
    b.kind="revolve"; b.profile=prof; b.sweepDeg=angleDeg;
    Mat4 F = prof.frame;
    f64 sweepRaw = deg2rad(angleDeg);
    bool full = std::fabs(angleDeg-360.0) < 1e-9;
    f64 sweep = full ? K_2PI : sweepRaw;
    Vec3 axW = F.transformDir(Vec3{0,1,0}).normalized();
    auto poly = prof.flattenPts();

    for (auto& s : prof.segs) {
        if (s.type == ProfileSeg::Arc) continue;      // torus patches: roadmap
        Vec2 A=s.p0, B=s.p1;
        f64 ra=A.x, rb=B.x, ya=A.y, yb=B.y;
        bool vertAxis = feq(ra,rb,1e-12);
        bool horiz    = feq(ya,yb,1e-12);
        if (vertAxis && horiz) continue;              // point segment

        if (vertAxis) {                               // cylinder patch
            f64 y0=tmin(ya,yb), h=std::fabs(yb-ya);
            auto c = std::make_shared<CylinderSurface>(
                F.transformPoint(Vec3{ra,y0,0}), axW, ra, h);
            alignRadialFrame(*c, F, axW);
            std::vector<TrimLoop> loops;
            if (!full) loops.push_back(angularTrim(0, sweep));
            addFace(b, c, std::move(loops), "wall");
        } else if (horiz) {                           // annular planar ring
            f64 rIn=tmin(ra,rb), rOut=tmax(ra,rb);
            if (rOut-rIn < 1e-12) continue;
            auto pl = std::make_shared<PlaneSurface>();
            pl->origin = F.transformPoint(Vec3{0,ya,0});
            Vec3 px2 = F.transformDir(Vec3{1,0,0});
            px2 = (px2 - axW*px2.dot(axW));
            if (px2.lensq() < K_EPS*K_EPS) px2 = F.transformDir(Vec3{0,0,1});
            px2 = px2.normalized();
            pl->uDir = px2;
            pl->vDir = axW.cross(px2).normalized();
            pl->uExt = rOut; pl->vExt = rOut;
            Face f; f.stableId=nextStableFaceId(); f.surf=pl; f.role="cap";
            TrimLoop outer; outer.outer=true;
            TrimLoop inner; inner.outer=false;
            int n=48;
            f64 kO=rOut/pl->uExt, kI=rIn/pl->uExt;
            for(int i=0;i<n;++i){ f64 t=K_2PI*i/n;
                outer.uv.push_back({0.5+0.5*kO*std::cos(t), 0.5+0.5*kO*std::sin(t)});
                inner.uv.push_back({0.5+0.5*kI*std::cos(t), 0.5+0.5*kI*std::sin(t)});
            }
            f.loops.push_back(std::move(outer));
            f.loops.push_back(std::move(inner));
            normalizeLoops(f.loops);
            b.faces.push_back(std::move(f));
        } else {                                      // conical patch
            f64 y0=tmin(ya,yb), h=std::fabs(yb-ya);
            f64 rAtY0 = (y0==ya) ? ra : rb;
            f64 rAtY1 = (y0==ya) ? rb : ra;
            auto c = std::make_shared<ConeSurface>(
                F.transformPoint(Vec3{rAtY0,y0,0}), axW, rAtY0, rAtY1, h);
            alignRadialFrame(*c, F, axW);
            std::vector<TrimLoop> loops;
            if (!full) loops.push_back(angularTrim(0, sweep));
            addFace(b, c, std::move(loops), "wall");
        }
    }

    // wedge end-caps for partial revolutions
    if (!full) {
        for (int side = 0; side < 2; ++side) {
            f64 ang = side==0 ? 0 : sweep;
            Mat4 R = F * Mat4::rotY(ang);
            auto pl = std::make_shared<PlaneSurface>();
            pl->origin = R.transformPoint(Vec3{0,0,0});
            pl->uDir = R.transformDir(Vec3{1,0,0}).normalized();
            pl->vDir = R.transformDir(Vec3{0,1,0}).normalized();
            f64 maxX=0, minY=1e300, maxY=-1e300;
            for(auto&p:poly){ maxX=tmax(maxX,std::fabs(p.x)); minY=tmin(minY,p.y); maxY=tmax(maxY,p.y);}
            pl->uExt=tmax(maxX,1e-9); pl->vExt=tmax(maxY-minY,1e-9);
            pl->origin = R.transformPoint(Vec3{0,minY,0});
            Face f; f.stableId=nextStableFaceId(); f.surf=pl; f.role="cap";
            TrimLoop tl; tl.outer=true;
            for (auto& p : poly) tl.uv.push_back({std::fabs(p.x)/pl->uExt,(p.y-minY)/pl->vExt});
            f.loops.push_back(std::move(tl));
            normalizeLoops(f.loops);
            b.faces.push_back(std::move(f));
        }
    }
    return b;
}

// ---------------------------------------------------------------------------
Body transformBody(const Body& b, const Mat4& M) {
    // Bake-first: transform the watertight mesh so CSG inputs stay closed.
    Body r; r.id=b.id; r.dirty=true;
    Vec3 x=M.transformDir({1,0,0}),y=M.transformDir({0,1,0}),z=M.transformDir({0,0,1});
    if((b.kind=="extrude"||b.kind=="prism_cut")&&std::abs(x.lensq()-1)<1e-10&&std::abs(y.lensq()-1)<1e-10&&std::abs(z.lensq()-1)<1e-10&&
       std::abs(x.dot(y))<1e-10&&std::abs(y.dot(z))<1e-10&&std::abs(z.dot(x))<1e-10&&x.cross(y).dot(z)>0){
        r.kind=b.kind;r.profile=b.profile;r.profile.frame=M*b.profile.frame;r.pd1=b.pd1;r.prismHoles=b.prismHoles;
    }
    Mesh src=b.tessellate(TessOptions{});
    size_t nv=src.vertCount();
    std::vector<Vec3> tmp(nv), out(nv);
    for(size_t i=0;i<nv;++i) tmp[i]={src.pos[i*3],src.pos[i*3+1],src.pos[i*3+2]};
    transform_points_f32(M,tmp.data(),out.data(),nv);
    auto baked=std::make_unique<Mesh>();
    for(size_t i=0;i<nv;++i) baked->addVertex(out[i]);
    baked->idx=src.idx;
    meshOrientOutward(*baked);
    baked->computeNormals();
    r.baked=std::move(baked);
    // keep exact faces for B-rep inspection (mesh is authoritative for ops)
    for (auto& f : b.faces) {
        Face nf=f;
        nf.surf = f.surf ? f.surf->transformed(M) : nullptr;
        r.faces.push_back(std::move(nf));
    }
    return r;
}

} // namespace ark
