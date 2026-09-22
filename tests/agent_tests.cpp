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
    CHECK(!documentFromJson(R"({"version":9,"features":[]})",invalid,&error));
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
    CHECK(AgentSession::tools().arr.size()==18);
    std::cout<<checks<<" checks / "<<failures<<" failures\n";return failures?1:0;
}
