#include "ark/agent.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/io.hpp"
#include "ark/version.hpp"
#include <iostream>
#include <cstdio>
using namespace ark;using V=arkjson::Value;
int failures=0,checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';++failures;}}while(0)
V json(const char* s){V v;CHECK(arkjson::parse(s,v));return v;}
bool failed(const V& v){return v.find("isError")->b;}
#include "object_context_tests.inl"
// Contract audit against the advertised JSON-schema subset, not a permissive
// proxy for backend dispatch. The examples must name advertised typed fields.
bool schemaAccepts(const V& schema,const V& value){
    auto typeMatches=[&](const std::string& type){return type=="null"?value.t==V::Null:type=="boolean"?value.t==V::Bool:type=="string"?value.t==V::Str:type=="array"?value.t==V::Arr:type=="object"?value.t==V::Obj:type=="number"?value.t==V::Num:type=="integer"?value.t==V::Num&&value.num==std::floor(value.num):false;};
    if(const auto* type=schema.find("type")){bool match=type->t==V::Str?typeMatches(type->str):false;if(type->t==V::Arr)for(const auto& item:type->arr)match|=typeMatches(item.str);if(!match)return false;}
    if(const auto* excluded=schema.find("not");excluded&&schemaAccepts(*excluded,value))return false;
    if(const auto* choices=schema.find("oneOf")){int n=0;for(const auto& option:choices->arr)n+=schemaAccepts(option,value);if(n!=1)return false;}
    if(const auto* choices=schema.find("anyOf")){bool match=false;for(const auto& option:choices->arr)match|=schemaAccepts(option,value);if(!match)return false;}
    if(const auto* choices=schema.find("enum")){bool match=false;for(const auto& option:choices->arr)match|=option.dump()==value.dump();if(!match)return false;}
    if(value.t==V::Num){if(const auto* n=schema.find("minimum");n&&value.num<n->num)return false;if(const auto* n=schema.find("maximum");n&&value.num>n->num)return false;}
    if(value.t==V::Str){if(const auto* n=schema.find("minLength");n&&value.str.size()<n->num)return false;if(const auto* n=schema.find("maxLength");n&&value.str.size()>n->num)return false;}
    if(value.t==V::Arr){if(const auto* n=schema.find("minItems");n&&value.arr.size()<n->num)return false;if(const auto* n=schema.find("maxItems");n&&value.arr.size()>n->num)return false;if(const auto* items=schema.find("items"))for(const auto& item:value.arr)if(!schemaAccepts(*items,item))return false;}
    if(value.t==V::Obj){
        if(const auto* required=schema.find("required"))for(const auto& key:required->arr)if(!value.find(key.str.c_str()))return false;
        const auto* props=schema.find("properties");const auto* extra=schema.find("additionalProperties");
        for(const auto& item:value.obj){const auto* definition=props?props->find(item.first.c_str()):nullptr;if(definition){if(!schemaAccepts(*definition,item.second))return false;}else if(extra){if(extra->t==V::Bool&&!extra->b)return false;if(extra->t==V::Obj&&!schemaAccepts(*extra,item.second))return false;}}
    }return true;
}
void testDocInfoDiscoveryContract(){
    AgentSession session;
    for(bool detailed:{false,true})for(const auto& tool:AgentSession::tools(detailed).arr)if(tool.find("name")->str=="doc_info"){
        const auto& schema=*tool.find("inputSchema");
        for(const char* input:{R"({})",R"({"detail":"summary"})",R"({"detail":"design"})",R"({"sections":["features"]})",R"({"detail":"design","sections":["assembly"]})"}){
            const auto args=json(input);CHECK(schemaAccepts(schema,args));CHECK(!failed(session.call("doc_info",args)));
        }
        for(const char* input:{R"({"detail":"summary","ids":[1]})",R"({"detail":"summary","sections":["features"]})",R"({"detail":"summary","ids":[1],"sections":["features"]})",R"({"detail":null})",R"({"detail":"unknown"})"}){
            const auto args=json(input);CHECK(!schemaAccepts(schema,args));const auto answer=session.call("doc_info",args);CHECK(failed(answer));
            const auto* receipt=answer.find("structuredContent");CHECK(receipt&&receipt->find("error_code")->str=="INVALID_ARGUMENT"&&receipt->find("stage")->str=="arguments"&&receipt->find("document_unchanged")->b);
        }
        CHECK(schemaAccepts(schema,json(R"({"ids":[1]})")));CHECK(schemaAccepts(schema,json(R"({"detail":"design","ids":[1]})")));
    }
}
void testDocApplyDiscoveryContract(){
    V advertised,previewSchema;
    for(const auto& tool:AgentSession::tools().arr){if(tool.find("name")->str=="doc_apply")advertised=*tool.find("inputSchema");if(tool.find("name")->str=="doc_preview")previewSchema=*tool.find("inputSchema");}
    CHECK(advertised.t==V::Obj);if(advertised.t!=V::Obj)return;
    const auto& command=*advertised.find("properties")->find("commands")->find("items");const auto& props=*command.find("properties");
    for(const char* key:{"id","p","type","name","inputs","expr","sketch","plane","parent","feature","a","b","value","reference_a","reference_b","driver","mate","mode","delta"})CHECK(props.find(key));
    CHECK(command.find("additionalProperties")&&command.find("additionalProperties")->b);
    const auto* examples=command.find("examples");CHECK(examples&&examples->arr.size()>=6);
    if(examples)for(const auto& example:examples->arr){for(const auto& item:example.obj)CHECK(props.find(item.first.c_str()));CHECK(schemaAccepts(command,example));}
    CHECK(!schemaAccepts(command,json(R"({"op":"feature.edit","id":{"feature":1},"p":{"dx":10}})")));
    CHECK(!schemaAccepts(command,json(R"({"op":"feature.edit","id":1,"p":[10]})")));
    CHECK(!schemaAccepts(command,json(R"({"op":"feature.add","type":"box","inputs":[{}]})")));
    CHECK(previewSchema.find("properties")->find("commands")->find("items")->dump()==command.dump());
    const auto& conditions=*advertised.find("properties")->find("checks");
    CHECK(previewSchema.find("properties")->find("checks")->dump()==conditions.dump());
    auto validChecks=json(R"([{"id":"count","query":{"tool":"doc_info","args":{},"path":"feature_count"},"op":"eq","value":1}])");
    CHECK(schemaAccepts(conditions,validChecks));
    auto misplacedPath=json(R"([{"id":"count","query":{"tool":"doc_info","args":{}},"path":"feature_count","op":"eq","value":1}])");
    CHECK(!schemaAccepts(conditions,misplacedPath));
    CHECK(!schemaAccepts(conditions,json(R"([{"query":{"tool":"doc_info","args":{},"path":"feature_count"},"op":"equals","value":1}])")));
    CHECK(!schemaAccepts(conditions,json(R"([{"id":1,"query":{"tool":"doc_info","args":{},"path":"feature_count"},"op":"eq","value":1}])")));
    CHECK(!schemaAccepts(conditions,json(R"([{"query":{"tool":"doc_info","args":{},"path":"feature_count"},"op":"eq"}])")));
    CHECK(!schemaAccepts(conditions,json(R"([{"query":{"tool":"doc_apply","args":{},"path":"feature_count"},"op":"eq","value":1}])")));
    AgentSession session;
    auto batch=json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":20},{"op":"feature.add","type":"box","name":"Base","p":{"dx":"width","dy":30,"dz":10}},{"op":"feature.edit","id":"Base","p":{"dx":25}},{"op":"sketch.set","name":"Profile","sketch":{"plane":"XY","entities":[{"kind":"circle","name":"Ring","dof":[0,0,10]}]}},{"op":"assembly.add","name":"Placed base","feature":"Base","parent":0,"p":{"tx":10}}]})");
    CHECK(schemaAccepts(advertised,batch));CHECK(!failed(session.call("doc_apply",batch)));CHECK(session.document.features.size()==1&&session.document.features[0].p.at("dx")=="25"&&session.document.assembly.size()==1);
    const auto baseline=documentToJson(session.document);auto missing=session.call("doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"feature.edit","entity":{"feature":1},"p":{"dx":90}}]})"));CHECK(failed(missing)&&missing.dump().find("Missing field: id")!=std::string::npos&&documentToJson(session.document)==baseline);
    auto malformed=session.call("doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"width","exprValue":99}]})"));CHECK(failed(malformed)&&malformed.dump().find("Missing field: expr")!=std::string::npos&&session.revision==1&&documentToJson(session.document)==baseline);
    auto previewArgs=json(R"({"expected_revision":1,"commands":[{"op":"feature.edit","id":1,"p":{"dx":30}}]})");previewArgs.set("checks",misplacedPath);auto refused=session.call("doc_apply",previewArgs);CHECK(failed(refused)&&documentToJson(session.document)==baseline&&session.revision==1);
    previewArgs.set("checks",validChecks);CHECK(schemaAccepts(previewSchema,previewArgs));auto preview=session.call("doc_preview",previewArgs);CHECK(!failed(preview));
    if(!failed(preview)){auto apply=*preview.find("structuredContent")->find("apply");CHECK(schemaAccepts(advertised,apply));CHECK(!failed(session.call("doc_apply",apply))&&session.revision==2&&session.document.features[0].p.at("dx")=="30");}
#ifdef ARK_TEST_WITH_OCCT
    // Execute the advertised native add/edit examples themselves. Merely
    // accepting an unused p.dx key would not prove a native box was edited.
    V nativeAdd,nativeEdit;
    if(examples)for(const auto& example:examples->arr){
        if(example.find("type")&&example.find("type")->str=="native_solid"&&example.find("native")&&example.find("native")->find("op")->str=="box")nativeAdd=example;
        if(example.find("op")->str=="feature.edit"&&example.find("id")&&example.find("id")->str=="Native block")nativeEdit=example;
    }
    CHECK(nativeAdd.t==V::Obj&&nativeEdit.t==V::Obj);
    if(nativeAdd.t==V::Obj&&nativeEdit.t==V::Obj){
        const auto& initial=*nativeAdd.find("p");const auto& edited=*nativeEdit.find("p");
        CHECK(initial.find("width")&&initial.find("depth")&&initial.find("height")&&!initial.find("dx")&&edited.find("height")&&!edited.find("dx"));
        AgentSession native;V args=V::makeObj(),commands=V::makeArr();commands.arr.push_back(nativeAdd);args.set("commands",commands);args.set("expected_revision",V::makeNum(0));CHECK(schemaAccepts(advertised,args));auto created=native.call("doc_apply",args);CHECK(!failed(created));
        if(failed(created))return;
        const auto* createdIds=created.find("structuredContent")->find("created_ids");CHECK(createdIds&&createdIds->t==V::Arr&&createdIds->arr.size()==1);if(!createdIds||createdIds->t!=V::Arr||createdIds->arr.size()!=1)return;
        V inspect=V::makeObj();inspect.set("id",createdIds->arr.front());auto before=native.call("native_inspect",inspect);CHECK(!failed(before));
        commands.arr[0]=nativeEdit;args.set("commands",commands);args.set("expected_revision",V::makeNum(1));CHECK(schemaAccepts(advertised,args));auto changed=native.call("doc_apply",args);CHECK(!failed(changed));auto after=native.call("native_inspect",inspect);CHECK(!failed(after));
        if(!failed(before)&&!failed(after)){
            const auto* first=before.find("structuredContent")->find("report");const auto* second=after.find("structuredContent")->find("report");CHECK(first&&second&&first->find("volume_mm3")&&second->find("volume_mm3"));
            if(first&&second&&first->find("volume_mm3")&&second->find("volume_mm3")){
                const auto initialVolume=initial.find("width")->num*initial.find("depth")->num*initial.find("height")->num;const auto editedVolume=initial.find("width")->num*initial.find("depth")->num*edited.find("height")->num;
                CHECK(std::fabs(first->find("volume_mm3")->num-initialVolume)<1e-8&&std::fabs(second->find("volume_mm3")->num-editedVolume)<1e-8&&std::fabs(initialVolume-editedVolume)>1);
            }
        }
    }
#endif
}
int main(){
    cpu_detect();
    testObjectContext();
    benchmarkObjectContext();
    testDocInfoDiscoveryContract();testDocApplyDiscoveryContract();
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
    auto releaseOnboard=s.call("agent_onboard",V::makeObj());CHECK(!failed(releaseOnboard));
    auto* releaseIdentity=releaseOnboard.find("structuredContent");CHECK(releaseIdentity);
    if(releaseIdentity){auto* version=releaseIdentity->find("version"),*code=releaseIdentity->find("iteration_code");CHECK(version&&version->asStr()==ARK_APP_VERSION);CHECK(code&&code->asStr()==ARK_ITERATION_CODE);}
    auto result=s.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":"20"},{"op":"feature.add","type":"box","name":"Base","p":{"dx":"width","dy":30,"dz":10}}]})"));
    CHECK(!failed(result));CHECK(s.revision==1&&s.document.features.size()==1);
    auto before=documentToJson(s.document);
    result=s.call("doc_apply",json(R"({"commands":[{"op":"param.set","name":"width","expr":"40"},{"op":"feature.add","type":"transform","inputs":[999]}]})"));
    CHECK(failed(result));CHECK(s.revision==1&&documentToJson(s.document)==before);
    CHECK(failed(s.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":"40"}]})"))));
    CHECK(!failed(s.call("doc_apply",json(R"({"commands":[{"op":"param.set","name":"width","expr":"40"}]})"))));
    CHECK(s.document.getParam("width")==40);
    for(const auto* mutation:{"doc_undo","doc_redo","doc_load","import_step","import_sheet_step"}){
        const auto frozen=documentToJson(s.document);const auto frozenRevision=s.revision;
        auto stale=s.call(mutation,json(R"({"expected_revision":0,"path":"missing.ark"})"));
        CHECK(failed(stale)&&stale.dump().find("Revision conflict")!=std::string::npos);
        CHECK(s.revision==frozenRevision&&documentToJson(s.document)==frozen);
    }
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
    CHECK(documentFromJson(R"({"version":14,"features":[]})",invalid,&error));
    CHECK(documentFromJson(R"({"version":30,"features":[]})",invalid,&error));
    CHECK(documentFromJson(R"({"version":31,"features":[]})",invalid,&error));
    CHECK(documentFromJson(R"({"version":32,"features":[]})",invalid,&error));
    CHECK(!documentFromJson(R"({"version":33,"features":[]})",invalid,&error));
    CHECK(error.find("unsupported document version")!=std::string::npos);
    CHECK(!documentFromJson(R"({"version":13,"features":[],"sketches":[{"name":"Plate","entities":[{"kind":"wire","name":"Outline","dof":[0,0,10,0,10,10,0,10,0,0],"segments":[{"kind":"line"},{"kind":"line"},{"kind":"line"},{"kind":"line"}]}],"corners":[{"entity":"Outline","corner":1,"kind":"fillet","size":"2"}]}]})",invalid,&error));
    CHECK(error.find("sketch fillets, sketch chamfers, or mates across assembly groups require document version 14")!=std::string::npos);
    CHECK(!documentFromJson(R"({"version":13,"features":[{"id":1,"type":"box","name":"Cube","op":0,"inputs":[],"p":{"dx":"10","dy":"10","dz":"10"}}],"assembly":[{"id":1,"parent":0,"feature":0,"name":"A"},{"id":2,"parent":0,"feature":0,"name":"B"},{"id":3,"parent":1,"feature":1,"name":"P","fixed":true},{"id":4,"parent":2,"feature":1,"name":"Q"}],"mates":[{"id":1,"a":3,"b":4,"type":"coincident","reference_a":"origin","reference_b":"origin","value":"0"}]})",invalid,&error));
    CHECK(error.find("document version 14")!=std::string::npos);
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
        V saved;CHECK(arkjson::parse(documentToJson(plane.document,true,false),saved));
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
        V saved;CHECK(arkjson::parse(documentToJson(box.document,true,false),saved));
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
        CHECK(arkjson::parse(documentToJson(box.document,true,false),saved)&&saved.find("version")->num==1);
    }
    {
        AgentSession sketch;
        CHECK(!failed(sketch.call("doc_apply",json(R"({"commands":[{"op":"sketch.set","name":"S","sketch":{"entities":[{"kind":"line","name":"Profile","dof":[0,0,10,0]},{"kind":"offset","name":"Copy","source":"Profile","distance":2}]}}]})"))));
        auto copy=sketch.call("sketch_inspect",json(R"({"name":"S","entity":"Copy"})"));
        CHECK(!failed(copy));
        const auto* payload=copy.find("structuredContent");
        CHECK(std::fabs(payload->find("length_mm")->num-10)<1e-6);
        CHECK(std::fabs(payload->find("bbox_mm")->arr[1].num-2)<1e-6&&std::fabs(payload->find("bbox_mm")->arr[3].num-2)<1e-6);
        V saved;CHECK(arkjson::parse(documentToJson(sketch.document,true,false),saved)&&saved.find("version")->num==13);
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
    {
        AgentSession plate;
        CHECK(!failed(plate.call("doc_apply",json(R"({"commands":[
            {"op":"param.set","name":"r","expr":"2"},
            {"op":"sketch.set","name":"Plate","sketch":{"tolerance":0.005,"entities":[{"kind":"wire","name":"Outline","start":[0,0],"segments":[{"kind":"line","to":[10,0]},{"kind":"line","to":[10,10]},{"kind":"line","to":[0,10]},{"kind":"line","to":[0,0]}]}]}},
            {"op":"sketch.fillet","name":"Plate","entity":"Outline","corner":0,"radius":"r"},
            {"op":"sketch.fillet","name":"Plate","entity":"Outline","corner":1,"radius":"r"},
            {"op":"sketch.fillet","name":"Plate","entity":"Outline","corner":2,"radius":"r"},
            {"op":"sketch.fillet","name":"Plate","entity":"Outline","corner":3,"radius":"r"}
        ]})"))));
        auto measured=plate.call("sketch_inspect",json(R"({"name":"Plate","entity":"Outline"})"));
        CHECK(!failed(measured));
        const double fillet=40-16+4*3.141592653589793;
        CHECK(std::fabs(measured.find("structuredContent")->find("length_mm")->num-fillet)<0.05);
        const auto& box=measured.find("structuredContent")->find("bbox_mm")->arr;
        CHECK(std::fabs(box[0].num)<0.02&&std::fabs(box[1].num)<0.02&&std::fabs(box[2].num-10)<0.02&&std::fabs(box[3].num-10)<0.02);
        V saved;CHECK(arkjson::parse(documentToJson(plate.document,true,false),saved)&&saved.find("version")->num==14);
        CHECK(!failed(plate.call("doc_apply",json(R"({"commands":[{"op":"param.set","name":"r","expr":"4"}]})"))));
        measured=plate.call("sketch_inspect",json(R"({"name":"Plate","entity":"Outline"})"));
        CHECK(std::fabs(measured.find("structuredContent")->find("length_mm")->num-(40-32+8*3.141592653589793))<0.05);
        std::string kept=documentToJson(plate.document);
        auto oversized=plate.call("doc_apply",json(R"({"commands":[{"op":"sketch.fillet","name":"Plate","entity":"Outline","corner":1,"radius":"20"}]})"));
        CHECK(failed(oversized)&&oversized.find("structuredContent")->find("error")->str.find("Fillet radius does not fit the corner")!=std::string::npos);
        CHECK(documentToJson(plate.document)==kept);
    }
    {
        AgentSession plate;
        CHECK(!failed(plate.call("doc_apply",json(R"({"commands":[
            {"op":"sketch.set","name":"Plate","sketch":{"tolerance":0.005,"entities":[{"kind":"wire","name":"Outline","start":[0,0],"segments":[{"kind":"line","to":[10,0]},{"kind":"line","to":[10,10]},{"kind":"line","to":[0,10]},{"kind":"line","to":[0,0]}]}]}},
            {"op":"sketch.chamfer","name":"Plate","entity":"Outline","corner":0,"distance":2},
            {"op":"sketch.chamfer","name":"Plate","entity":"Outline","corner":1,"distance":2},
            {"op":"sketch.chamfer","name":"Plate","entity":"Outline","corner":2,"distance":2},
            {"op":"sketch.chamfer","name":"Plate","entity":"Outline","corner":3,"distance":2}
        ]})"))));
        auto measured=plate.call("sketch_inspect",json(R"({"name":"Plate","entity":"Outline"})"));
        CHECK(!failed(measured));
        CHECK(std::fabs(measured.find("structuredContent")->find("length_mm")->num-(24+8*std::sqrt(2.0)))<0.05);
        auto open=plate.call("doc_apply",json(R"({"commands":[{"op":"sketch.set","name":"Open","sketch":{"entities":[{"kind":"wire","name":"Path","start":[0,0],"segments":[{"kind":"line","to":[10,0]},{"kind":"line","to":[10,10]}]}]}},{"op":"sketch.fillet","name":"Open","entity":"Path","corner":0,"radius":1}]})"));
        CHECK(failed(open)&&open.find("structuredContent")->find("error")->str.find("Fillet corner is not between two edges")!=std::string::npos);
    }
    {
        AgentSession dense;
        CHECK(!failed(dense.call("doc_apply",json(R"({"commands":[{"op":"feature.add","type":"box","name":"Cube","p":{"dx":10,"dy":10,"dz":10,"density":-1}}]})"))));
        V saved;CHECK(arkjson::parse(documentToJson(dense.document,true,false),saved)&&saved.find("version")->num==1);
        auto inspect=dense.call("native_inspect",json(R"({"id":1})"));
        CHECK(failed(inspect)&&inspect.find("structuredContent")->find("error")->str.find("Density must be a positive finite number of grams per cubic centimetre")!=std::string::npos);
    }
    CHECK(AgentSession::tools().arr.size()==73);
    std::cout<<checks<<" checks / "<<failures<<" failures\n";return failures?1:0;
}
