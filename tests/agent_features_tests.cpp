#include "ark/agent.hpp"
#include "ark/sketch.hpp"
#include "ark/native_backend.hpp"
#include "ark/io.hpp"
#include <iostream>
#include <stdexcept>
#include <chrono>
using V=arkjson::Value;
using namespace ark;
void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
V parse(const char* text){V out;if(!arkjson::parse(text,out))throw std::runtime_error("Bad test JSON");return out;}
V accepted(V r){if(r.find("isError")->b)throw std::runtime_error(r.find("structuredContent")->dump());return *r.find("structuredContent");}
V call(AgentSession& s,const char* name,const char* json){return accepted(s.call(name,parse(json)));}
void close(double a,double b,double tol,const char* reason){check(std::abs(a-b)<=tol,reason);}
void testMath(){AgentSession s;
    auto tools=AgentSession::tools(),full=AgentSession::tools(true);check(tools.arr.size()==76,"tool discovery");check(tools.dump().size()<full.dump().size(),"compact default discovery");std::cout<<"tool_schema_bytes="<<tools.dump().size()<<" detailed_schema_bytes="<<full.dump().size()<<"\n";
    check(call(s,"agent_onboard",R"({"topic":"automation"})").find("command_reference"),"detailed contract remains discoverable");
    close(call(s,"math_eval",R"({"op":"convert","value":2,"from":"in","to":"mm"})").find("value")->num,50.8,1e-12,"inch conversion");
    close(call(s,"math_eval",R"({"op":"angle","a":[1,0,0],"b":[0,1,0]})").find("value")->num,90,1e-12,"vector angle");
    auto p=call(s,"math_eval",R"({"op":"line_plane","a":[1,2,3],"direction":[0,0,-1],"origin":[0,0,0],"normal":[0,0,1]})");close(p.find("value")->arr[2].num,0,1e-12,"line plane intersection");
    check(s.call("math_eval",parse(R"({"op":"normalize","a":[0,0,0]})")).find("isError")->b,"zero direction rejected");
    check(s.call("math_eval",parse(R"({"op":"convert","value":1,"from":"mm","to":"deg"})")).find("isError")->b,"incompatible units rejected");
}
void testTransactions(){AgentSession s;
    call(s,"doc_apply",R"({"commands":[{"op":"param.set","name":"w","expr":10},{"op":"feature.add","type":"box","name":"A","p":{"dx":"w","dy":4,"dz":3}},{"op":"feature.add","type":"box","name":"B","p":{"dx":7,"dy":2,"dz":3}}]})");
    std::string saved=documentToJson(s.document);u64 revision=s.revision;
    auto p=call(s,"doc_preview",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"w","expr":20}]})");check(p.find("valid")->b,"valid preview");check(saved==documentToJson(s.document)&&s.revision==revision,"preview isolation");
    auto invalid=call(s,"doc_preview",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"w","expr":-1}]})");check(!invalid.find("valid")->b,"invalid preview evidence");check(saved==documentToJson(s.document)&&s.revision==revision,"failed preview isolation");
    check(s.call("doc_preview",parse(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"w","expr":20}]})")).find("isError")->b,"stale preview rejected");
    auto r=call(s,"doc_apply",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"w","expr":20}]})");check(r.find("features_reused")->num==1&&r.find("features_evaluated")->num==1,"only dependent primitive rebuilt");
    Document fresh=s.document;fresh.evaluationCache.clear();fresh.recompute();close(fresh.evalBodies[0].bbox().mx.x,s.document.evalBodies[0].bbox().mx.x,1e-9,"cached/fresh parity");
    auto solve=call(s,"design_solve",R"({"expected_revision":2,"parameter":"w","lower":1,"upper":30,"target":21,"tolerance":0.000001,"objective":{"tool":"eval_expr","args":{"expr":"w*3"},"path":"value"}})");check(solve.find("converged")->b,"target convergence");close(solve.find("parameter_value")->num,7,1e-6,"target parameter");check(s.revision==2,"solver read only");
    auto sens=call(s,"design_solve",R"({"expected_revision":2,"parameter":"w","lower":19,"upper":21,"mode":"sensitivity","objective":{"tool":"eval_expr","args":{"expr":"w*w"},"path":"value"}})");close(sens.find("secant_derivative")->num,40,1e-9,"sensitivity");
    auto batch=call(s,"query_batch",R"({"queries":[{"tool":"doc_info","args":{}},{"tool":"eval_expr","args":{"expr":"w"}}]})");check(batch.find("results")->arr.size()==2,"read batch");
    auto compact=call(s,"query_batch",R"({"queries":[{"tool":"doc_info","args":{},"fields":["revision"]}]})");check(compact.find("results")->arr[0].find("structuredContent")->obj.size()==1,"compact field projection");
    check(s.call("query_batch",parse(R"({"queries":[{"tool":"doc_apply","args":{"commands":[]}}]})")).find("isError")->b,"batch mutation rejected");
    auto error=s.call("doc_apply",parse(R"({"commands":[{"op":"missing"}]})"));check(error.find("isError")->b&&error.find("structuredContent")->find("command_index")->num==0,"structured command error");
    call(s,"doc_undo",R"({})");check(s.document.getParam("w")==10,"preview did not pollute undo");
}
void testDerivatives(){
    Sketch sk;sk.addLine(1,2,5,7);sk.addLine(3,-1,9,2);sk.addCircle(8,6,3);sk.addPoint(-1,4);
    for(int kind=SketchCon::FixedX;kind<=SketchCon::Tangent;++kind){SketchCon c;c.type=(SketchCon::Type)kind;c.a=0;c.b=1;c.pa=0;c.pb=1;c.val=.7;c.val2=1.2;if(kind==SketchCon::Radius)c.a=2;if(kind==SketchCon::Tangent)c.b=2;sk.cons.push_back(c);}
    auto linear=linearizeSketch(sk);
    for(size_t column=0;column<linear.freeSlots.size();++column){size_t slot=linear.freeSlots[column],offset=0;Sketch plus=sk,minus=sk;double h=1e-5;
        for(size_t i=0;i<sk.ents.size();++i){if(slot<offset+sk.ents[i].dof.size()){plus.ents[i].dof[slot-offset]+=h;minus.ents[i].dof[slot-offset]-=h;break;}offset+=sk.ents[i].dof.size();}
        auto p=linearizeSketch(plus),m=linearizeSketch(minus);for(size_t row=0;row<linear.residuals.size();++row)close(linear.jacobianColumns[column][row],(p.residuals[row]-m.residuals[row])/(2*h),1e-7,"AD matches independent central differences");
    }
}
void testPlacementFrames(){
    AgentSession s;call(s,"doc_apply",R"({"commands":[{"op":"feature.add","type":"box","name":"Part"},{"op":"assembly.add","name":"Group","feature":0,"p":{"tx":10,"rz":90}},{"op":"assembly.add","name":"Child","feature":"Part","parent":"Group","p":{"tx":3}}]})");
    auto calculated=call(s,"math_eval",R"({"op":"frame","origin":[20,5,2],"z":[0,0,1],"x_hint":[0,1,0]})");V request=V::makeObj(),target=V::makeObj();request.set("expected_revision",V::makeNum(1));request.set("node",V::makeStr("Child"));target.set("frame",*calculated.find("frame"));request.set("target",target);V plan=accepted(s.call("placement_plan",request));check(plan.find("valid")->b,"nested placement preview");accepted(s.call("doc_apply",*plan.find("apply")));
    std::vector<AssemblyPlacement> placements;std::string error;check(resolveAssembly(s.document,placements,error),"nested placement resolve");for(const auto& p:placements)if(p.name=="Child")for(size_t i=0;i<16;++i)close(p.world.m[i],calculated.find("frame")->arr[i].num,1e-8,"nested local/world conversion");
    auto plane=call(s,"placement_plan",R"({"expected_revision":2,"node":"Child","target":{"plane":"XY"}})");check(plane.find("valid")->b,"datum target placement");
}
void testManufacturingDiscovery(){
    AgentSession s;
    Feature blank;blank.type=FeatureType::SheetMetal;blank.name="Uninformative name";blank.manufacturingRoute="sheet_metal";const auto origin=s.document.addFeature(blank);
    Feature placed;placed.type=FeatureType::NativeSolid;placed.name="Just a solid";placed.inputs={origin};placed.nativeSpec=parse(R"({"op":"transform"})");const auto installed=s.document.addFeature(placed);
    Feature cut;cut.type=FeatureType::NativeSolid;cut.name="Sheet label cannot prove sheet intent";cut.inputs={installed};cut.nativeSpec=parse(R"({"op":"hole"})");s.document.addFeature(cut);
    const auto lineage=featureManufacturingLineage(s.document,*s.document.find(installed));check(lineage.origin==origin&&lineage.editableSheet,"rigid placement retains authoring lineage");
    auto design=call(s,"doc_info",R"({"detail":"design"})");const auto& rows=design.find("features")->arr;
    check(rows[1].find("operation")->str=="transform"&&rows[1].find("inputs")->arr[0].num==origin,"operation and dependency discovery");
    check(rows[1].find("manufacturing")->find("sheet_origin")->num==origin,"placed sheet is discoverable without name inference");
    check(!rows[2].find("manufacturing")->find("sheet_origin"),"generic hole terminates editable blank lineage");
    s.document.features[2].manufacturingRoute="sheet_metal";std::string error;check(!validateManufacturingRoutes(s.document,error),"misdeclared sheet lineage rejects");
    s.document.features[2].manufacturingRoute="purchased";check(validateManufacturingRoutes(s.document,error),"supplier envelope is explicit intent");
    Feature sampled;sampled.type=FeatureType::Box;sampled.manufacturingRoute="machined";s.document.addFeature(sampled);check(!validateManufacturingRoutes(s.document,error),"machined route rejects legacy display-only solids");s.document.features.pop_back();
    Document direct;direct.addFeature(sampled);direct.recompute();check(!direct.parameterError.empty()&&direct.evalBodies.empty(),"direct C++ recompute enforces manufacturing intent before evaluating a legacy solid");
    direct.features[0].manufacturingRoute="purchased";direct.recompute();check(direct.parameterError.empty()&&!direct.lastResults.empty()&&direct.lastResults[0].ok&&!direct.evalBodies.empty(),"correcting direct C++ intent recovers normal recomputation");
    placed.p["op"]="rigid_transform";placed.nativeSpec=parse(R"({"op":"box"})");check(featureOperation(placed)=="transform","p.op takes authority and rigid_transform alias matches native evaluator");
    Document restored;check(documentFromJson(documentToJson(s.document),restored,&error),"manufacturing intent persistence");check(restored.features[0].manufacturingRoute=="sheet_metal"&&restored.features[2].manufacturingRoute=="purchased","routes survive persistence");
    call(s,"agent_onboard",R"({"topic":"manufacturing"})");
    AgentSession rejected;const auto revision=rejected.revision;
    auto invalid=rejected.call("doc_apply",parse(R"({"commands":[{"op":"param.set","name":"uncommitted","expr":20},{"op":"feature.add","type":"box","name":"Generic blank","manufacturing":{"route":"sheet_metal"}}]})"));
    check(invalid.find("isError")->b&&rejected.revision==revision&&rejected.document.features.empty()&&rejected.document.params.empty(),"route mismatch rolls back entire modeling batch");
    auto invalidPreview=call(rejected,"doc_preview",R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","manufacturing":{"route":"machined"}}]})");
    check(!invalidPreview.find("valid")->b&&!invalidPreview.find("candidate_id")&&rejected.revision==revision&&rejected.document.features.empty(),"invalid manufacturing route cannot create a cached candidate");
    auto validPreview=call(rejected,"doc_preview",R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","manufacturing":{"route":"purchased"}}]})");
    check(validPreview.find("valid")->b&&validPreview.find("candidate_cached")->b,"valid explicit supplier envelope can be prepared");
    auto committed=accepted(rejected.call("doc_apply",*validPreview.find("apply")));
    check(committed.find("applied_prepared")->b&&rejected.document.features[0].manufacturingRoute=="purchased","prepared commit retains validated intent without re-evaluation");
}
void testSelectedDesignReads(){
    AgentSession session;session.document.setParam("width","25");Sketch sketch;sketch.addCircle(0,0,3);sketch.ents[0].name="rim";session.document.setSketch("Profile",sketch);session.document.setSketch("Unrelated",sketch);
    Material used;used.name="Grey";session.document.materials.push_back(used);Material unused;unused.name="Unused";session.document.materials.push_back(unused);
    for(int index=0;index<154;++index){Feature feature;feature.name="Part "+std::to_string(index);feature.type=index?FeatureType::Box:FeatureType::Extrude;if(!index){feature.p={{"sketch","Profile"},{"height","2"}};feature.material="Grey";}session.document.addFeature(feature);}
    AssemblyNode node;node.id=1;node.name="Placed";node.feature=session.document.features[0].id;session.document.assembly.push_back(node);session.document.recompute();
    const auto revision=session.revision;const auto before=documentToJson(session.document);const auto full=call(session,"doc_info",R"({"detail":"design"})");
    check(!full.find("partial")&&full.find("features")->arr.size()==154,"Default design inventory keeps complete legacy shape");
    const auto selected=call(session,"doc_info",R"({"detail":"design","ids":["Part 0",154]})");
    check(selected.find("partial")->b&&selected.find("scope")->str=="selected_features"&&selected.find("units")->str=="mm"&&selected.find("parameter_error"),"Selected inventory declares scope and units/error");
    check(selected.find("features")->arr.size()==2&&!selected.find("parameters")&&!selected.find("sketches"),"IDs default to feature rows only");
    check(selected.find("features")->arr[0].dump()==full.find("features")->arr[0].dump()&&selected.find("features")->arr[1].dump()==full.find("features")->arr[153].dump(),"Selected rows equal full inventory rows exactly");
    check(selected.find("total_counts")->find("features")->num==154&&selected.find("total_counts")->find("sketches")->num==2,"Partial report retains actual global totals");
    auto related=call(session,"doc_info",R"({"detail":"design","ids":[1],"sections":["features","sketches","parameters","assembly","materials"]})");
    check(related.find("sketches")->arr.size()==1&&related.find("sketches")->arr[0].dump()==full.find("sketches")->arr[0].dump(),"Selected sketch section retains exact direct source row");
    check(related.find("assembly")->arr.size()==1&&related.find("materials")->arr.size()==1&&related.find("parameters")->dump()==full.find("parameters")->dump(),"Associated placements/materials and document-wide parameters remain explicit");
    check(related.find("section_scopes")->find("parameters")->str=="document","Parameter section states its document-wide scope");
    auto section=call(session,"doc_info",R"({"detail":"design","sections":["sketches"]})");check(section.find("sketches")->dump()==full.find("sketches")->dump()&&!section.find("features"),"Section-only selection preserves entire chosen section");
    for(const char* args:{R"({"detail":"design","ids":[9999]})",R"({"detail":"design","ids":["Unknown"]})",R"({"detail":"design","ids":[1,1]})",R"({"detail":"design","ids":[]})",R"({"detail":"design","sections":["checks"]})",R"({"detail":"summary","ids":[1]})"})check(session.call("doc_info",parse(args)).find("isError")->b,"Invalid/unknown selection rejects instead of dropping data");
    check(call(session,"doc_info",R"({"ids":["Part 0",154]})").dump()==selected.dump(),"Omitted detail infers the same selected design read");
    auto incompatible=session.call("doc_info",parse(R"({"detail":"summary","ids":["Part 0",154],"sections":["features","assembly"]})"));
    const auto& diagnostic=*incompatible.find("structuredContent");check(diagnostic.find("error_code")->str=="INVALID_ARGUMENT"&&diagnostic.find("field")->str=="detail","Filter conflict is an argument error, not geometry evaluation or revision conflict");
    const auto& retry=*diagnostic.find("recovery")->find("retry_arguments");check(retry.find("ids")->dump()=="[\"Part 0\",154]"&&retry.find("sections")->dump()=="[\"features\",\"assembly\"]"&&retry.find("detail")->str=="design","Recovery retains exact requested targets and sections");
    check(!session.call("doc_info",retry).find("isError")->b,"Proposed corrected read executes successfully");
    session.document.features[1].name="Duplicate";session.document.features[2].name="Duplicate";check(session.call("doc_info",parse(R"({"detail":"design","ids":["Duplicate"]})")).find("isError")->b,"Ambiguous selected name rejects");session.document.features[1].name="Part 1";session.document.features[2].name="Part 2";
    check(session.revision==revision&&documentToJson(session.document)==before,"Selected reads never mutate document or revision");
    check(selected.dump().size()*10<full.dump().size(),"Representative selected read materially reduces response bytes");
    std::cout<<"design_inventory_features=154 full_bytes="<<full.dump().size()<<" selected_two_bytes="<<selected.dump().size()<<"\n";
}
#ifdef ARK_AGENT_NATIVE_TESTS
void testSheetPlacementIntent(){
    AgentSession s;
    call(s,"doc_apply",R"({"commands":[{"op":"sheet.create","name":"Blank","p":{"width":30,"thickness":1,"k_factor":0.5},"base":{"id":"panel","length":50}},{"op":"feature.edit","id":"Blank","manufacturing":{"route":"sheet_metal"}},{"op":"feature.add","type":"native_solid","name":"Installed","inputs":["Blank"],"native":{"op":"transform"},"p":{"tx":100,"rz":30},"manufacturing":{"route":"sheet_metal"}}]})");
    auto report=call(s,"sheet_inspect",R"({"id":"Installed"})");check(report.find("sheet_origin")->num==s.document.features[0].id&&report.find("local_authoring_coordinates")->b,"sheet inspection resolves installed placement to authoring geometry");
    auto saved=documentToJson(s.document);auto revision=s.revision;
    auto invalid=s.call("doc_apply",parse(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"bad","expr":99},{"op":"feature.add","type":"native_solid","name":"False sheet","native":{"op":"box"},"p":{"width":10,"depth":10,"height":1},"manufacturing":{"route":"sheet_metal"}}]})"));
    check(invalid.find("isError")->b&&s.revision==revision&&documentToJson(s.document)==saved,"invalid fabrication declaration preserves evaluated sheet and revision");
}
void testCurvedQueries(){
    AgentSession s;call(s,"doc_apply",R"({"commands":[{"op":"sketch.set","name":"Ring","sketch":{"entities":[{"kind":"circle","name":"rim","dof":[0,0,10]}]}},{"op":"feature.add","type":"native_solid","name":"Round","native":{"op":"extrude"},"p":{"sketch":"Ring","depth":2}}]})");
    auto report=call(s,"geometry_query",R"({"id":"Round","queries":[{"action":"curve","reference":"edge.start.rim","count":3},{"action":"continuity","a":"edge.start.rim","b":"edge.start.rim","fraction_a":0,"fraction_b":1},{"action":"references","filter":{"kind":"face","surface":"cylinder","radius_mm":10,"axis":[0,0,1],"coaxial_origin":[0,0,7]},"unique":true}]})");
    const auto& rows=report.find("results")->arr;for(const auto& row:rows)if(!row.find("ok")->b)throw std::runtime_error(row.dump());close(rows[0].find("length_mm")->num,20*K_PI,1e-6,"circle arc length");const auto& samples=rows[0].find("samples")->arr;close(samples[1].find("point_mm")->arr[0].num,-10,1e-6,"equal arc length inversion on circle");close(samples[1].find("curvature_per_mm")->num,.1,1e-8,"circle curvature");check(rows[1].find("g2")->b,"periodic seam G2");check(rows[2].find("count")->num==1,"cylinder radius and coaxial selector");
}
void testNative(){AgentSession s;auto started=std::chrono::steady_clock::now();
    call(s,"doc_apply",R"({"commands":[{"op":"param.set","name":"width","expr":10},{"op":"feature.add","type":"native_solid","name":"Block","native":{"op":"box"},"p":{"width":"width","depth":20,"height":30}},{"op":"assembly.add","name":"Base","feature":"Block","fixed":true},{"op":"assembly.add","name":"Moving","feature":"Block","p":{"tx":50}}]})");
    auto data=call(s,"geometry_query",R"({"id":"Block","queries":[{"action":"properties","density_g_cm3":1},{"action":"references","filter":{"kind":"face","normal":[0,0,1]},"unique":true},{"action":"curve","reference":"edge.x.ymin.zmin","count":3,"margin_mm":1},{"action":"surface","reference":"z_max"},{"action":"section","origin":[0,0,15],"normal":[0,0,1]}]})");
    auto rows=data.find("results");for(const auto& r:rows->arr)if(!r.find("ok")->b)throw std::runtime_error(r.dump());
    close(rows->arr[0].find("volume_mm3")->num,6000,1e-6,"exact volume");close(rows->arr[0].find("mass_g")->num,6,1e-8,"exact mass");close(rows->arr[0].find("volume_inertia_mm5_row_major")->arr[0].num,650000,1e-5,"box volume inertia");
    check(rows->arr[1].find("count")->num==1,"semantic selection");close(rows->arr[2].find("samples")->arr[1].find("point_mm")->arr[0].num,5,1e-7,"arc length middle");close(rows->arr[3].find("gaussian_curvature_per_mm2")->num,0,1e-9,"plane curvature");close(rows->arr[4].find("loops")->arr[0].find("enclosed_area_mm2")->num,200,1e-6,"section area");
    auto multiple=call(s,"geometry_query",R"({"id":1,"queries":[{"action":"references","filter":{"kind":"face"},"unique":true},{"action":"properties"}]})");check(!multiple.find("results")->arr[0].find("ok")->b&&multiple.find("results")->arr[1].find("ok")->b,"ambiguous query isolated");
    auto continuity=call(s,"geometry_query",R"({"id":1,"queries":[{"action":"continuity","a":"edge.x.ymin.zmin","b":"edge.x.ymin.zmin","fraction_a":0.5,"fraction_b":0.5},{"action":"references","filter":{"kind":"edge","adjacent_to":"z_max"}},{"action":"point_distance","point":[-1,10,15]},{"action":"curve","reference":"edge.x.ymin.zmin","fraction":2}]})");auto cr=continuity.find("results");check(cr->arr[0].find("g2")->b,"local G2 continuity");check(cr->arr[1].find("count")->num==4,"face boundary adjacency");close(cr->arr[2].find("nearest_point_mm")->arr[0].num,0,1e-6,"exact projection");check(!cr->arr[3].find("ok")->b,"curve domain checked");
    auto centered=call(s,"placement_plan",R"({"expected_revision":1,"node":"Moving","target":{"node":"Base","reference":"origin"},"mode":"center"})");check(centered.find("valid")->b,"centroid placement");close(centered.find("world_frame")->arr[12].num,0,1e-6,"centers coincide");
    auto plan=call(s,"placement_plan",R"({"expected_revision":1,"node":"Moving","target":{"node":"Base","reference":"origin"},"offset":[15,0,0]})");check(plan.find("valid")->b&&s.revision==1,"placement preview");accepted(s.call("doc_apply",*plan.find("apply")));
    auto gap=call(s,"assembly_info",R"({"clearance":{"a":"Base","b":"Moving"}})");close(gap.find("separation_mm")->num,5,1e-6,"exact separation");check(!gap.find("penetration_depth_checked")->b,"penetration contract");
    auto solve=call(s,"design_solve",R"({"expected_revision":2,"parameter":"width","lower":5,"upper":14,"target":3,"tolerance":0.000001,"objective":{"tool":"assembly_info","args":{"clearance":{"a":"Base","b":"Moving"}},"path":"separation_mm"}})");check(solve.find("converged")->b,"clearance target converges");close(solve.find("parameter_value")->num,12,1e-6,"width for clearance");
    auto pattern=call(s,"pattern_plan",R"({"expected_revision":2,"path":"Block","reference":"edge.x.ymin.zmin","count":3,"margin_mm":1,"name":"Pins","feature":"Block"})");check(pattern.find("valid")->b&&pattern.find("recipe")->find("commands")->arr.size()==3,"pattern preview");
    auto holes=call(s,"pattern_plan",R"({"expected_revision":2,"path":"Block","reference":"edge.x.ymin.zmin","count":3,"margin_mm":1,"name":"Drilled","feature":"Block","mode":"holes","normal":[0,0,1],"offset":[0,5,-1],"radius":0.5,"depth":32})");if(!holes.find("valid")->b)throw std::runtime_error(holes.dump());check(holes.find("path_linked")->b,"linked hole pattern");accepted(s.call("doc_apply",*holes.find("apply")));
    call(s,"doc_apply",R"({"commands":[{"op":"param.set","name":"width","expr":20}]})");
    auto drilled=call(s,"geometry_query",R"({"id":"Drilled","queries":[{"action":"properties"}]})");double volume=drilled.find("results")->arr[0].find("volume_mm3")->num;close(volume,12000-3*K_PI*.25*30,1e-5,"holes regenerate on changed path");
    Document restored;std::string error;auto serialized=documentToJson(s.document);V doc;arkjson::parse(documentToJson(s.document,true,false),doc);check(doc.find("version")->num==19,"linked pattern version");check(documentFromJson(serialized,restored,&error),"pattern roundtrip load");restored.recompute();check(restored.lastResults.back().ok,"pattern roundtrip evaluation");close(restored.evalBodies.back().nativeData.find("report")->find("volume_mm3")->num,volume,1e-5,"cache and cold rebuild parity");
    auto collision=call(s,"assembly_info",R"({"clearance":{"a":"Base","b":"Moving"}})");check(collision.find("overlap_volume_mm3")->num>0&&collision.find("clearance_is_estimate")->b&&!collision.find("penetration_depth_checked")->b,"overlap does not claim exact penetration");
    auto solid=call(s,"pattern_plan",R"({"expected_revision":4,"path":"Block","reference":"edge.x.ymin.zmin","count":2,"margin_mm":1,"name":"Copies","feature":"Block","mode":"solid","offset":[0,40,0]})");if(!solid.find("valid")->b)throw std::runtime_error(solid.dump());check(solid.find("path_linked")->b,"linked solid pattern");
    V bad=V::makeObj();bad.set("operation",V::makeStr("unknown_for_recovery_test"));bool rejected=false;try{nativeWorkerRequest(bad);}catch(const std::exception&){rejected=true;}check(rejected,"worker rejects invalid operation");check(nativeKernelInfo().find("version"),"worker recovers after error");
    auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();std::cout<<"native workflow elapsed_ms="<<elapsed<<" solve_evaluations="<<solve.find("evaluations")->num<<"\n";std::cout<<"worker_statistics="<<nativeKernelInfo().dump()<<"\n";
}
#endif
int main(){try{testMath();testTransactions();testDerivatives();testPlacementFrames();testManufacturingDiscovery();testSelectedDesignReads();
#ifdef ARK_AGENT_NATIVE_TESTS
testNative();
testCurvedQueries();testSheetPlacementIntent();
#endif
std::cout<<"Agent features passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
