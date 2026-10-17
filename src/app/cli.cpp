// ARK-Point :: ark-cli — headless demo: builds a parametric part, exports,
// snapshots, benchmarks. Also serves as an integration smoke test.
#include "ark/model.hpp"
#include "ark/io.hpp"
#include "ark/project_memory_tools.hpp"
#include <cstdio>

using namespace ark;

int main(int argc,char** argv){
    if(argc>1&&std::string(argv[1])=="--memory")return ark::projectMemoryCli(argc-2,argv+2);
    const char* dir = argc>1?argv[1]:".";
    cpu_detect();
    printf("ARK-Point kernel demo | avx2=%d fma=%d threads=%u\n",
           g_has_avx2,g_has_fma,ThreadPool::get().workers());

    Document doc;
    doc.setParam("plate","60");
    doc.setParam("bore","plate/6");          // expression dependency

    u64 base=doc.addFeature([&]{ Feature f; f.type=FeatureType::Box; f.name="base";
        f.p={{"dx","plate"},{"dy","40"},{"dz","8"}}; return f; }());
    (void)base;
    u64 boss=doc.addFeature([&]{ Feature f; f.type=FeatureType::Cylinder; f.name="boss";
        f.p={{"r","9"},{"h","22"}}; return f; }());
    doc.addFeature([&]{ Feature f; f.type=FeatureType::Transform; f.name="place-boss";
        f.inputs={boss}; f.p={{"tx","plate/2"},{"ty","20"},{"tz","8"}}; return f; }());
    u64 holeC=doc.addFeature([&]{ Feature f; f.type=FeatureType::Cylinder; f.name="hole-blank";
        f.p={{"r","bore"},{"h","50"}}; return f; }());
    doc.addFeature([&]{ Feature f; f.type=FeatureType::Transform; f.name="place-hole";
        f.inputs={holeC}; f.p={{"tx","plate/2"},{"ty","20"},{"tz","-5"}}; return f; }());

    doc.recompute();
    printf("recompute: %.2f ms\n",doc.recomputeMs);
    for(auto&r:doc.lastResults)
        printf("  [%llu] %s (%.2f ms)%s\n",(unsigned long long)r.id,
               r.ok?"ok":"FAIL", r.ms, r.ok?"":(": "+r.error).c_str());

    // boolean cut of last two bodies (placed hole vs placed boss assembly)
    // history: [0]=box [1]=cyl(boss) [2]=xform-boss [3]=cyl(hole) [4]=xform-hole
    size_t n=doc.evalBodies.size();
    if(n>=4){
        u64 a=doc.evalBodies[0].id, b=doc.evalBodies[n-2].id, c=doc.evalBodies[n-1].id;
        (void)b;
        Feature cutF; cutF.type=FeatureType::Boolean; cutF.name="cut"; cutF.inputs={a,c}; cutF.opInt=1;
        doc.addFeature(std::move(cutF));
        doc.recompute();
        printf("boolean recompute: %.1f ms | bodies=%zu tris=%zu\n",
               doc.recomputeMs,n+1,doc.totalTris());
    }

    Mesh all;
    for(auto&b:doc.evalBodies) all.append(b.tessellate(doc.tessOpt));

    std::string stl=std::string(dir)+"/demo_out.stl";
    std::string obj=std::string(dir)+"/demo_out.obj";
    std::string png=std::string(dir)+"/demo_snap.png";
    exportSTL(stl.c_str(),all); exportOBJ(obj.c_str(),all);
    SnapCam cam; cam.dist=all.computeBBox().diagonal()*2.4;
    Image img=renderSnapshot(all,1024,768,cam);
    writePNG(png.c_str(),img);

    printf("closed=%d euler=%lld tris=%zu\n",meshIsClosed(all)?1:0,
           (long long)meshEulerCharacteristic(all),all.triCount());
    printf("wrote %s / %s / %s\n",stl.c_str(),obj.c_str(),png.c_str());
    return 0;
}
