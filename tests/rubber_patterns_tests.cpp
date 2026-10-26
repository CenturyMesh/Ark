// SPDX-License-Identifier: Apache-2.0
// 059C local evidence for A.02/B.04/D.02. No renderer/solid/fabrication claims.
#include "ark/rubber_patterns.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#ifdef _WIN32
#include <malloc.h>
#endif

// Intercept standard/aligned allocations to check the public indexed-access
// contract without a machine-dependent latency threshold.
static std::atomic<size_t> allocations{0};
static std::atomic<size_t> allocatedBytes{0};
void* operator new(size_t size) { ++allocations; allocatedBytes+=size; if (void* p=std::malloc(size ? size : 1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
void* operator new(size_t size, std::align_val_t alignment) {
    ++allocations;
    allocatedBytes+=size;
    void* p = nullptr;
#ifdef _WIN32
    p = _aligned_malloc(size ? size : 1, static_cast<size_t>(alignment));
#else
    if (posix_memalign(&p, static_cast<size_t>(alignment), size ? size : 1) != 0) p = nullptr;
#endif
    if (!p) throw std::bad_alloc(); return p;
}
void operator delete(void* p, std::align_val_t) noexcept {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete(void* p, size_t, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }
void* operator new[](size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete[](void* p, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }
void operator delete[](void* p, size_t, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }

namespace {
using namespace ark;
using J = arkjson::Value;
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)
bool near(double a, double b, double tolerance = 1e-10) { return std::abs(a-b) <= tolerance; }
bool near(Vec3 a, Vec3 b, double tolerance = 1e-10) { return (a-b).len() <= tolerance; }
J json(const char* source) { J value; if (!arkjson::parse(source,value)) throw std::runtime_error("bad test JSON"); return value; }
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}
void validFrame(const RubberPatternFrame& f) {
    CHECK(std::isfinite(f.position_mm.x) && std::isfinite(f.position_mm.y) && std::isfinite(f.position_mm.z));
    CHECK(near(f.tangent.len(), 1)); CHECK(near(f.bitangent.len(),1)); CHECK(near(f.normal.len(),1));
    CHECK(near(f.tangent.dot(f.bitangent),0)); CHECK(near(f.tangent.dot(f.normal),0)); CHECK(near(f.bitangent.dot(f.normal),0));
    CHECK(near(f.tangent.cross(f.bitangent),f.normal));
    CHECK(near(f.transform.transformPoint({}),f.position_mm));
    CHECK(near(f.transform.transformDir({1,0,0}),f.tangent));
    CHECK(near(f.transform.transformDir({0,1,0}),f.bitangent));
    CHECK(near(f.transform.transformDir({0,0,1}),f.normal));
    CHECK(f.u_rad >= 0 && f.u_rad < K_2PI);
    CHECK(f.transform.m[3] == 0 && f.transform.m[7] == 0 && f.transform.m[11] == 0 && f.transform.m[15] == 1);
}
J cylinder() { return json(R"({"surface":{"kind":"cylinder","radius_mm":20,"length_mm":30},"around":17,"rows":11})"); }
J torus() { return json(R"({"surface":{"kind":"torus","major_radius_mm":50,"radial_radius_mm":12,"axial_radius_mm":7,"v_start_deg":-60,"v_end_deg":60},"around":19,"rows":9})"); }
J sphere() { return json(R"({"surface":{"kind":"sphere","radius_mm":25},"count":509})"); }
J racetrack() { return json(R"({"surface":{"kind":"racetrack","radius_mm":13,"center_distance_mm":71,"width_mm":22},"around":61,"rows":5})"); }
void updateSurface(J& recipe, const char* key, double value) { J s=*recipe.find("surface"); s.set(key,J::makeNum(value)); recipe.set("surface",s); }

void surfaceAndDistributionChecks() {
    for (const char* distribution : {"grid","stagger","helix","chevron","braided"}) {
        for (int shape=0; shape<3; ++shape) {
            J recipe=shape==0 ? cylinder() : shape==1 ? torus() : racetrack();
            recipe.set("distribution",J::makeStr(distribution));
            RubberPatternPlan p(recipe);
            CHECK(p.count() == static_cast<uint32_t>(recipe.find("around")->num*recipe.find("rows")->num));
            for (uint32_t i=0; i<p.count(); ++i) {
                const auto f=p.frame(i); validFrame(f);
                CHECK(f.index == i);
                if (shape==0) {
                    CHECK(near(std::hypot(f.position_mm.x,f.position_mm.y),20));
                    CHECK(f.position_mm.z > -15 && f.position_mm.z < 15);
                    CHECK(near(f.normal,Vec3{f.position_mm.x/20,f.position_mm.y/20,0}));
                } else if (shape==1) {
                    const double radius=std::hypot(f.position_mm.x,f.position_mm.y);
                    CHECK(near(std::pow((radius-50)/12,2)+std::pow(f.position_mm.z/7,2),1));
                    CHECK(f.v_rad > -K_PI/3 && f.v_rad < K_PI/3);
                    CHECK(radius>50); // Outer tire band preserved by all winding modes.
                    Vec3 gradient{(radius-50)*f.position_mm.x/(144*radius),(radius-50)*f.position_mm.y/(144*radius),f.position_mm.z/49};
                    CHECK(near(f.normal,gradient.normalized()));
                } else {
                    const double nearestCenterX=clamp(f.position_mm.x,-35.5,35.5);
                    Vec3 fromCenter{f.position_mm.x-nearestCenterX,f.position_mm.y,0};
                    CHECK(near(fromCenter.len(),13)); CHECK(near(fromCenter.normalized(),f.normal));
                    CHECK(f.position_mm.z > -11 && f.position_mm.z < 11);
                    CHECK(near(f.bitangent,Vec3{0,0,1}));
                }
                const auto repeated=p.frame(i);
                for (int k=0;k<16;++k) CHECK(repeated.transform.m[k]==f.transform.m[k]);
            }
        }
    }
    for (const char* distribution : {"fibonacci","spherical_weave"}) {
        J recipe=sphere(); recipe.set("distribution",J::makeStr(distribution));
        RubberPatternPlan p(recipe); double sumZ=0;
        for (uint32_t i=0;i<p.count();++i) {
            const auto f=p.frame(i); validFrame(f);
            CHECK(near(f.position_mm.len(),25)); CHECK(near(f.position_mm/25,f.normal));
            CHECK(f.v_rad > 0 && f.v_rad < K_PI);
            CHECK(near(f.normal.z,1-2*(i+.5)/p.count())); sumZ+=f.normal.z;
        }
        CHECK(std::abs(sumZ)<1e-10);
    }
}

void coverageAndFrameChecks() {
    J c=cylinder(); updateSurface(c,"axial_start_mm",2); updateSurface(c,"axial_end_mm",5);
    c.set("rows",J::makeNum(1)); c.set("around",J::makeNum(1));
    auto f=RubberPatternPlan(c).frame(0); CHECK(near(f.position_mm,Vec3{20,0,3.5}));
    c.set("orientation_deg",J::makeNum(90)); c.set("normal_offset_mm",J::makeNum(3));
    c.set("placement",json(R"({"origin_mm":[4,7,2],"x_axis":[0,1,0],"z_axis":[1,0,0]})"));
    f=RubberPatternPlan(c).frame(0); validFrame(f);
    CHECK(near(f.position_mm,Vec3{7.5,30,2})); CHECK(near(f.normal,Vec3{0,1,0})); CHECK(near(f.tangent,Vec3{1,0,0}));
    J r=racetrack(); updateSurface(r,"width_start_mm",-2); updateSurface(r,"width_end_mm",2);
    const RubberPatternPlan rp(r); CHECK(rp.frame(0).transverse_mm > -2); CHECK(rp.frame(rp.count()-1).transverse_mm < 2);
    J s=sphere(); updateSurface(s,"polar_start_deg",30); updateSurface(s,"polar_end_deg",70);
    const RubberPatternPlan sp(s);
    for (uint32_t i=0;i<sp.count();++i) {
        const auto f2=sp.frame(i); CHECK(f2.v_rad>deg2rad(30) && f2.v_rad<deg2rad(70));
    }
    s.set("count",J::makeNum(1)); const auto sf=RubberPatternPlan(s).frame(0); validFrame(sf);
    CHECK(near(sf.normal.z,(std::cos(deg2rad(30))+std::cos(deg2rad(70)))/2));
    for (double phase : {0.,360.,-360.,360000.,-360000.}) {
        J recipe=cylinder(); recipe.set("phase_deg",J::makeNum(phase));
        CHECK(near(RubberPatternPlan(recipe).frame(0).position_mm,RubberPatternPlan(cylinder()).frame(0).position_mm,1e-8));
    }
    J stagger=cylinder(); stagger.set("distribution",J::makeStr("stagger"));
    const RubberPatternPlan st(stagger); CHECK(near(st.frame(17).u_rad,K_PI/17));
    CHECK(near(st.frame(34).u_rad,0));
    for (double start : {-360.,-180.,-60.,0.}) {
        J t=torus(); updateSurface(t,"v_start_deg",start); updateSurface(t,"v_end_deg",start+360);
        RubberPatternPlan tp(t); validFrame(tp.frame(0)); validFrame(tp.frame(tp.count()-1));
    }
}

void racetrackJunctionsAndPoles() {
    const double d=71,r=13,arc=K_PI*r,perimeter=2*d+2*arc;
    J recipe=racetrack(); recipe.set("around",J::makeNum(1)); recipe.set("rows",J::makeNum(1));
    const Vec3 expectedPositions[]={{d/2,r,0},{-d/2,r,0},{-d/2,-r,0},{d/2,-r,0},{d/2,r,0}};
    const Vec3 expectedNormals[]={{0,1,0},{0,1,0},{0,-1,0},{0,-1,0},{0,1,0}};
    int j=0;
    for (double distance : {0.,d,d+arc,2*d+arc,perimeter}) {
        recipe.set("phase_deg",J::makeNum(360*distance/perimeter));
        const auto at=RubberPatternPlan(recipe).frame(0); validFrame(at);
        CHECK(near(at.position_mm,expectedPositions[j],1e-10)); CHECK(near(at.normal,expectedNormals[j],1e-10)); ++j;
        recipe.set("phase_deg",J::makeNum(360*(distance-1e-7)/perimeter)); const auto before=RubberPatternPlan(recipe).frame(0);
        recipe.set("phase_deg",J::makeNum(360*(distance+1e-7)/perimeter)); const auto after=RubberPatternPlan(recipe).frame(0);
        CHECK(near(before.position_mm,after.position_mm,3e-7)); CHECK(near(before.normal,after.normal,3e-8));
        CHECK(near(before.tangent,after.tangent,3e-8));
    }
    updateSurface(recipe,"center_distance_mm",0); recipe.set("phase_deg",J::makeNum(0));
    auto circle=RubberPatternPlan(recipe).frame(0); validFrame(circle); CHECK(near(circle.position_mm,Vec3{0,13,0}));
    J s=sphere(); s.set("count",J::makeNum(RUBBER_PATTERN_MAX_COUNT));
    const RubberPatternPlan sp(s); validFrame(sp.frame(0)); validFrame(sp.frame(sp.count()-1));
    CHECK(sp.frame(0).normal.z<1 && sp.frame(sp.count()-1).normal.z>-1);
    updateSurface(s,"polar_end_deg",0.01); const RubberPatternPlan nearPole(s);
    validFrame(nearPole.frame(0)); CHECK(nearPole.frame(0).normal.z<1);

    J grid=json(R"({"surface":{"kind":"sphere","radius_mm":25},"distribution":"grid","around":48,"rows":24})");
    const RubberPatternPlan sphereGrid(grid); CHECK(sphereGrid.count()==48*24);
    for (uint32_t row=0;row<24;++row) for (uint32_t col=0;col<48;++col) {
        const auto f=sphereGrid.frame(row*48+col); validFrame(f);
        CHECK(near(f.v_rad,K_PI*(row+.5)/24)); CHECK(near(f.u_rad,K_2PI*col/48));
        CHECK(near(f.position_mm.len(),25)); CHECK(f.v_rad>0 && f.v_rad<K_PI);
        CHECK(near(f.normal,f.position_mm/25));
    }
    updateSurface(grid,"polar_start_deg",30); updateSurface(grid,"polar_end_deg",90);
    CHECK(near(RubberPatternPlan(grid).frame(0).v_rad,deg2rad(31.25)));
    CHECK(near(RubberPatternPlan(grid).frame(48*24-1).v_rad,deg2rad(88.75)));
    grid.set("around",J::makeNum(1)); grid.set("rows",J::makeNum(1000000));
    updateSurface(grid,"polar_start_deg",0); updateSurface(grid,"polar_end_deg",0.01);
    const auto tinyPolar=RubberPatternPlan(grid).frame(0); validFrame(tinyPolar);
    CHECK(tinyPolar.v_rad>0); CHECK(std::hypot(tinyPolar.normal.x,tinyPolar.normal.y)>0);
}

void invalidInputChecks() {
    rejects([]{ RubberPatternPlan p(J{}); });
    const char* invalidRecipes[]={
        R"({"surface":{"kind":"cone"},"around":3,"rows":4})",
        R"({"surface":{"kind":"cylinder","radius_mm":0,"length_mm":10},"around":3,"rows":4})",
        R"({"surface":{"kind":"torus","major_radius_mm":5,"radial_radius_mm":5,"axial_radius_mm":3},"around":3,"rows":4})",
        R"({"surface":{"kind":"sphere","radius_mm":10},"around":3,"rows":4})",
        R"({"surface":{"kind":"sphere","radius_mm":10},"count":3,"distribution":"grid"})",
        R"({"surface":{"kind":"sphere","radius_mm":10},"count":3,"rows":4})",
        R"({"surface":{"kind":"sphere","radius_mm":10,"polar_start_deg":70,"polar_end_deg":30},"count":3})",
        R"({"surface":{"kind":"sphere","radius_mm":10,"polar_end_deg":0.00001},"count":3})",
        R"({"surface":{"kind":"racetrack","radius_mm":10,"center_distance_mm":-1,"width_mm":10},"around":3,"rows":4})"
    };
    for (const char* r : invalidRecipes) rejects([&]{ RubberPatternPlan p(json(r)); });
    for (double bad : {0.,-1.,1.5,1000001.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        J recipe=cylinder(); recipe.set("around",J::makeNum(bad)); rejects([&]{ RubberPatternPlan p(recipe); });
    }
    J c=cylinder(); c.set("around",J::makeNum(1000000)); c.set("rows",J::makeNum(1000000)); rejects([&]{ RubberPatternPlan p(c); });
    c=cylinder(); c.set("count",J::makeNum(1)); rejects([&]{ RubberPatternPlan p(c); });
    c=cylinder(); c.set("distribution",J::makeStr("fibonacci")); rejects([&]{ RubberPatternPlan p(c); });
    c=cylinder(); c.set("turns",J::makeNum(1)); rejects([&]{ RubberPatternPlan p(c); });
    c=cylinder(); c.set("weave_deg",J::makeNum(20)); rejects([&]{ RubberPatternPlan p(c); });
    c=cylinder(); c.set("phase_deg",J::makeStr("3")); rejects([&]{ RubberPatternPlan p(c); });
    c=cylinder(); c.set("extra",J::makeBool(true)); rejects([&]{ RubberPatternPlan p(c); });
    c=cylinder(); c.obj.emplace_back("around",J::makeNum(2)); rejects([&]{ RubberPatternPlan p(c); });
    c=cylinder(); c.set("motif",J::makeStr("unknown")); rejects([&]{ RubberPatternPlan p(c); });
    c=cylinder(); c.set("distribution",J::makeStr("helix")); c.set("turns",J::makeNum(1.5)); rejects([&]{ RubberPatternPlan p(c); });
    c=cylinder(); updateSurface(c,"axial_start_mm",16); rejects([&]{ RubberPatternPlan p(c); });
    c=cylinder(); updateSurface(c,"axial_start_mm",10); updateSurface(c,"axial_end_mm",-10); rejects([&]{ RubberPatternPlan p(c); });
    c=torus(); updateSurface(c,"v_start_deg",-200); updateSurface(c,"v_end_deg",200); rejects([&]{ RubberPatternPlan p(c); });
    for (const char* placement : {R"({"x_axis":[2,0,0]})",R"({"x_axis":[0,0,1]})",R"({"x_axis":[1,0]})",R"({"scale":2})",R"({"origin_mm":[1000000001,0,0]})"}) {
        c=cylinder(); c.set("placement",json(placement)); rejects([&]{ RubberPatternPlan p(c); });
    }
    bool boundsRejected=false; const RubberPatternPlan p(cylinder());
    try { p.frame(p.count()); } catch(const std::out_of_range&) { boundsRejected=true; } CHECK(boundsRejected);
    rejects([]{ previewRubberPattern(cylinder(),0,0); }); rejects([]{ previewRubberPattern(cylinder(),0,257); });
    rejects([]{ previewRubberPattern(cylinder(),std::numeric_limits<uint32_t>::max(),1); });
}

void boundedAccessAndContractChecks() {
    J small=sphere(), large=sphere(); small.set("count",J::makeNum(1)); large.set("count",J::makeNum(1000000));
    size_t before=allocations, bytesBefore=allocatedBytes; const RubberPatternPlan smallPlan(small); const size_t smallAllocations=allocations-before, smallBytes=allocatedBytes-bytesBefore;
    before=allocations; bytesBefore=allocatedBytes; const RubberPatternPlan largePlan(large); const size_t largeAllocations=allocations-before, largeBytes=allocatedBytes-bytesBefore;
    CHECK(smallAllocations==largeAllocations); CHECK(largeAllocations<16);
    CHECK(smallBytes==largeBytes); CHECK(largeBytes<2048);
    before=allocations;
    const auto last=largePlan.frame(999999); const auto first=largePlan.frame(0); const auto middle=largePlan.frame(500001);
    const size_t frameAllocations=allocations-before;
    CHECK(frameAllocations==0); validFrame(last); validFrame(first); validFrame(middle);
    CHECK(last.index==999999 && first.index==0 && middle.index==500001);
    const RubberPatternPlan copied=largePlan; CHECK(copied.count()==1000000); CHECK(near(copied.frame(999999).position_mm,last.position_mm));
    J page=previewRubberPattern(large,999998,256);
    CHECK(page.find("count")->num==1000000); CHECK(page.find("returned")->num==2); CHECK(page.find("next_offset")->isNull());
    CHECK(page.find("frames")->arr.size()==2); CHECK(page.find("frames")->arr[0].find("index")->num==999998);
    page=previewRubberPattern(large,1000000,1); CHECK(page.find("frames")->arr.empty());
    page=previewRubberPattern(large,0,256); CHECK(page.find("returned")->num==256); CHECK(page.find("next_offset")->num==256);
    J output; CHECK(arkjson::parse(page.dump(),output)); CHECK(output.find("frames")->arr.size()==256);
    const J capabilities=rubberPatternCapabilities(); CHECK(capabilities.find("capability")->str=="rubber.pattern.indexed.v1");
    CHECK(capabilities.find("schema")->t==J::Obj);
    for (const auto& example : capabilities.find("examples")->arr) { RubberPatternPlan p(example); validFrame(p.frame(p.count()-1)); }
    const J diagnostics=largePlan.diagnostics(); CHECK(!diagnostics.find("overlap_checked")->b); CHECK(diagnostics.find("surface_binding_required")->b);
}
} // namespace

int main() {
    try {
        surfaceAndDistributionChecks(); coverageAndFrameChecks(); racetrackJunctionsAndPoles();
        invalidInputChecks(); boundedAccessAndContractChecks();
        std::cout << "rubber_patterns_tests: " << checks << " checks passed (A.02/B.04/D.02 local mathematical/contract evidence)\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << "rubber_patterns_tests failed: " << e.what() << '\n'; return 1; }
}
