#include "ark/source_tube.hpp"
#include "ark/serialize.hpp"
#include "ark/mesh_analysis.hpp"
#include <iostream>
using namespace ark;
int main(int argc,char** argv){
    int checks=0;auto check=[&](bool ok,const char* m){++checks;if(!ok)throw std::runtime_error(m);};
    SourceTubePreview preview;SourceBend b;b.start={100,.75,17.5};b.axis={0,0,1};b.outward={0,-1,0};b.widthAxis={0,0,1};b.width=35;b.wall=1.5;b.neutralRadius=4.25;b.angle=K_HALF_PI;b.length=b.neutralRadius*b.angle;preview.bends={b};
    auto straight=sourceFoldTransforms(preview,0),folded=sourceFoldTransforms(preview,1);
    check(straight.size()==2&&straight[1].transformPoint({300,20,5}).approx({300,20,5},1e-10),"Straight transform identity");
    check(folded[1].transformPoint(b.start+Vec3{b.length,0,0}).approx({104.25,5,17.5},1e-10),"Finite-radius endpoint");
    check(folded[1].transformDir({1,0,0}).approx({0,1,0},1e-10),"Finite-radius tangent");
    for(double phase:{0.,.1,.5,1.}){
        auto m=sourceHingeMesh(b,phase);auto audit=analyzeMesh(m);check(audit.closedOriented(),"Hinge mesh topology");
        double exact=b.length*b.wall*b.width;check(std::fabs(audit.signedVolume-exact)<exact*.001,"Tessellated hinge volume");
        auto pose=sourceFoldTransforms(preview,phase)[1];size_t last=m.vertCount()-4;
        for(int k=0;k<4;++k){Vec3 start{m.pos[3*k],m.pos[3*k+1],m.pos[3*k+2]},end{m.pos[3*(last+k)],m.pos[3*(last+k)+1],m.pos[3*(last+k)+2]};
            check(pose.transformPoint(start+Vec3{b.length,0,0}).approx(end,2e-5),"Hinge/rigid interface tears during folding");}
    }
    if(argc>1){
        Document d;std::string error;check(loadDocument(argv[1],d,&error),"Source document load");
        for(auto& feature:d.features)if(feature.type==FeatureType::SourceTube){
            auto p=buildSourceTubePreview(feature.sourceData);auto poses=sourceFoldTransforms(p,1);auto& report=p.report;Mat4 base;
            for(int j=0;j<16;++j)base.m[j]=report.find("root_from_source")->arr[j].num;
            for(size_t i=0;i<poses.size();++i){Mat4 originalToFlat;for(int j=0;j<16;++j)originalToFlat.m[j]=report.find("source_to_flat")->arr[i].arr[j].num;
                for(auto point:std::vector<Vec3>{{0,0,0},{1000,0,0},{0,1000,0},{0,0,1000}})check((poses[i]*originalToFlat).transformPoint(point).approx(base.transformPoint(point),1e-6),"Native folding differs from verified source transform");}
            for(auto& h:p.holes.arr){auto& flat=h.find("flat_center")->arr;auto& original=h.find("source_center")->arr;auto owner=(size_t)h.find("segment")->num;
                Vec3 a{flat[0].num,flat[1].num,flat[2].num},source{original[0].num,original[1].num,original[2].num};
                check((base.inverted()*poses[owner]).transformPoint(a).approx(source,1e-6),"Native hole projection differs from STEP source");}
            check(documentToJson(d).size()<4*1024*1024,"Source document exceeds persistence limit");
        }
    }
    std::cout<<checks<<" source tube checks passed\n";
}
