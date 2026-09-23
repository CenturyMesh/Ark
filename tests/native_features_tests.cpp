#include "ark/agent.hpp"

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

int main() {
    try {
        testLoftHistoryAndExport();
        testNurbsExpressionsAndThicken();
        testSheetSourceImportAndCustody();
        std::cout << "Native feature history tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native feature history tests failed: " << error.what() << '\n';
        return 1;
    }
}
