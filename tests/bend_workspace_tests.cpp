#include "ark/agent.hpp"
#include "ark/mesh_analysis.hpp"
#include <iostream>
using namespace ark;using V=arkjson::Value;
int main(){
    int checks=0;auto check=[&](bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);};
    V request;arkjson::parse(R"({"width_mm":40,"height_mm":30,"wall_mm":2,"length_mm":600,"hinge_offset_mm":1,"notches":[{"station_mm":180,"angle_deg":60,"hinge_face":"bottom"},{"station_mm":400,"angle_deg":90,"hinge_face":"top"}]})",request);
    for(auto face:{"bottom","top","left","right"}){
        V input=request,points=*request.find("notches");points.arr[0].set("hinge_face",V::makeStr(face));input.set("notches",points);
        auto preview=buildNotchPreview(input);auto report=analyzeMesh(preview.straight);
        std::cout<<face<<": tris="<<report.triangles<<" boundary="<<report.boundaryEdges<<" nonmanifold="<<report.nonManifoldEdges<<" winding="<<report.inconsistentEdges<<" volume="<<report.signedVolume<<'\n';
        check(report.closedOriented(),"Straight preview is not closed/oriented");
        double expected=preview.plan.find("validation")->find("remaining_volume_mm3")->num;
        check(std::fabs(report.signedVolume-expected)<expected*1e-6,"Preview volume differs from analytic plan");
        check(preview.segments.size()==3,"Segment count");double volume=0;
        for(auto& m:preview.segments){auto a=analyzeMesh(m);std::cout<<" segment "<<a.triangles<<" boundary "<<a.boundaryEdges<<" volume "<<a.signedVolume<<'\n';check(a.closedOriented(),"Segment mesh is not closed/oriented");volume+=a.signedVolume;}
        check(std::fabs(volume-expected)<expected*1e-6,"Segment volume partition");
        auto straight=notchFoldTransforms(preview,0),folded=notchFoldTransforms(preview,1),half=notchFoldTransforms(preview,.25);
        for(auto& m:straight)check(m.transformPoint({17,3,9}).approx({17,3,9},1e-10),"Zero pose identity");
        check(half[1].transformPoint({500,0,0}).approx(half[2].transformPoint({500,0,0}),1e-10),"Sequential animation activated second bend early");
        for(size_t i=0;i<preview.stations.size();++i){auto& j=preview.plan.find("notches")->arr[i];Mat4 frame;for(int k=0;k<16;++k)frame.m[k]=j.find("frame_column_major")->arr[k].num;
            double station=preview.stations[i],d=10,a=preview.angles[i]*.5;
            Vec3 left=frame.transformPoint({station-d*std::tan(a),1+d,0}),right=frame.transformPoint({station+d*std::tan(a),1+d,0});
            check(folded[i].transformPoint(left).approx(folded[i+1].transformPoint(right),1e-9),"Full fold cut faces do not meet");}
    }
    request.set("notches",V::makeArr());auto blank=buildNotchPreview(request);check(blank.segments.size()==1&&blank.stations.empty(),"Zero point tube");check(analyzeMesh(blank.straight).closedOriented(),"Zero point stock topology");
    AgentSession session;V args;arkjson::parse(R"({"commands":[{"op":"feature.add","type":"notched_tube","p":{"width_mm":40,"height_mm":30,"wall_mm":2,"length_mm":600,"hinge_offset_mm":1},"notches":[{"station":200,"angle":90,"face":"bottom"}]}]})",args);
    auto result=session.call("doc_apply",args);if(result.find("isError")->b)std::cout<<result.dump()<<'\n';check(!result.find("isError")->b,"Session feature creation");
    std::string json=documentToJson(session.document),error;Document loaded;check(documentFromJson(json,loaded,&error),"Document reload");check(json==documentToJson(loaded),"Document roundtrip");loaded.recompute();check(loaded.lastResults[0].ok,"Recompute reloaded laser tube");
    arkjson::parse(R"({"commands":[{"op":"feature.edit","id":1,"notches":[]}]})",args);check(!session.call("doc_apply",args).find("isError")->b,"Remove final point");
    check(!session.call("doc_undo",V::makeObj()).find("isError")->b&&session.document.features[0].notches.size()==1,"Undo removal");
    std::cout<<checks<<" bend workspace checks passed\n";
}
