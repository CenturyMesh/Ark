#include "ark/agent.hpp"
#include "ark/serialize.hpp"

#include <cmath>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using arkjson::Value;

Value parse(const char* text) {
    Value value;
    if (!arkjson::parse(text, value)) throw std::runtime_error("Invalid native feature test JSON");
    return value;
}

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Value accepted(const Value& response) {
    const Value* error = response.find("isError");
    if (error && error->b) {
        const Value* payload = response.find("structuredContent");
        throw std::runtime_error(payload && payload->find("error") ? payload->find("error")->str : "MCP call failed");
    }
    const Value* payload = response.find("structuredContent");
    if (!payload) throw std::runtime_error("MCP result missing payload");
    return *payload;
}

void testLoftHistoryAndExport() {
    ark::AgentSession session;
    const Value& first = accepted(session.call("doc_apply", parse(R"({"expected_revision":0,"commands":[
      {"op":"param.set","name":"base_r","expr":"5"},
      {"op":"plane.set","name":"Top","p":{"oz":"10"}},
      {"op":"sketch.set","name":"BottomCircle","sketch":{"entities":[{"kind":"circle","dof":[0,0,"base_r"]}]}},
      {"op":"sketch.set","name":"TopCircle","sketch":{"plane":"Top","entities":[{"kind":"circle","dof":[0,0,3]}]}},
      {"op":"feature.add","type":"loft","name":"Conical loft","p":{"solid":1,"ruled":1},
       "native":{"sections":["BottomCircle","TopCircle"]}}
    ]})")));
    check(first.find("created_ids") && first.find("created_ids")->arr.size() == 1, "loft creation IDs");
    check(session.document.evalBodies.size() == 1, "loft evaluation body");
    const Value& native = session.document.evalBodies.front().nativeData;
    const Value& report = *native.find("report");
    const double expected = std::acos(-1.0) * 10.0 * 49.0 / 3.0;
    check(std::abs(report.find("volume_mm3")->num - expected) < 1.0e-4, "history loft volume");
    const Value& inspection = accepted(session.call("native_inspect", parse(R"({"id":1})")));
    check(inspection.find("report") && !inspection.find("brep_base64"), "compact native inspection");
    check(accepted(session.call("kernel_info", Value::makeObj())).find("python_required")->b == false, "native backend info");

    const auto base = std::filesystem::temp_directory_path() /
        ("ark-native-feature-roundtrip-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Value args = Value::makeObj();args.set("path", Value::makeStr((base.string() + ".ark")));
    accepted(session.call("doc_save", args));
    ark::AgentSession restored;
    accepted(restored.call("doc_load", args));
    check(restored.document.features.front().type == ark::FeatureType::Loft, "loft feature persisted");
    check(restored.document.features.front().nativeSpec.find("sections") != nullptr, "native loft sections persisted");
    check(std::abs(restored.document.evalBodies.front().nativeData.find("report")->find("volume_mm3")->num - expected) < 1.0e-4, "loft recompute after load");
    Value exportArgs = Value::makeObj();exportArgs.set("id", Value::makeNum(1));exportArgs.set("path", Value::makeStr((base.string() + ".step")));
    const Value& exported = accepted(restored.call("export_step", exportArgs));
    check(exported.find("step_sha256") != nullptr, "native STEP export receipt");
    std::filesystem::remove(base.string() + ".ark");
    std::filesystem::remove(base.string() + ".step");
    std::filesystem::remove(base.string() + ".validation.json");

    const auto before = restored.revision;
    const Value invalid = restored.call("doc_apply", parse(R"({"commands":[{"op":"feature.add","type":"loft","p":{"solid":1,"ruled":1},"native":{"sections":["missing","TopCircle"]}}]})"));
    check(invalid.find("isError") && invalid.find("isError")->b, "missing sketch rejected");
    check(restored.revision == before && restored.document.features.size() == 1, "failed native edit is atomic");
}

void testNurbsExpressionsAndThicken() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"param.set","name":"edge","expr":"3"},
      {"op":"feature.add","type":"bspline_surface","name":"Exact pole plane","p":{"u_degree":3,"v_degree":3},
       "native":{"poles":[
         [[0,0,0],[0,1,0],[0,2,0],[0,"edge",0]],
         [[1,0,0],[1,1,0],[1,2,0],[1,"edge",0]],
         [[2,0,0],[2,1,0],[2,2,0],[2,"edge",0]],
         [["edge",0,0],["edge",1,0],["edge",2,0],["edge","edge",0]]
       ]}},
      {"op":"feature.add","type":"thicken","inputs":[1],"p":{"thickness":"2"}}
    ]})")));
    check(session.document.evalBodies.size() == 2, "NURBS and thickening bodies");
    const Value& faceReport = *session.document.evalBodies[0].nativeData.find("report");
    const Value& solidReport = *session.document.evalBodies[1].nativeData.find("report");
    check(std::abs(faceReport.find("area_mm2")->num - 9) < 1.0e-5, "native expression poles surface area");
    check(std::abs(solidReport.find("volume_mm3")->num - 18) < 1.0e-4, "native thickening volume");
    check(session.document.features[0].nativeSpec.find("poles")->arr[0].arr[3].arr[1].t == Value::Str,
          "authored pole expression retained");
    const auto revision = session.revision;
    const Value meshTransform = session.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"transform","inputs":[1],"p":{"tx":2}}
    ]})"));
    check(meshTransform.find("isError") && meshTransform.find("isError")->b, "mesh transform of exact surface rejected");
    check(session.revision == revision, "rejected approximate transform leaves history unchanged");
}

void testExtendedSurfacesAndMeshConsent() {
    ark::AgentSession session;
    const Value created = accepted(session.call("doc_apply", parse(R"({"expected_revision":0,"commands":[
      {"op":"sketch.set","name":"Section","sketch":{"entities":[{"kind":"circle","dof":[0,0,2]}]}},
      {"op":"sketch.set","name":"Spine","sketch":{"plane":"XZ","entities":[{"kind":"line","dof":[0,0,0,20]}]}},
      {"op":"sketch.set","name":"Rail","sketch":{"entities":[{"kind":"line","dof":[0,0,10,0]}]}},
      {"op":"sketch.set","name":"Generatrix","sketch":{"entities":[{"kind":"line","dof":[5,0,5,10]}]}},
      {"op":"feature.add","type":"sweep","name":"Swept cylinder","p":{"solid":"1"},"native":{"section":"Section","path":"Spine"}},
      {"op":"feature.add","type":"extrude_surface","name":"Extruded rail","p":{"sketch":"Rail","distance":"5","solid":"0"}},
      {"op":"feature.add","type":"revolve_surface","name":"Cylinder wall","p":{"sketch":"Generatrix","angle":"360","solid":"0"},"native":{"axis":"Y"}}
    ]})")));
    check(created.find("created_ids") && created.find("created_ids")->arr.size() == 3, "extended surface creation IDs");
    const double pi = std::acos(-1.0);
    const Value& sweep = *session.document.evalBodies[0].nativeData.find("report");
    const Value& extrusion = *session.document.evalBodies[1].nativeData.find("report");
    const Value& revolution = *session.document.evalBodies[2].nativeData.find("report");
    check(std::abs(sweep.find("volume_mm3")->num - 80.0 * pi) < 1.0e-2, "history sweep volume");
    check(std::abs(extrusion.find("area_mm2")->num - 50.0) < 1.0e-3, "history surface extrusion area");
    check(std::abs(revolution.find("area_mm2")->num - 100.0 * pi) < 1.0e-2, "history surface revolution area");

    const auto base = std::filesystem::temp_directory_path() /
        ("ark-extended-surface-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Value save = Value::makeObj();
    save.set("path", Value::makeStr(base.string() + ".ark"));
    accepted(session.call("doc_save", save));
    ark::AgentSession loaded;
    accepted(loaded.call("doc_load", save));
    check(loaded.document.features[0].type == ark::FeatureType::Sweep, "sweep persisted");
    check(loaded.document.features[1].type == ark::FeatureType::ExtrudeSurface, "surface extrusion persisted");
    check(std::abs(loaded.document.evalBodies[2].nativeData.find("report")->find("area_mm2")->num - 100.0 * pi) < 1.0e-2, "revolution recompute after load");
    std::filesystem::remove(base.string() + ".ark");

    const auto revision = loaded.revision;
    const auto count = loaded.document.features.size();
    const Value sampled = loaded.call("doc_apply", parse(R"({"commands":[{"op":"feature.add","type":"mesh_offset","inputs":[1],"p":{"distance":"1"}}]})"));
    check(sampled.find("isError") && sampled.find("isError")->b, "mesh offset of an exact surface requires sample");
    const Value* payload = sampled.find("structuredContent");
    check(payload && payload->find("error") && payload->find("error")->str.find("sample") != std::string::npos, "sample rejection explains the consent flag");
    check(loaded.revision == revision && loaded.document.features.size() == count, "refused mesh sampling is atomic");
}

void testSheetSourceImportAndCustody() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("ark-native-feature-sheet-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(directory), "create test-owned sheet directory");
    const auto formed = directory / "formed.step";
    const auto flat = directory / "flat.step";
    const auto saved = directory / "source.ark";
    ark::AgentSession design;
    accepted(design.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"sheet_metal","p":{
        "width":60,"leg_a":80,"leg_b":50,"thickness":2,
        "inner_radius":5,"angle":90,"k_factor":0.5
      },"native":{"openings":[
        {"panel":0,"kind":"circle","x":25,"y":20,"radius":4},
        {"panel":1,"kind":"polygon","points":[[15,20],[25,20],[25,30],[15,30]]}
      ]}}
    ]})")));
    check(design.document.evalBodies.size() == 1, "designed sheet body");
    check(design.document.evalBodies[0].nativeData.find("flat_brep_base64") != nullptr, "designed flat B-rep");
    Value output = Value::makeObj();output.set("id", Value::makeNum(1));
    output.set("path", Value::makeStr(formed.string()));output.set("pose", Value::makeStr("formed"));
    check(accepted(design.call("export_step", output)).find("step_sha256") != nullptr, "formed sheet STEP receipt");

    ark::AgentSession imported;
    Value input = Value::makeObj();input.set("path", Value::makeStr(formed.string()));
    input.set("k_factor", Value::makeNum(0.5));
    const Value importedResult = accepted(imported.call("import_sheet_step", input));
    check(importedResult.find("created_id") && importedResult.find("created_id")->num == 1, "sheet import feature ID");
    check(imported.document.features.front().type == ark::FeatureType::SourceSheet, "read-only source feature type");
    const Value& report = *imported.document.features.front().sourceData.find("report");
    check(report.find("source_projection_verified") && report.find("source_projection_verified")->b, "source-owned projection verified");
    check(report.find("bend_count") && report.find("bend_count")->num == 1, "one physical source bend");
    check(report.find("opening_count") && report.find("opening_count")->num == 2, "source openings preserved");
    const Value inspect = accepted(imported.call("native_inspect", parse(R"({"id":1})")));
    check(inspect.find("read_only_geometry") && inspect.find("read_only_geometry")->b, "source native inspection is read-only");
    check(inspect.find("has_flat") && inspect.find("has_flat")->b, "source flat pose available");
    output.set("path", Value::makeStr(flat.string()));output.set("pose", Value::makeStr("flat"));
    check(accepted(imported.call("export_step", output)).find("step_sha256") != nullptr, "source-bound flat STEP receipt");

    Value save = Value::makeObj();save.set("path", Value::makeStr(saved.string()));
    accepted(imported.call("doc_save", save));
    ark::AgentSession loaded;accepted(loaded.call("doc_load", save));
    check(loaded.document.features.front().sourceData.find("source_sha256") != nullptr, "source SHA persisted");
    const auto revision = loaded.revision;
    const Value changed = loaded.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.edit","id":1,"p":{"k_factor":0.3}}
    ]})"));
    check(changed.find("isError") && changed.find("isError")->b && loaded.revision == revision, "source geometry edit rejected atomically");
    std::filesystem::remove(formed);
    check(accepted(loaded.call("export_step", output)).find("step_sha256") != nullptr,
          "missing original source exports verified embedded payload");
    std::filesystem::copy_file(flat, formed);
    const Value stale = loaded.call("export_step", output);
    check(stale.find("isError") && stale.find("isError")->b, "changed existing source blocks export");
    std::filesystem::remove(formed);
    std::filesystem::remove(flat);
    std::filesystem::remove(directory / "formed.validation.json");
    std::filesystem::remove(directory / "flat.validation.json");
    std::filesystem::remove(saved);
    std::filesystem::remove(directory);
}
} // namespace

void testMirrorPlanes() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"expected_revision":0,"commands":[
      {"op":"feature.add","type":"native_solid","name":"Stock","p":{"op":"box","width":"10","depth":"10","height":"5"}},
      {"op":"plane.set","name":"Split","p":{"oz":"20"}},
      {"op":"feature.add","type":"native_solid","name":"Across datum","inputs":[1],"p":{"op":"mirror","fuse":"0"},"native":{"plane":"Split"}},
      {"op":"feature.add","type":"native_solid","name":"Across face","inputs":[1],"p":{"op":"mirror","fuse":"0"},"native":{"face":"z_max"}}
    ]})")));
    check(session.document.evalBodies.size() == 3, "mirror bodies");
    const auto& datum = session.document.evalBodies[1].nativeData;
    const auto& face = session.document.evalBodies[2].nativeData;
    check(std::abs(datum.find("report")->find("volume_mm3")->num - 1000) < 1e-3, "datum mirror volume");
    check(datum.find("report")->find("solids")->num == 2, "datum mirror solids");
    check(std::abs(datum.find("report")->find("bounds_mm")->arr[5].num - 40) < 1e-3, "datum mirror height");
    check(std::abs(face.find("report")->find("volume_mm3")->num - 1000) < 1e-3, "face mirror volume");
    check(face.find("report")->find("solids")->num == 2, "face mirror solids");
    check(std::abs(face.find("report")->find("bounds_mm")->arr[5].num - 10) < 1e-3, "face mirror height");
    arkjson::Value saved;
    check(arkjson::parse(ark::documentToJson(session.document,true,false), saved), "serialize mirror document");
    check(saved.find("version")->num == 9, "mirror document is version 9");
    ark::AgentSession plain;
    accepted(plain.call("doc_apply", parse(R"({"expected_revision":0,"commands":[
      {"op":"feature.add","type":"native_solid","name":"Stock","p":{"op":"box","width":"10","depth":"10","height":"5"}}
    ]})")));
    arkjson::Value plainSaved;
    check(arkjson::parse(ark::documentToJson(plain.document,true,false), plainSaved), "serialize plain solid");
    check(plainSaved.find("version")->num == 7, "plain native solid stays version 7");
}

void testFinishingHistory() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"expected_revision":0,"commands":[
      {"op":"feature.add","type":"native_solid","name":"Stock","p":{"op":"box","width":"10","depth":"10","height":"5"}},
      {"op":"feature.add","type":"native_solid","name":"Round convex","inputs":[1],"p":{"op":"fillet","radius":"0.5","min_angle":"15"},"native":{"selection":"convex"}},
      {"op":"feature.add","type":"native_solid","name":"Copies","inputs":[1],"p":{"op":"linear_pattern","count":"2","dx":"30","fuse":"0"}},
      {"op":"plane.set","name":"TipPlane","p":{"oz":"10"}},
      {"op":"sketch.set","name":"BaseSquare","sketch":{"entities":[
        {"kind":"line","dof":[-5,-5,5,-5]},{"kind":"line","dof":[5,-5,5,5]},
        {"kind":"line","dof":[5,5,-5,5]},{"kind":"line","dof":[-5,5,-5,-5]}
      ]}},
      {"op":"sketch.set","name":"Tip","sketch":{"plane":"TipPlane","entities":[{"kind":"point","dof":[0,0]}]}},
      {"op":"feature.add","type":"loft","name":"Pyramid","p":{"solid":"1","ruled":"1"},"native":{"sections":["BaseSquare","Tip"]}}
    ]})")));
    check(session.document.features.size() == 4, "finishing feature count");
    const double pi = std::acos(-1.0);
    const double filleted = 500 - 4 * (10 + 10 + 5) * 0.25 * (1 - pi / 4) + 8 * 0.125 * (2 - 7 * pi / 12);
    check(std::abs(session.document.evalBodies[1].nativeData.find("report")->find("volume_mm3")->num - filleted) < 1.0e-3, "history convex fillet");
    check(session.document.evalBodies[2].nativeData.find("report")->find("solids")->num == 2, "history pattern solids");
    check(std::abs(session.document.evalBodies[3].nativeData.find("report")->find("volume_mm3")->num - 1000.0 / 3.0) < 1.0e-4, "history point-tip loft");
    arkjson::Value saved;
    check(arkjson::parse(ark::documentToJson(session.document,true,false), saved), "serialize finishing document");
    check(saved.find("version")->num == 9, "finishing document is version 9");
    std::string older = ark::documentToJson(session.document,true,false);
    auto at = older.find("\"version\":9");
    check(at != std::string::npos, "version token");
    older.replace(at, std::string("\"version\":9").size(), "\"version\":8");
    ark::Document rejected;
    std::string error;
    check(!ark::documentFromJson(older, rejected, &error), "version 8 rejects the new operations");
    check(error.find("version 9") != std::string::npos, "version rejection names version 9");
}

void testPlaneSplitAndMirrorCopyHistory() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"expected_revision":0,"commands":[
      {"op":"feature.add","type":"native_solid","name":"Stock","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"plane.set","name":"Station","p":{"oz":"3"}},
      {"op":"feature.add","type":"native_solid","name":"Upper","inputs":["Stock"],"p":{"op":"split_plane"},"native":{"plane":"Station","keep":"positive"}},
      {"op":"feature.add","type":"native_solid","name":"Reflected","inputs":["Stock"],"p":{"op":"mirror","copy_only":"1"},"native":{"normal":[0,0,1],"origin":[0,0,20]}},
      {"op":"plane.set","name":"Rotated","p":{"oy":"4","rx":"90"}},
      {"op":"feature.add","type":"native_solid","name":"Side","inputs":["Stock"],"p":{"op":"split_plane"},"native":{"plane":"Rotated","keep":"positive"}}
    ]})")));
    const auto& upper=session.document.evalBodies[1].nativeData;
    const auto& reflected=session.document.evalBodies[2].nativeData;
    check(std::abs(upper.find("report")->find("volume_mm3")->num-700)<1e-4,"datum split uses authored plane");
    check(std::abs(reflected.find("report")->find("volume_mm3")->num-1000)<1e-4,"copy-only mirror keeps one volume");
    check(reflected.find("report")->find("solids")->num==1,"copy-only mirror returns one solid");
    check(std::abs(session.document.evalBodies[3].nativeData.find("report")->find("volume_mm3")->num-400)<1e-4,"rotated datum split uses its normal");
    const auto previous=session.revision;
    auto invalid=session.call("doc_apply",parse(R"({"commands":[{"op":"feature.edit","id":"Upper","native":{"keep":"invalid"}}]})"));
    check(invalid.find("isError")&&invalid.find("isError")->b&&session.revision==previous,"invalid split rolls back atomically");
    std::string saved=ark::documentToJson(session.document),error;
    ark::Document loaded;
    check(ark::documentFromJson(saved,loaded,&error),"split and mirror document reloads");
    loaded.recompute();
    check(loaded.lastResults.back().ok,"reloaded exact operations recompute");
}

static void testSingleSolidRecovery(){
    ark::AgentSession session;
    accepted(session.call("doc_apply",parse(R"({"commands":[{"op":"feature.add","type":"native_solid","name":"Left","native":{"op":"box"},"p":{"width":10,"depth":10,"height":10}},{"op":"feature.add","type":"native_solid","name":"Right","inputs":["Left"],"native":{"op":"transform"},"p":{"tx":30}}]})")));
    auto before=ark::documentToJson(session.document);auto revision=session.revision;
    auto response=session.call("doc_apply",parse(R"({"expected_revision":1,"commands":[{"op":"feature.add","type":"native_solid","name":"Disconnected union","inputs":["Left","Right"],"native":{"op":"union"}}]})"));
    check(response.find("isError")->b,"disconnected union remains rejected");const auto& detail=*response.find("structuredContent");
    check(detail.find("error_code")->str=="NATIVE_SINGLE_SOLID_REQUIRED"&&detail.find("observed_solid_count")->num==2,"actual kernel solid count reaches the typed diagnostic");
    check(detail.find("recovery")->find("inspect_before_retry")->b&&detail.find("recovery")->find("design_options")->arr.size()==2,"diagnostic offers inspection and design alternatives without choosing geometry");
    check(detail.find("document_unchanged")->b&&session.revision==revision&&ark::documentToJson(session.document)==before,"failure guidance cannot commit or alter rejected geometry");
}

// The moto concept's tyre (an ellipse recipe revolved into a solid) took the
// 30-second worker limit for its centroid and then reported a missing solid.
static void testRevolvedEllipseMassProperties(){
    ark::AgentSession session;
    accepted(session.call("doc_apply",parse(R"({"expected_revision":0,"commands":[
      {"op":"param.set","name":"wheel_r","expr":"315"},{"op":"param.set","name":"tire_hw","expr":"75"},{"op":"param.set","name":"tire_sec","expr":"48"},
      {"op":"material.set","name":"Tyre rubber","density":1.15},
      {"op":"plane.set","name":"wheel_plane","parent":"XY","p":{"oz":"wheel_r"}},
      {"op":"sketch.set","name":"tire_profile","sketch":{"plane":"wheel_plane","entities":[{"kind":"recipe","name":"tire_section","recipe":{"type":"ellipse","center":["wheel_r-tire_sec",0],"major":"tire_hw","minor":"tire_sec","angle":90}}]}},
      {"op":"feature.add","type":"revolve_surface","name":"Tyre","p":{"sketch":"tire_profile","angle":360,"solid":1},"native":{"axis":"Y"},"material":"Tyre rubber"}]})")));
    const auto started=std::chrono::steady_clock::now();
    const Value inspected=accepted(session.call("native_inspect",parse(R"({"id":"Tyre","detail":"properties","gravity_m_s2":9.80665})")));
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    const double pi=std::acos(-1.0),volume=2.0*pi*pi*267.0*75.0*48.0;
    check(inspected.find("mass_checked")&&inspected.find("mass_checked")->b,"revolved ellipse tyre has exact mass properties");
    check(std::abs(inspected.find("mass_kg")->num-volume*1.15e-6)<1e-6,"tyre mass matches the analytic elliptic torus volume");
    const Value& center=*inspected.find("center_mm");
    check(std::abs(center.arr[0].num)<1e-6&&std::abs(center.arr[2].num-315.0)<1e-6,"tyre centroid is on the axle");
    check(seconds<10.0,"tyre centroid stays far inside the native worker limit");
}

// Agent-facing contracts observed failing in Studio sessions: each rejection
// names the field, operation or alternative instead of a bare kernel failure.
static std::string rejectedError(ark::AgentSession& session, const char* commands) {
    const auto before = session.revision;
    const Value response = session.call("doc_preview", parse(commands));
    const Value& payload = *response.find("structuredContent");
    if (response.find("isError")->b) { check(session.revision == before, "rejected request leaves revision unchanged"); return payload.find("error")->str; }
    check(!payload.find("valid")->b && session.revision == before, "invalid candidate is rejected without commit");
    return payload.find("candidate_result")->find("error")->str;
}
static void testAgentGeometryDiagnostics() {
    ark::AgentSession session;
    const char* sections = R"({"op":"plane.set","name":"Top","p":{"oz":"10"}},
      {"op":"sketch.set","name":"Low","sketch":{"entities":[{"kind":"circle","dof":[0,0,5]}]}},
      {"op":"sketch.set","name":"High","sketch":{"plane":"Top","entities":[{"kind":"circle","dof":[0,0,3]}]}},
      {"op":"sketch.set","name":"OpenHigh","sketch":{"plane":"Top","entities":[{"kind":"line","dof":[-3,0,3,0]}]}})";
    accepted(session.call("doc_apply", parse((std::string(R"({"expected_revision":0,"commands":[)") + sections + "]}").c_str())));
    accepted(session.call("doc_preview", parse(R"({"expected_revision":1,"commands":[{"op":"feature.add","type":"loft","name":"Default ruled","p":{"solid":1},"native":{"sections":["Low","High"]}}]})")));
    // A preview check can only reach the feature it adds by name.
    accepted(session.call("doc_preview", parse(R"({"expected_revision":1,"commands":[{"op":"feature.add","type":"native_solid","name":"Checked block","native":{"op":"box"},"p":{"width":4,"depth":5,"height":6}}],"checks":[{"query":{"tool":"native_inspect","args":{"id":"Checked block"},"path":"report.brep_valid"},"op":"true"}]})")));
    auto error = rejectedError(session, R"({"expected_revision":1,"commands":[{"op":"feature.add","type":"loft","name":"Mixed","p":{"solid":0,"compatible":1},"native":{"sections":["Low","OpenHigh"]}}]})");
    check(error.find("mix open and closed profiles") != std::string::npos && error.find("section 1") != std::string::npos, "loft names the open/closed section mismatch instead of a bare OCCT failure");
    error = rejectedError(session, R"({"expected_revision":1,"commands":[{"op":"feature.add","type":"native_solid","name":"Spun","native":{"op":"revolve"}}]})");
    check(error.find("Unknown native_solid op 'revolve'") != std::string::npos && error.find("revolve_surface") != std::string::npos && error.find("input_brep") == std::string::npos, "unknown native op is named before any input body is required");
    error = rejectedError(session, R"({"expected_revision":1,"commands":[{"op":"feature.add","type":"circular_pattern","name":"Ring"}]})");
    check(error.find("native_solid operation") != std::string::npos, "native op sent as a feature type points to native_solid");
    error = rejectedError(session, R"({"expected_revision":1,"commands":[{"op":"feature.add","type":"no_such_type","name":"X"}]})");
    check(error.find("accepted types:") != std::string::npos && error.find("revolve_surface") != std::string::npos, "unknown feature type lists accepted types");
    error = rejectedError(session, R"({"expected_revision":1,"commands":[{"op":"feature.add","type":"loft","name":"Bool","p":{"solid":true},"native":{"sections":["Low","High"]}}]})");
    check(error.find("p.solid") != std::string::npos && error.find("1 or 0") != std::string::npos, "boolean parameter is rejected with its field and flag syntax");
    error = rejectedError(session, R"({"expected_revision":1,"commands":[{"op":"feature.add","type":"revolve","name":"Legacy","p":{"sketch":"Low"},"native":{"axis":"Y"}}]})");
    check(error.find("does not accept a native object") != std::string::npos && error.find("revolve_surface") != std::string::npos, "native object on a legacy type is distinguished from a malformed native object");
    error = rejectedError(session, R"({"expected_revision":1,"commands":[{"op":"feature.add","type":"native_solid","name":"Cut","inputs":["Missing body"],"native":{"op":"cut"}}]})");
    check(error.find("existing feature names") != std::string::npos || error.find("not found") != std::string::npos, "missing named input is reported");
    const Value stale = session.call("doc_apply", parse(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"stale","expr":"1"}]})"));
    const Value& conflict = *stale.find("structuredContent");
    check(stale.find("isError")->b && conflict.find("error_code")->str == "REVISION_CONFLICT" && conflict.find("recovery")->find("current_revision")->num == 1 && conflict.find("expected_revision")->num == 0, "revision conflict echoes both revisions with an explicit recovery");
    check(conflict.dump().find("detail design") == std::string::npos, "revision conflict does not ask for a full design read");
    accepted(session.call("doc_apply", parse(R"({"expected_revision":1,"commands":[
      {"op":"sketch.set","name":"Ring profile","sketch":{"entities":[{"kind":"line","dof":[5,0,10,0]},{"kind":"line","dof":[10,0,10,10]},{"kind":"line","dof":[10,10,5,10]},{"kind":"line","dof":[5,10,5,0]}]}},
      {"op":"feature.add","type":"revolve_surface","name":"Closed skin","p":{"sketch":"Ring profile","angle":"360","solid":"0"},"native":{"axis":"Y"}}]})")));
    const auto closedSkin = session.document.features.back().id;
    error = rejectedError(session, (R"({"expected_revision":2,"commands":[{"op":"feature.add","type":"thicken","inputs":[)" + std::to_string(closedSkin) + R"(],"p":{"thickness":"1"}}]})").c_str());
    check(error.find("shell is closed") != std::string::npos, "thicken explains that a closed shell has no boundary to offset");
}

int main() {
    try {
        testAgentGeometryDiagnostics();
        testSingleSolidRecovery();
        testRevolvedEllipseMassProperties();
        testLoftHistoryAndExport();
        testMirrorPlanes();
        testFinishingHistory();
        testPlaneSplitAndMirrorCopyHistory();
        testNurbsExpressionsAndThicken();
        testExtendedSurfacesAndMeshConsent();
        testSheetSourceImportAndCustody();
        std::cout << "Native feature history tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native feature history tests failed: " << error.what() << '\n';
        return 1;
    }
}
