// ARK-Point :: tests â€” kernel unit + integration tests (no framework, exit code).
#include "ark/model.hpp"
#include "ark/io.hpp"
#include "ark/api.h"
#include "ark/serialize.hpp"
#include "ark/json.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>

using namespace ark;
static int g_fail=0;
#define CHECK(cond,msg) do{ if(!(cond)){ printf("FAIL: %s (line %d)\n",msg,__LINE__); ++g_fail; } \
                            else printf("  ok: %s\n",msg); }while(0)

static void testMath(){
    Mat4 A=Mat4::rotZ(0.7)*Mat4::translation({1,2,3});
    Vec3 p=A.transformPoint(A.inverted().transformPoint({3,-2,5}));
    CHECK(p.approx({3,-2,5},1e-9),"mat4 inverse roundtrip");
    Vec3 q=Mat4::axisAngle({0,1,0},K_HALF_PI).transformDir({1,0,0});
    CHECK(q.approx(Vec3{0,0,-1},1e-9),"axis-angle rotY(+90) maps X->-Z (RH)");
    f64 d=Mat4::perspective(1.0,1.0,0.1,100).determinant();
    CHECK(std::fabs(d)>0,"perspective det nonzero");
}

static void testNurbs(){
    auto line=NurbsCurve::polyline({{0,0,0},{10,0,0}});
    CHECK(nurbs_curve_point(line,0.5).approx({5,0,0}),"deg1 curve mid");
    auto circ=NurbsCurve::circle({}, {0,0,1}, 5);
    bool ok=true;
    for(int i=0;i<16;++i){
        Vec3 p=circ.eval((f64)i/16);
        ok &= std::fabs(p.len()-5.0)<1e-9;
    }
    CHECK(ok,"rational circle radius exact");
    auto interp=NurbsCurve::interpolateCubic({{0,0,0},{5,3,0},{10,-2,0},{15,0,0}});
    ok=true;
    Vec3 pts[4]={{0,0,0},{5,3,0},{10,-2,0},{15,0,0}};
    // endpoints of bezier spans hit data points
    ok &= interp.eval(0).approx(pts[0],1e-6);
    ok &= interp.eval(interp.tmax()).approx(pts[3],1e-6);
    CHECK(ok,"cubic interpolation endpoints");

    NurbsSurface s; s.pu=s.pv=1; s.nu=s.nv=2;
    s.U={0,0,1,1}; s.V={0,0,1,1};
    s.P={{0,0,0},{10,0,0},{0,10,0},{10,10,3}};
    s.W.assign(4,1);
    Vec3 c=s.eval(0.5,0.5);
    CHECK(c.approx(Vec3{5,5,0.75},1e-9),"bilinear patch center");
}

static void testPrimitivesAndTess(){
    Body box=makeBox({10,20,30});
    Mesh m=box.tessellate({});
    CHECK(m.triCount()>=12 && meshIsClosed(m),"box mesh watertight");
    AABB bb=m.computeBBox();
    CHECK(bb.extent().approx(Vec3{10,20,30},1e-4),"box bbox extents");

    Body cyl=makeCylinder(5,10);
    Mesh cm=cyl.tessellate({});
    CHECK(meshIsClosed(cm),"cylinder closed");
    i64 chi=meshEulerCharacteristic(cm);
    CHECK(chi==2,"cylinder genus 0 (chi=2)");

    Body sph=makeSphere(7);
    Mesh sm=sph.tessellate({});
    CHECK(meshIsClosed(sm),"sphere closed");
    bb=sm.computeBBox();
    CHECK(bb.extent().approx(Vec3{14,14,14},2e-2*tmax(1.,bb.diagonal())),"sphere bbox ~2r");
}

static void testExtrudeRevolve(){
    SketchProfile prof;
    prof.segs={
        {ProfileSeg::Line,{0,0},{10,0}},
        {ProfileSeg::Line,{10,0},{10,10}},
        {ProfileSeg::Line,{10,10},{0,10}},
        {ProfileSeg::Line,{0,10},{0,0}},
    };
    Body b=extrudeProfile(prof,8,false);
    Mesh m=b.tessellate({});
    CHECK(meshIsClosed(m),"extrude rect closed");
    AABB bb=m.computeBBox();
    CHECK(bb.extent().approx(Vec3{10,10,8},1e-6),"extrude extents");

    Body rb=revolveProfile(prof,360);
    Mesh rm=rb.tessellate({});
    CHECK(meshIsClosed(rm),"revolve rect closed (torus-ish solid)");
    AABB rbb=rm.computeBBox();
    CHECK(rbb.extent().x>18 && rbb.extent().z>18 && rbb.extent().y>9,"revolve outer dia ~=20, height 10");

    SketchProfile circProf;
    Vec2 c{0,0}; f64 r=5;
    for(int q=0;q<4;++q)
        circProf.segs.push_back({ProfileSeg::Arc,{},{},c,r,
            q*K_HALF_PI,(q+1)*K_HALF_PI});
    Body cb=extrudeProfile(circProf,10,false);
    Mesh cm2=cb.tessellate({});
    CHECK(meshIsClosed(cm2),"circle extrude closed (cylinder)");
    AABB cbb=cm2.computeBBox();
    CHECK(cbb.extent().approx(Vec3{10,10,10},1e-3),"cylinder extents 2r x h");
}

static void testCSG(){
    Body A=makeBox({10,10,10});
    Body B=makeBox({10,10,10}); B=transformBody(B,Mat4::translation({5,5,5}));
    Mesh ma=A.tessellate({}), mb=B.tessellate({});
    Mesh uni=meshBoolean(ma,mb,CsgOp::Union);
    meshOrientOutward(uni);
    CHECK(meshIsClosed(uni),"union closed");
    AABB u=uni.computeBBox();
    CHECK(u.extent().approx(Vec3{15,15,15},1e-3*tmax(1.,u.diagonal())),"union bbox 15^3");

    Body C=makeBox({4,4,4}); C=transformBody(C,Mat4::translation({3,3,3}));
    Mesh mc=C.tessellate({});
    Mesh sub=meshBoolean(ma,mc,CsgOp::Subtract);
    CHECK(meshIsClosed(sub),"subtract closed");
    AABB ab=sub.computeBBox();
    CHECK(ab.extent().x>9.99 && ab.extent().y>9.99,"subtract keeps shell extent");

    Mesh inter=meshBoolean(ma,mc,CsgOp::Intersect);
    meshOrientOutward(inter);
    CHECK(meshIsClosed(inter),"intersect closed");
    AABB ib2=inter.computeBBox();
    CHECK(ib2.extent().approx(Vec3{4,4,4},1e-3*tmax(1.,ib2.diagonal())),"intersect bbox = C (fully inside)");
}

static void testExpressions(){
    ExprEnv e; e.vars={ {"w",12.0} };
    bool ok=false;
    CHECK(std::fabs(evalExpr("w/2+3*2",e,&ok)-12)<1e-12 && ok,"expr precedence");
    CHECK(std::fabs(evalExpr("sqrt(w)*pi",e)-std::sqrt(12.0)*K_PI)<1e-12,"expr functions");
    evalExpr("w/",e,&ok); CHECK(!ok,"expr syntax error detected");
}

static void testSketchSolver(){
    Sketch sk;
    int l=sk.addLine(0,0, 9,4);          // will be constrained horizontal len 10
    sk.cons.push_back({SketchCon::Horizontal,l});
    sk.cons.push_back({SketchCon::FixedX,l,-1,0,0,0.0});   // start x = 0
    sk.cons.push_back({SketchCon::FixedY,l,-1,0,0,0.0});   // start y = 0
    sk.cons.push_back({SketchCon::DistPP,l,0,l,1,10.0}); // length 10
    int iters=0;
    f64 res=solveSketch(sk,&iters);
    CHECK(res<1e-6,"sketch converges");
    printf("    solved line: %.4f %.4f %.4f %.4f (res=%g iters=%d)\n",
        sk.ents[l].dof[0],sk.ents[l].dof[1],sk.ents[l].dof[2],sk.ents[l].dof[3],res,iters);
    CHECK(std::fabs(sk.ents[l].dof[0])<1e-6&&std::fabs(sk.ents[l].dof[1])<1e-6&&
          std::fabs(sk.ents[l].dof[2]-10)<1e-4&&std::fabs(sk.ents[l].dof[3])<1e-4,
          "horizontal length-10 solved");

    // regression: unconstrained entities must not stall the solver
    // (zero diagonal rows in JtJ made every LM step fail -> iters=0)
    Sketch sk2;
    int l0=sk2.addLine(0,0, 9,4);
    int lfree=sk2.addLine(50,50, 80,-20);   // referenced by no constraint
    sk2.cons.push_back({SketchCon::Horizontal,l0});
    sk2.cons.push_back({SketchCon::DistPP,l0,0,l0,1,10.0});
    res=solveSketch(sk2,&iters);
    auto&d0 =sk2.ents[(size_t)l0].dof;
    auto&df =sk2.ents[(size_t)lfree].dof;
    CHECK(res<1e-6,"solver converges despite unconstrained entities");
    f64 len=std::sqrt((d0[2]-d0[0])*(d0[2]-d0[0])+(d0[3]-d0[1])*(d0[3]-d0[1]));
    CHECK(std::fabs(d0[1]-d0[3])<1e-4 && std::fabs(len-10.0)<1e-4,
          "constrained line solved");
    CHECK(df[0]==50 && df[1]==50 && df[2]==80 && df[3]==-20,
          "free line left untouched");
}

static void testFixedConstraint(){
    Sketch sk;
    int l=sk.addLine(0,0, 5,1);          // dragged around by solver initially
    sk.cons.push_back({SketchCon::Horizontal,l});
    sk.cons.push_back({SketchCon::Fixed,l,-1,0,0,2.0,3.0});   // pin start to (2,3)
    sk.cons.push_back({SketchCon::FixedX,l,-1,1,0,12.0});     // end x = 12
    int iters=0;
    f64 res=solveSketch(sk,&iters);
    auto&d=sk.ents[(size_t)l].dof;
    printf("    fixed line: %.4f %.4f %.4f %.4f (res=%g iters=%d)\n",d[0],d[1],d[2],d[3],res,iters);
    CHECK(res<1e-6,"Fixed constraint converges");
    CHECK(std::fabs(d[0]-2)<1e-5 && std::fabs(d[1]-3)<1e-5 &&
          std::fabs(d[2]-12)<1e-4 && std::fabs(d[3]-3)<1e-4,
          "point pinned at (2,3), horizontal, end at x=12");
}

static void testSketchFeatures(){
    Document d;
    d.setParam("H","6");

    // concave L profile, segments deliberately shuffled/reversed:
    // (0,0)->(10,0)->(10,4)->(4,4)->(4,10)->(0,10)->close ; notch 6x6
    Sketch sk;
    sk.addLine(0,0, 10,0);
    sk.addLine(10,4, 10,0);
    sk.addLine(10,4, 4,4);
    sk.addLine(0,10, 4,10);
    sk.addLine(4,4, 4,10);
    sk.addLine(0,0, 0,10);
    d.setSketch("L",sk);

    Feature e; e.type=FeatureType::Extrude; e.name="pad";
    e.p={{"sketch","L"},{"height","H"}};
    (void)d.addFeature(e);
    d.recompute();
    CHECK(d.lastResults.size()==1 && d.lastResults[0].ok &&
          d.lastResults[0].error.empty(),"L-sketch extrude evaluates");
    Mesh m=d.evalBodies[0].tessellate(d.tessOpt);
    CHECK(meshIsClosed(m),"L extrude watertight");
    CHECK(meshEulerCharacteristic(m)==2,"L extrude chi=2");
    AABB bb=m.computeBBox();
    CHECK(bb.extent().approx(Vec3{10,10,6},1e-3),"L extrude extents 10x10x6");

    // signed volume proves the concave caps triangulate correctly
    // (a centroid fan would still be closed but with wrong volume)
    auto meshVolume=[](const Mesh& mm){
        f64 v=0;
        for(size_t t=0;t<mm.idx.size();t+=3){
            u32 a=mm.idx[t],b=mm.idx[t+1],c=mm.idx[t+2];
            Vec3 pa{mm.pos[a*3],mm.pos[a*3+1],mm.pos[a*3+2]},
                 pb{mm.pos[b*3],mm.pos[b*3+1],mm.pos[b*3+2]},
                 pc{mm.pos[c*3],mm.pos[c*3+1],mm.pos[c*3+2]};
            v+=pa.dot(pb.cross(pc));
        }
        return v/6.0;
    };
    f64 vol=meshVolume(m);
    CHECK(std::fabs(vol-384.0)<1e-2*384.0 && vol>0,
          "L extrude volume = (100-36)*6 = 384");

    // parametric propagation into sketch features
    d.setParam("H","9");
    d.recompute();
    Mesh m9=d.evalBodies[0].tessellate(d.tessOpt);
    AABB b9=m9.computeBBox();
    CHECK(b9.extent().approx(Vec3{10,10,9},1e-3),"param change re-extrudes H=9");

    // circle-only sketch -> disc extrude
    Sketch cs; cs.addCircle(0,0,5);
    d.setSketch("disc",cs);
    Feature e2; e2.type=FeatureType::Extrude; e2.name="pad2";
    e2.p={{"sketch","disc"},{"height","10"}};
    d.addFeature(e2);
    d.recompute();
    Mesh m2=d.evalBodies[1].tessellate(d.tessOpt);
    CHECK(meshIsClosed(m2),"circle-sketch extrude watertight");
    AABB b2=m2.computeBBox();
    CHECK(b2.extent().approx(Vec3{10,10,10},2e-2),"circle-sketch extents 10x10x10");
}

static void testPersistence(){
    Document d;
    d.setParam("W","40"); d.setParam("h","W/4");
    Sketch sk;
    int l=sk.addLine(0,0, 9,4);
    sk.cons.push_back({SketchCon::Horizontal,l});
    sk.cons.push_back({SketchCon::DistPP,l,0,l,1,12.0});
    d.setSketch("base",sk);
    Feature b; b.type=FeatureType::Box; b.name="blk"; b.p={{"dx","W"},{"dy","10"},{"dz","h"}};
    u64 idb=d.addFeature(b);
    Feature cut; cut.type=FeatureType::Boolean; cut.name="cut"; cut.opInt=1;
    cut.inputs={idb,idb};
    u64 idc=d.addFeature(cut);
    d.recompute();
    (void)idc;

    const char* path=".test_doc.arkjson";
    CHECK(saveDocument(d,path),"saveDocument writes file");

    Document r; std::string err;
    bool ok=loadDocument(path,r,&err);
    if(!ok) printf("    load err: %s\n",err.c_str());
    CHECK(ok&&err.empty(),"loadDocument parses");
    CHECK(r.params.size()==d.params.size() &&
          std::fabs(r.getParam("h")-10.0)<1e-12,"params restored with values");
    CHECK(r.sketches.size()==1 && r.sketches[0].name=="base" &&
          r.sketches[0].sk.ents.size()==1 && r.sketches[0].sk.cons.size()==2,
          "sketch restored");
    CHECK(r.features.size()==d.features.size() &&
          r.features[0].id==d.features[0].id && r.features[1].type==FeatureType::Boolean,
          "feature ids/types/op restored");
    CHECK(r.nextFeatureId_>r.features.back().id,"next feature id preserved");

    r.recompute();
    CHECK(r.evalBodies.size()==d.evalBodies.size(),"loaded doc recomputes same body count");

    // roundtrip through JSON text as well
    std::string j=documentToJson(d);
    Document r2; ok=documentFromJson(j,r2,&err)&&err.empty();
    CHECK(ok && r2.features.size()==d.features.size(),"documentFromJson roundtrip");

    // malformed input rejected cleanly
    Document keep;
    CHECK(!documentFromJson("{\"features\":[{\"type\":\"nope\"}]}",keep,&err) && !err.empty(),
          "bad feature type rejected");
    std::remove(path);
}

static void testCAbi(){
    ArkDoc* doc=ark_doc_create();
    CHECK(ark_param_set(doc,"W","30")==0,"param_set");
    u64 id=ark_add_box(doc,10,10,10);
    CHECK(id>0,"add box");
    CHECK(ark_feature_set_param(doc,id,"dx","W")==0,"feature_set_param");
    CHECK(ark_recompute(doc)==0,"recompute ok");
    CHECK(ark_body_count(doc)==1,"one body");

    const char* ents=
        "[{\"kind\":\"line\",\"dof\":[0,0,8,0]},{\"kind\":\"line\",\"dof\":[8,0,8,8]},"
        "{\"kind\":\"line\",\"dof\":[8,8,0,8]},{\"kind\":\"line\",\"dof\":[0,8,0,0]}]";
    CHECK(ark_sketch_set(doc,"sq",ents,nullptr)==0,"sketch_set via JSON");
    char* sj=ark_sketch_json(doc,"sq");
    CHECK(sj && strstr(sj,"line")!=nullptr,"sketch_json returns entities");
    ark_free(sj);

    u64 ex=ark_feature_add(doc,"extrude");
    CHECK(ex>0,"generic feature_add");
    CHECK(ark_feature_set_param(doc,ex,"sketch","sq")==0 &&
          ark_feature_set_param(doc,ex,"height","5")==0,"set sketch params");
    CHECK(ark_recompute(doc)==0,"recompute with sketch feature");
    CHECK(ark_body_count(doc)==2,"two bodies after sketch extrude");

    char* dj=ark_doc_json(doc);
    bool jsonOk=false;
    if(dj){ arkjson::Value v; jsonOk=arkjson::parse(dj,v)&&v.find("params")&&v.find("sketches"); }
    CHECK(jsonOk,"doc_json is valid escaped JSON");
    ark_free(dj);

    CHECK(ark_doc_save(doc,".test_api.arkjson")==0,"doc_save");
    ArkDoc* d2=ark_doc_create();
    CHECK(ark_doc_load(d2,".test_api.arkjson")==0,"doc_load");
    CHECK(ark_body_count(d2)==2,"loaded doc has both bodies");
    CHECK(ark_sketch_remove(d2,"sq")==0,"sketch_remove");
    ark_doc_destroy(d2);
    std::remove(".test_api.arkjson");
    ark_doc_destroy(doc);
}

static void testParametricDoc(){
    Document d;
    d.setParam("L","50"); d.setParam("R","L/5");
    Feature b; b.type=FeatureType::Box; b.name="blk"; b.p={{"dx","L"},{"dy","L"},{"dz","L"}};
    u64 idb=d.addFeature(b);
    Feature s; s.type=FeatureType::Sphere; s.name="sph"; s.p={{"r","R"}};
    u64 ids=d.addFeature(s);
    d.recompute();
    CHECK(d.evalBodies.size()==2,"two features -> two bodies");
    AABB bb=d.evalBodies[1].bbox();
    CHECK(bb.extent().approx(Vec3{20,20,20},1e-3),"param dependency L/5=10 dia");
    d.setParam("L","100"); d.recompute();
    bb=d.evalBodies[1].bbox();
    CHECK(bb.extent().approx(Vec3{40,40,40},0.2),"recompute propagates param change");
    (void)idb;(void)ids;
}

static void testSnapshotIO(){
    Document d;
    Feature b;b.type=FeatureType::Torus;b.p={{"R","10"},{"r","3"}};d.addFeature(b);
    d.recompute();
    Mesh m=d.evalBodies[0].tessellate(d.tessOpt);
    SnapCam cam; cam.dist=45;
    Image img=renderSnapshot(m,320,240,cam);
    bool wrote=writePNG(".test_snap.png",img);
    CHECK(wrote && img.px.size()==(size_t)320*240*3,"png snapshot render+write");
    std::remove(".test_snap.png");
    CHECK(exportSTL(".test.stl",m)&&exportOBJ(".test.obj",m),"stl/obj export");
    std::remove(".test.stl"); std::remove(".test.obj");
}

int main(){
    cpu_detect();
    printf("== ARK kernel tests ==\n");
    testMath();
    testNurbs();
    testPrimitivesAndTess();
    testExtrudeRevolve();
    testCSG();
    testExpressions();
    testSketchSolver();
    testFixedConstraint();
    testSketchFeatures();
    testParametricDoc();
    testPersistence();
    testCAbi();
    testSnapshotIO();
    if(g_fail){ printf("\n%d FAILURE(S)\n",g_fail); return 1; }
    printf("\nALL TESTS PASSED\n");
    return 0;
}
