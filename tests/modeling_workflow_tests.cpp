#include "ark/agent.hpp"
#include "ark/io.hpp"
#include "ark/serialize.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using arkjson::Value;

Value parse(const char* text) {
    Value value;
    if (!arkjson::parse(text, value)) throw std::runtime_error("Invalid modeling workflow JSON");
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

const Value* bodyData(ark::Document& document, ark::u64 id) {
    for (auto& body : document.evalBodies) if (body.id == id) return &body.nativeData;
    throw std::runtime_error("Missing evaluated body");
}

double volumeOf(ark::Document& document, ark::u64 id) {
    return bodyData(document, id)->find("report")->find("volume_mm3")->num;
}

const Value* placed(const Value& info, const std::string& name, double copy) {
    for (const auto& item : info.find("instances")->arr)
        if (item.find("name")->str == name && item.find("copy")->num == copy) return &item;
    return nullptr;
}

void testNames() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"box","name":"Block","p":{"dx":10,"dy":10,"dz":10}},
      {"op":"feature.add","type":"transform","name":"Moved","inputs":["Block"],"p":{"tx":5}}
    ]})")));
    check(session.document.features[1].inputs.size() == 1 && session.document.features[1].inputs[0] == 1, "unique feature name stores an id");
    const auto saved = ark::documentToJson(session.document);
    const auto revision = session.revision;
    const Value ambiguous = session.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"box","name":"Block","p":{"dx":4,"dy":4,"dz":4}},
      {"op":"feature.add","type":"transform","inputs":["Block"],"p":{"tx":1}}
    ]})"));
    check(ambiguous.find("isError") && ambiguous.find("isError")->b, "ambiguous feature name fails");
    check(ambiguous.find("structuredContent")->find("error")->str.find("Ambiguous") != std::string::npos, "ambiguous name is reported");
    check(session.revision == revision && ark::documentToJson(session.document) == saved, "ambiguous name rolls the batch back");
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"assembly.add","name":"Anchor","feature":"Block","fixed":true},
      {"op":"assembly.add","name":"Follower","feature":"Block","p":{"ty":"12"}},
      {"op":"mate.add","a":"Anchor","b":"Follower","type":"coincident"}
    ]})")));
    check(session.document.assembly[0].feature == 1, "assembly feature name stores an id");
    check(session.document.mates.size() == 1 && session.document.mates[0].a == 1 && session.document.mates[0].b == 2, "mate names store ids");
    const auto mated = ark::documentToJson(session.document);
    const auto matedRevision = session.revision;
    const Value broken = session.call("doc_apply", parse(R"({"commands":[{"op":"mate.add","a":1,"b":1,"type":"coincident"}]})"));
    check(broken.find("isError") && broken.find("isError")->b, "bad mate fails");
    check(session.revision == matedRevision && ark::documentToJson(session.document) == mated, "bad mate rolls the batch back");
}

void testPatterns() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"param.set","name":"motor_x","expr":"120"},
      {"op":"param.set","name":"motor_y","expr":"0"},
      {"op":"param.set","name":"copies","expr":"4"},
      {"op":"feature.add","type":"box","name":"Pod","p":{"dx":10,"dy":10,"dz":10}},
      {"op":"assembly.add","name":"Orbit","feature":"Pod","p":{"tx":"motor_x","ty":"motor_y"},"pattern":{"type":"circular","count":"copies","step":"90","axis":"Z"}},
      {"op":"assembly.add","name":"Row","feature":"Pod","pattern":{"type":"linear","count":"2","dx":"20"}}
    ]})")));
    Value info = accepted(session.call("assembly_info", Value::makeObj()));
    check(info.find("instances")->arr.size() == 6, "pattern emits every copy");
    check(info.find("bom")->arr[0].find("quantity")->num == 6, "bom counts copies");
    check(info.find("diagnostics")->find("remaining_dofs")->num == 12, "a patterned node counts as one body");
    const Value* orbit = placed(info, "Orbit", 1);
    check(orbit != nullptr, "circular copy");
    const auto& matrix = orbit->find("world_column_major")->arr;
    check(std::abs(matrix[0].num) < 1e-6 && std::abs(matrix[1].num - 1) < 1e-6 && std::abs(matrix[4].num + 1) < 1e-6, "rotZ 90 takes +X to +Y");
    check(std::abs(matrix[12].num) < 1e-4 && std::abs(matrix[13].num - 120) < 1e-4, "circular copy orbits the parent origin");
    const Value* row = placed(info, "Row", 1);
    check(row && std::abs(row->find("world_column_major")->arr[12].num - 20) < 1e-6, "linear copy translates by dx");
    Value saved;
    check(arkjson::parse(ark::documentToJson(session.document), saved), "serialize pattern document");
    check(saved.find("version")->num == 10, "instance pattern is version 10");
    std::string older = ark::documentToJson(session.document);
    const auto at = older.find("\"version\":10");
    check(at != std::string::npos, "version token");
    older.replace(at, std::string("\"version\":10").size(), "\"version\":9");
    ark::Document rejected;
    std::string error;
    check(!ark::documentFromJson(older, rejected, &error), "version 9 rejects an instance pattern");
    check(error.find("version 10") != std::string::npos, "version rejection names version 10");
    ark::Document loaded;
    check(ark::documentFromJson(ark::documentToJson(session.document), loaded, &error), "version 10 reloads");
    check(loaded.assembly[0].pattern.active && loaded.assembly[0].pattern.count == "copies", "pattern expressions reload");
    accepted(session.call("doc_apply", parse(R"({"commands":[{"op":"param.set","name":"copies","expr":"3"}]})")));
    info = accepted(session.call("assembly_info", Value::makeObj()));
    int orbits = 0;
    for (const auto& item : info.find("instances")->arr) if (item.find("name")->str == "Orbit") ++orbits;
    check(orbits == 3, "count expression rebuilds the copies");
    const auto kept = ark::documentToJson(session.document);
    const auto revision = session.revision;
    const Value group = session.call("doc_apply", parse(R"({"commands":[{"op":"assembly.add","name":"Empty","feature":0,"pattern":{"type":"linear","count":"2","dx":"5"}}]})"));
    check(group.find("isError") && group.find("isError")->b, "a group cannot carry a pattern");
    check(session.revision == revision && ark::documentToJson(session.document) == kept, "group pattern rolls back");
}

void testInterference() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"box","name":"Cube","p":{"dx":10,"dy":10,"dz":10}},
      {"op":"assembly.add","name":"A","feature":"Cube","fixed":true},
      {"op":"assembly.add","name":"B","feature":"Cube","fixed":true,"p":{"tx":"6"}}
    ]})")));
    Value info = accepted(session.call("assembly_info", Value::makeObj()));
    check(info.find("interference_checked") && info.find("interference_checked")->b, "small meshes are checked");
    check(info.find("interference_authority")->str == "display_mesh", "interference authority");
    check(info.find("interference")->arr.size() == 1, "overlapping cubes are reported");
    check(info.find("interference")->arr[0].find("method")->str == "display_mesh", "overlap uses the display mesh");
    accepted(session.call("doc_apply", parse(R"({"commands":[{"op":"assembly.edit","id":2,"p":{"tx":"30"}}]})")));
    info = accepted(session.call("assembly_info", Value::makeObj()));
    check(info.find("interference_checked")->b && info.find("interference")->arr.empty(), "separated cubes do not overlap");
}

void testCandidate() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"box","name":"Cube","p":{"dx":10,"dy":10,"dz":10}},
      {"op":"assembly.add","name":"A","feature":"Cube","fixed":true},
      {"op":"assembly.add","name":"B","feature":"Cube","fixed":true}
    ]})")));
    auto before = ark::documentToJson(session.document);
    auto revision = session.revision;
    Value info = accepted(session.call("assembly_info", parse(R"({"candidate":{"a":"A","b":"B","type":"coincident","reference_a":"origin","reference_b":"origin"}})")));
    const Value* candidate = info.find("candidate");
    check(candidate && candidate->find("solved")->b && candidate->find("satisfied")->b, "coincident fixed frames satisfy");
    check(candidate->find("frames")->arr.size() == 2, "candidate returns both frames");
    check(candidate->find("axis_convention")->str.find("Analytic cylinder, cone and torus meshes run along +Y") != std::string::npos, "candidate states the axis convention");
    check(info.find("mates")->arr.empty(), "a candidate is not stored");
    check(session.revision == revision && ark::documentToJson(session.document) == before, "a satisfied candidate does not change the document");
    accepted(session.call("doc_apply", parse(R"({"commands":[{"op":"assembly.edit","id":2,"p":{"ty":"10"}}]})")));
    before = ark::documentToJson(session.document);
    revision = session.revision;
    info = accepted(session.call("assembly_info", parse(R"({"candidate":{"a":1,"b":2,"type":"coincident"}})")));
    candidate = info.find("candidate");
    check(candidate && candidate->find("solved")->b && !candidate->find("satisfied")->b, "separated fixed frames stay unsatisfied");
    check(std::abs(candidate->find("position_residual_mm")->num - 10) < 1e-3, "candidate residual is the 10 mm gap");
    check(session.revision == revision && ark::documentToJson(session.document) == before, "an unsatisfied candidate does not change the document");
}

void testExactBooleans() {
    ark::AgentSession pair;
    accepted(pair.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"native_solid","name":"A","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"feature.add","type":"native_solid","name":"B","inputs":["A"],"p":{"op":"transform","tx":"6"}},
      {"op":"feature.add","type":"native_solid","name":"Joined","inputs":["A","B"],"p":{"op":"union"}}
    ]})")));
    Value saved;
    check(arkjson::parse(ark::documentToJson(pair.document), saved), "serialize two-input union");
    check(saved.find("version")->num == 7, "two-input union stays version 7");
    check(std::abs(volumeOf(pair.document, 3) - 1600) < 1e-2, "two overlapping cubes union to 1600 mm3");

    ark::AgentSession triple;
    accepted(triple.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"native_solid","name":"A","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"feature.add","type":"native_solid","name":"B","inputs":["A"],"p":{"op":"transform","tx":"6"}},
      {"op":"feature.add","type":"native_solid","name":"C","inputs":["A"],"p":{"op":"transform","tx":"12"}},
      {"op":"feature.add","type":"native_solid","name":"Joined","inputs":["A","B","C"],"p":{"op":"union"}}
    ]})")));
    check(arkjson::parse(ark::documentToJson(triple.document), saved), "serialize three-input union");
    check(saved.find("version")->num == 10, "three-input union is version 10");
    check(std::abs(volumeOf(triple.document, 4) - 2200) < 1e-2, "three cubes union to 2200 mm3");
    check(bodyData(triple.document, 4)->find("report")->find("solids")->num == 1, "the union is one solid");

    ark::AgentSession tube;
    const Value rejected = tube.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"native_solid","name":"A","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"feature.add","type":"native_solid","name":"B","inputs":["A"],"p":{"op":"transform","tx":"6"}},
      {"op":"feature.add","type":"tube","name":"Arm","p":{"od":12,"wall":1.6,"tail":40}},
      {"op":"feature.add","type":"native_solid","name":"Joined","inputs":["A","B","Arm"],"p":{"op":"union"}}
    ]})"));
    check(rejected.find("isError") && rejected.find("isError")->b, "a tube cannot enter an exact boolean");
    check(rejected.find("structuredContent")->find("error")->str.find("B-rep") != std::string::npos, "tube rejection names the missing B-rep");
    check(tube.revision == 0 && tube.document.features.empty(), "tube boolean rolls the batch back");

    ark::AgentSession instances;
    accepted(instances.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"native_solid","name":"Cube","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"assembly.add","name":"A","feature":"Cube","fixed":true},
      {"op":"assembly.add","name":"B","feature":"Cube","fixed":true,"p":{"tx":"6"}},
      {"op":"feature.add","type":"native_solid","name":"Fused","native":{"op":"union","instances":["A","B"]}},
      {"op":"feature.add","type":"native_solid","name":"Cut","native":{"op":"cut","instances":["A","B"]}}
    ]})")));
    check(arkjson::parse(ark::documentToJson(instances.document), saved), "serialize instance boolean");
    check(saved.find("version")->num == 10, "instance boolean is version 10");
    check(std::abs(volumeOf(instances.document, 2) - 1600) < 1e-2, "instance union is 1600 mm3");
    check(std::abs(volumeOf(instances.document, 3) - 600) < 1e-2, "instance cut is 600 mm3");
}

void testSnapshotAndContract() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[{"op":"feature.add","type":"box","name":"Cube","p":{"dx":20,"dy":20,"dz":20}}]})")));
    const auto path = std::filesystem::temp_directory_path() / "ark-workflow-snapshot.png";
    Value args = Value::makeObj();
    args.set("path", Value::makeStr(path.string()));
    args.set("view", Value::makeStr("front"));
    const Value shot = accepted(session.call("snapshot", args));
    check(shot.find("view")->str == "front", "snapshot echoes the view");
    check(shot.find("display_mesh_authoritative") && !shot.find("display_mesh_authoritative")->b, "snapshot is not STEP authority");
    check(shot.find("authority")->str == "display_mesh", "snapshot names the display mesh");
    std::filesystem::remove(path);

    ark::Mesh cube = ark::makeBox({20, 20, 20}).tessellate({});
    ark::SnapCam camera;
    camera.dist = 100;
    ark::Image image = ark::renderSnapshot(cube, 320, 240, camera);
    check(image.px.size() >= 3 && image.px[0] > 180 && image.px[1] > 180 && image.px[2] > 180, "snapshot background is light");
    const size_t center = (size_t)(120 * 320 + 160) * 3;
    check(image.px[center] != image.px[0] || image.px[center + 1] != image.px[1] || image.px[center + 2] != image.px[2], "the model pixel differs from the corner");

    const Value assembly = accepted(session.call("agent_onboard", parse(R"({"topic":"assembly"})")));
    const std::string assemblyText = assembly.dump();
    check(assemblyText.find("Analytic cylinder, cone and torus meshes run along +Y") != std::string::npos, "onboarding states the analytic axis");
    check(assemblyText.find("display mesh") != std::string::npos, "onboarding names the display mesh");
    const Value solids = accepted(session.call("agent_onboard", parse(R"({"topic":"solids"})")));
    check(solids.dump().find("version 10") != std::string::npos, "solids onboarding names version 10");
    check(solids.find("version")->str == "0.9.0", "product version stays 0.9.0");
    check(ark::AgentSession::tools().arr.size() == 34, "tool count stays 34");
}
}

int main() {
    try {
        testNames();
        testPatterns();
        testInterference();
        testCandidate();
        testExactBooleans();
        testSnapshotAndContract();
        std::cout << "Modeling workflow tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
