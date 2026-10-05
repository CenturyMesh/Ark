#include "ark/agent.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/io.hpp"
#include <iostream>
#include <cstdio>
using namespace ark;using V=arkjson::Value;
int failures=0,checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';++failures;}}while(0)
V json(const char* s){V v;CHECK(arkjson::parse(s,v));return v;}
bool failed(const V& v){return v.find("isError")->b;}
int main(){
    cpu_detect();
    // Detect the old f64/f32 reinterpretation bug on arrays entering the batch path.
    std::vector<Vec3> in(128),out(128);Mat4 m=Mat4::translation({1000000.125,-12.3,8})*Mat4::rotZ(.73);
    for(size_t i=0;i<in.size();++i)in[i]={(double)i/7.,(double)i/11.,(double)i/13.};
    transform_points_f32(m,in.data(),out.data(),in.size());
    for(size_t i=0;i<in.size();++i)CHECK(out[i].approx(m.transformPoint(in[i]),1e-10));
    bool ok=false;
    CHECK(std::fabs(evalExpr("2e-3 + sin(deg(30)) ",{},&ok)-.502)<1e-12&&ok);
    for(auto text:{"sqrt(-1)","sin(2", "bogus(2)","1/0","sin(1,2)","1e999","1.2.3"}){evalExpr(text,{},&ok);CHECK(!ok);}
    Document chain;for(int i=20;i>=0;--i)chain.setParam("p"+std::to_string(i),i==0?"2":"p"+std::to_string(i-1)+"+1");
    CHECK(chain.parameterError.empty()&&chain.getParam("p20")==22);
    chain.setParam("p0","p20");CHECK(!chain.parameterError.empty());
    AgentSession s;
    auto result=s.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":"20"},{"op":"feature.add","type":"box","name":"Base","p":{"dx":"width","dy":30,"dz":10}}]})"));
    CHECK(!failed(result));CHECK(s.revision==1&&s.document.features.size()==1);
    auto before=documentToJson(s.document);
    result=s.call("doc_apply",json(R"({"commands":[{"op":"param.set","name":"width","expr":"40"},{"op":"feature.add","type":"transform","inputs":[999]}]})"));
    CHECK(failed(result));CHECK(s.revision==1&&documentToJson(s.document)==before);
    CHECK(failed(s.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":"40"}]})"))));
    CHECK(!failed(s.call("doc_apply",json(R"({"commands":[{"op":"param.set","name":"width","expr":"40"}]})"))));
    CHECK(s.document.getParam("width")==40);
    CHECK(!failed(s.call("doc_undo",V::makeObj()))&&s.document.getParam("width")==20);
    CHECK(!failed(s.call("doc_redo",V::makeObj()))&&s.document.getParam("width")==40);
    CHECK(!failed(s.call("doc_apply",json(R"({"commands":[{"op":"feature.add","type":"transform","inputs":[1],"p":{"tx":10}}]})"))));
    auto report=s.call("mesh_inspect",V::makeObj());auto data=report.find("structuredContent");
    CHECK(data->find("triangles")->num==12); // Only terminal output, not original+transformed.
    CHECK(data->find("closed_oriented_edges")->b);
    CHECK(std::fabs(std::fabs(data->find("signed_volume_mm3")->num)-12000)<1e-6);
    CHECK(!failed(s.call("doc_save",json(R"({"path":".test_roundtrip.ark"})"))));
    AgentSession loaded;CHECK(!failed(loaded.call("doc_load",json(R"({"path":".test_roundtrip.ark"})"))));
    CHECK(documentToJson(loaded.document)==documentToJson(s.document));std::remove(".test_roundtrip.ark");
    std::string error;Document invalid;
    CHECK(documentFromJson(R"({"version":9,"features":[]})",invalid,&error));
    CHECK(documentFromJson(R"({"version":10,"features":[]})",invalid,&error));
    CHECK(documentFromJson(R"({"version":11,"features":[]})",invalid,&error));
    CHECK(documentFromJson(R"({"version":12,"features":[]})",invalid,&error));
    CHECK(documentFromJson(R"({"version":13,"features":[]})",invalid,&error));
    CHECK(!documentFromJson(R"({"version":14,"features":[]})",invalid,&error));
    CHECK(error.find("unsupported document version")!=std::string::npos);
    CHECK(!documentFromJson(R"({"version":12,"features":[{"id":1,"type":"box","suppressed":true}]})",invalid,&error));
    CHECK(error.find("document version 13")!=std::string::npos);
    CHECK(!documentFromJson(R"({"version":12,"features":[],"sketches":[{"name":"S","entities":[{"kind":"project","name":"Edge","feature":1,"reference":"edge.x.ymin.zmin"}]}]})",invalid,&error));
    CHECK(error.find("plane canvases, suppressed features, or projected and offset sketch entities require document version 13")!=std::string::npos);
    CHECK(!documentFromJson(R"({"features":[{"id":1,"type":"box"},{"id":1,"type":"sphere"}]})",invalid,&error));
    Sketch sketch;
    CHECK(!sketchFromJson(R"({"entities":[{"kind":"line","dof":[1]}]})",sketch,&error));
    CHECK(!sketchFromJson(R"({"entities":[{"kind":"point","dof":[1,2]}],"constraints":[{"type":"horizontal","a":0}]})",sketch,&error));
    CHECK(!sketchFromJson(R"({"constraints":[{"type":"fixed","a":99}]})",sketch,&error));
    auto ray=loaded.call("mesh_raycast",json(R"({"origin":[-10,15,5],"direction":[1,0,0]})"));
    CHECK(!failed(ray)&&ray.find("structuredContent")->find("hit")->b);
    CHECK(std::fabs(ray.find("structuredContent")->find("distance_mm")->num-20)<1e-9);
    auto miss=loaded.call("mesh_raycast",json(R"({"origin":[-10,150,5],"direction":[1,0,0]})"));CHECK(!miss.find("structuredContent")->find("hit")->b);
    CHECK(failed(loaded.call("mesh_raycast",json(R"({"origin":[0,0,0],"direction":[0,0,0]})"))));
    CHECK(!failed(loaded.call("doc_apply",json(R"({"commands":[{"op":"feature.remove","id":2}]})"))));
    CHECK(!failed(loaded.call("doc_apply",json(R"({"commands":[{"op":"feature.add","type":"sphere","p":{"r":3}}]})"))));
    CHECK(loaded.document.features.back().id==3);
    V malformed;for(auto text:{"NaN","1e999","[01]","+1",R"({"a":1,"a":2})"})CHECK(!arkjson::parse(text,malformed));
    std::string deep(70,'[');deep+="0";deep+=std::string(70,']');CHECK(!arkjson::parse(deep,malformed));
    s.document=makeDemoDocument();++s.revision;CHECK(s.document.lastResults[0].ok);
    Mesh surface=s.document.evalBodies[0].tessellate({});auto audit=analyzeMesh(surface);
    CHECK(audit.vertices==160*64&&audit.triangles==160*64*2);
    CHECK(audit.closedOriented()&&audit.components==1&&audit.signedVolume>0);
    before=documentToJson(s.document);
    CHECK(failed(s.call("doc_apply",json(R"({"commands":[{"op":"feature.edit","id":1,"p":{"x":"u"}}]})"))));
    CHECK(documentToJson(s.document)==before);
    surface.idx.resize(surface.idx.size()-3);audit=analyzeMesh(surface);CHECK(!audit.closedOriented()&&audit.boundaryEdges==3);
    surface.idx[0]=9999999;CHECK(analyzeMesh(surface).invalidIndices==1);
    CHECK(!failed(s.call("eval_expr",json(R"({"expr":"radius*2"})"))));
    SnapCam camera;camera.dist=100;
    auto cube=makeBox({20,20,20}).tessellate({});
    auto rendered=renderSnapshot(cube,320,240,camera),background=renderSnapshot({},320,240,camera);
    size_t foreground=0;for(size_t i=0;i<rendered.px.size();i+=3)if(rendered.px[i]!=background.px[i]||rendered.px[i+1]!=background.px[i+1]||rendered.px[i+2]!=background.px[i+2])++foreground;
    CHECK(foreground>500&&foreground<20000); // Projection must not fill the frame.
    {
        AgentSession plane;
        CHECK(!failed(plane.call("doc_apply",json(R"({"commands":[{"op":"feature.add","type":"box","name":"Base","p":{"dx":10,"dy":10,"dz":10}},{"op":"canvas.set","name":"Photo","path":"missing-photo.png","plane":"XY","width":80,"height":40,"opacity":0.4}]})"))));
        auto mesh=plane.call("mesh_inspect",json(R"({"id":1})"));
        CHECK(!failed(mesh)&&std::fabs(std::fabs(mesh.find("structuredContent")->find("signed_volume_mm3")->num)-1000)<1e-6);
        V saved;CHECK(arkjson::parse(documentToJson(plane.document),saved));
        CHECK(saved.find("version")->num==13&&saved.find("canvases")&&saved.find("canvases")->arr.size()==1);
        const auto& canvas=saved.find("canvases")->arr[0];
        CHECK(canvas.find("path")->str=="missing-photo.png"&&canvas.find("plane")->str=="XY");
        CHECK(canvas.find("width")->str=="80"&&canvas.find("height")->str=="40"&&std::fabs(canvas.find("opacity")->num-0.4)<1e-9);
        auto design=plane.call("doc_info",json(R"({"detail":"design"})"));
        CHECK(!failed(design)&&design.find("structuredContent")->find("canvases")->arr[0].find("name")->str=="Photo");
    }
    {
        AgentSession box;
        CHECK(!failed(box.call("doc_apply",json(R"({"commands":[{"op":"feature.add","type":"box","name":"Base","p":{"dx":10,"dy":10,"dz":10}}]})"))));
        CHECK(!failed(box.call("doc_apply",json(R"({"commands":[{"op":"feature.edit","id":"Base","suppressed":true}]})"))));
        CHECK(box.document.features.size()==1&&box.document.evalBodies.empty()&&box.document.lastResults.size()==1&&box.document.lastResults[0].ok&&box.document.lastResults[0].error.empty());
        V saved;CHECK(arkjson::parse(documentToJson(box.document),saved));
        CHECK(saved.find("version")->num==13&&saved.find("features")->arr[0].find("suppressed")&&saved.find("features")->arr[0].find("suppressed")->b);
        auto design=box.call("doc_info",json(R"({"detail":"design"})"));
        CHECK(!failed(design)&&design.find("structuredContent")->find("features")->arr[0].find("suppressed")->b);
        std::string kept=documentToJson(box.document);
        auto blocked=box.call("doc_apply",json(R"({"commands":[{"op":"feature.add","type":"transform","inputs":["Base"],"p":{"tx":5}}]})"));
        CHECK(failed(blocked)&&blocked.find("structuredContent")->find("error")->str.find("source feature is suppressed")!=std::string::npos);
        CHECK(documentToJson(box.document)==kept);
        CHECK(!failed(box.call("doc_apply",json(R"({"commands":[{"op":"feature.edit","id":"Base","suppressed":false}]})"))));
        auto restored=box.call("mesh_inspect",json(R"({"id":1})"));
        CHECK(!failed(restored)&&std::fabs(std::fabs(restored.find("structuredContent")->find("signed_volume_mm3")->num)-1000)<1e-6);
        CHECK(arkjson::parse(documentToJson(box.document),saved)&&saved.find("version")->num==1);
    }
    {
        AgentSession sketch;
        CHECK(!failed(sketch.call("doc_apply",json(R"({"commands":[{"op":"sketch.set","name":"S","sketch":{"entities":[{"kind":"line","name":"Profile","dof":[0,0,10,0]},{"kind":"offset","name":"Copy","source":"Profile","distance":2}]}}]})"))));
        auto copy=sketch.call("sketch_inspect",json(R"({"name":"S","entity":"Copy"})"));
        CHECK(!failed(copy));
        const auto* payload=copy.find("structuredContent");
        CHECK(std::fabs(payload->find("length_mm")->num-10)<1e-6);
        CHECK(std::fabs(payload->find("bbox_mm")->arr[1].num-2)<1e-6&&std::fabs(payload->find("bbox_mm")->arr[3].num-2)<1e-6);
        V saved;CHECK(arkjson::parse(documentToJson(sketch.document),saved)&&saved.find("version")->num==13);
        std::string kept=documentToJson(sketch.document);
        auto patched=sketch.call("doc_apply",json(R"({"commands":[{"op":"sketch.patch","name":"S","entity":"Copy","anchor":0,"y":"4"}]})"));
        CHECK(failed(patched)&&patched.find("structuredContent")->find("error")->str.find("Project and offset entities are driven")!=std::string::npos);
        CHECK(documentToJson(sketch.document)==kept);
        auto onboard=sketch.call("agent_onboard",json(R"({"topic":"sketch"})"));
        CHECK(!failed(onboard));
        auto text=onboard.find("structuredContent")->dump();
        CHECK(text.find("canvas.set")!=std::string::npos&&text.find("version 13")!=std::string::npos);
    }
    {
        AgentSession square;
        CHECK(!failed(square.call("doc_apply",json(R"({"commands":[{"op":"sketch.set","name":"S","sketch":{"entities":[{"kind":"wire","name":"Profile","start":[0,0],"segments":[{"kind":"line","to":[10,0]},{"kind":"line","to":[10,10]},{"kind":"line","to":[0,10]},{"kind":"line","to":[0,0]}]},{"kind":"offset","name":"Copy","source":"Profile","distance":2}]}}]})"))));
        auto copy=square.call("sketch_inspect",json(R"({"name":"S","entity":"Copy"})"));
        CHECK(!failed(copy));
        const auto* payload=copy.find("structuredContent");
        const auto& box=payload->find("bbox_mm")->arr;
        CHECK(std::fabs(payload->find("length_mm")->num-24)<1e-6);
        CHECK(std::fabs(box[0].num-2)<1e-6&&std::fabs(box[1].num-2)<1e-6&&std::fabs(box[2].num-8)<1e-6&&std::fabs(box[3].num-8)<1e-6);
    }
    CHECK(AgentSession::tools().arr.size()==34);
    std::cout<<checks<<" checks / "<<failures<<" failures\n";return failures?1:0;
}
