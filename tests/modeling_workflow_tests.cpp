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
    check(arkjson::parse(ark::documentToJson(session.document,true,false), saved), "serialize pattern document");
    check(saved.find("version")->num == 10, "instance pattern is version 10");
    std::string older = ark::documentToJson(session.document,true,false);
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
    check(arkjson::parse(ark::documentToJson(pair.document,true,false), saved), "serialize two-input union");
    check(saved.find("version")->num == 7, "two-input union stays version 7");
    check(std::abs(volumeOf(pair.document, 3) - 1600) < 1e-2, "two overlapping cubes union to 1600 mm3");

    ark::AgentSession triple;
    accepted(triple.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"native_solid","name":"A","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"feature.add","type":"native_solid","name":"B","inputs":["A"],"p":{"op":"transform","tx":"6"}},
      {"op":"feature.add","type":"native_solid","name":"C","inputs":["A"],"p":{"op":"transform","tx":"12"}},
      {"op":"feature.add","type":"native_solid","name":"Joined","inputs":["A","B","C"],"p":{"op":"union"}}
    ]})")));
    check(arkjson::parse(ark::documentToJson(triple.document,true,false), saved), "serialize three-input union");
    check(saved.find("version")->num == 10, "three-input union is version 10");
    check(std::abs(volumeOf(triple.document, 4) - 2200) < 1e-2, "three cubes union to 2200 mm3");
    check(bodyData(triple.document, 4)->find("report")->find("solids")->num == 1, "the union is one solid");

    ark::AgentSession mesh;
    const Value rejected = mesh.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"native_solid","name":"A","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"feature.add","type":"native_solid","name":"B","inputs":["A"],"p":{"op":"transform","tx":"6"}},
      {"op":"feature.add","type":"mesh_body","name":"Arm","mesh":{"positions":[0,0,0,12,0,0,0,8,0],"indices":[0,1,2]}},
      {"op":"feature.add","type":"native_solid","name":"Joined","inputs":["A","B","Arm"],"p":{"op":"union"}}
    ]})"));
    check(rejected.find("isError") && rejected.find("isError")->b, "a display mesh cannot enter an exact boolean");
    check(rejected.find("structuredContent")->find("error")->str.find("B-rep") != std::string::npos, "display mesh rejection names the missing B-rep");
    check(mesh.revision == 0 && mesh.document.features.empty(), "display mesh boolean rolls the batch back");

    ark::AgentSession instances;
    accepted(instances.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"native_solid","name":"Cube","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"assembly.add","name":"A","feature":"Cube","fixed":true},
      {"op":"assembly.add","name":"B","feature":"Cube","fixed":true,"p":{"tx":"6"}},
      {"op":"feature.add","type":"native_solid","name":"Fused","native":{"op":"union","instances":["A","B"]}},
      {"op":"feature.add","type":"native_solid","name":"Cut","native":{"op":"cut","instances":["A","B"]}}
    ]})")));
    check(arkjson::parse(ark::documentToJson(instances.document,true,false), saved), "serialize instance boolean");
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
    check(ark::AgentSession::tools().arr.size() == 66, "engineering module and exchange tools are discoverable");
    for (const auto& tool : ark::AgentSession::tools().arr)
        check(tool.find("inputSchema") && tool.find("inputSchema")->t == Value::Obj, "every tool schema parses");
}

std::string errorOf(const Value& response) {
    const Value* payload = response.find("structuredContent");
    if (payload && payload->find("error")) return payload->find("error")->str;
    return {};
}

const Value* referenceNamed(const Value& topology, const std::string& name) {
    const Value* rows = topology.find("references");
    if (!rows) return nullptr;
    for (const auto& row : rows->arr) if (row.find("name") && row.find("name")->str == name) return &row;
    return nullptr;
}

std::string topologyDump(const Value& topology) {
    std::string text;
    const Value* rows = topology.find("references");
    if (!rows) return "no references";
    for (const auto& row : rows->arr) {
        text += row.find("name")->str + ":" + row.find("status")->str;
        if (const Value* indices = row.find("indices")) for (const auto& index : indices->arr) text += "#" + std::to_string(index.num);
        text += " ";
    }
    return text;
}

void testMergedFaces() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"sketch.set","name":"Low","sketch":{"entities":[{"name":"skin","kind":"spline","closed":true,"dof":[-24,-14,24,-14,24,14,-24,14]}]}},
      {"op":"plane.set","name":"Upper","p":{"oz":"28"}},
      {"op":"sketch.set","name":"High","sketch":{"plane":"Upper","entities":[{"name":"skin","kind":"spline","closed":true,"dof":[-16,-10,16,-10,16,10,-16,10]}]}},
      {"op":"feature.add","type":"loft","name":"Pod","p":{"solid":"1","ruled":"0","smooth":"1","compatible":"1"},"native":{"sections":["Low","High"]}}
    ]})")));
    Value inspected = accepted(session.call("reference_inspect", parse(R"({"id":1})")));
    const Value* topology = inspected.find("topology");
    check(topology != nullptr, "loft publishes topology");
    const Value* alias = referenceNamed(*topology, "side.skin");
    if (!alias || alias->find("status")->str != "merged")
        throw std::runtime_error("side.skin was not merged: " + topologyDump(*topology));
    check(alias->find("indices")->arr.size() == 1, "merged alias names one face");
    check(!alias->find("surviving")->str.empty(), "merged alias names the surviving face");
    check(alias->find("merged_from")->arr.size() >= 2, "merged alias lists the authored faces");
    const auto revision = session.revision;
    const auto saved = ark::documentToJson(session.document);
    const Value draft = session.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"native_solid","name":"Taper","inputs":["Pod"],"p":{"op":"draft","angle":"3"},"native":{"direction":"Z","neutral":"XY","faces":["side.skin"]}}
    ]})"));
    const std::string draftError = errorOf(draft);
    check(draftError.find("Unknown semantic") == std::string::npos, "draft accepts the merged face alias");
    check(draftError.find("Draft rejected") != std::string::npos || draftError.find("no analytic origin") != std::string::npos || !(draft.find("isError") && draft.find("isError")->b), "freeform alias resolves");
    check(session.revision == revision && ark::documentToJson(session.document) == saved, "rejected draft keeps the authored loft");
    const Value datum = session.call("doc_apply", parse(R"({"commands":[
      {"op":"plane.set","name":"Skin","support":{"kind":"face","feature":1,"reference":"side.skin"}}
    ]})"));
    check(errorOf(datum).find("Unknown semantic") == std::string::npos, "plane support accepts the merged face alias");
}

void testRoundTube() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"tube","name":"Arm","p":{"od":12,"wall":1.6,"tail":40}},
      {"op":"feature.add","type":"native_solid","name":"Hub","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"feature.add","type":"native_solid","name":"Joined","inputs":["Arm","Hub"],"p":{"op":"union"}}
    ]})")));
    Value saved;
    check(arkjson::parse(ark::documentToJson(session.document,true,false), saved), "serialize round-tube union");
    check(saved.find("version")->num == 7, "round-tube union of two inputs stays version 7");
    Value axis = accepted(session.call("reference_inspect", parse(R"({"id":1,"reference":"axis"})")));
    check(axis.find("world_column_major")->arr[8].num > 0.99, "round tube axis starts along +X");
    const double tubeVolume = volumeOf(session.document, 1);
    check(std::abs(tubeVolume - 2090.97) < 1.0, "straight round tube volume");
    check(bodyData(session.document, 3)->find("report")->find("solids")->num == 1, "round tube union is one solid");
    const double joined = volumeOf(session.document, 3);
    check(joined > tubeVolume + 1.0 && joined < tubeVolume + 1000.0, "round tube enters the exact union");
    const auto step = std::filesystem::temp_directory_path() / "ark-round-tube.step";
    Value args = Value::makeObj();
    args.set("id", Value::makeNum(1));
    args.set("path", Value::makeStr(step.string()));
    accepted(session.call("export_step", args));
    std::filesystem::remove(step);
    std::filesystem::path sidecar = step;
    sidecar.replace_extension(".validation.json");
    std::filesystem::remove(sidecar);

    ark::AgentSession mate;
    accepted(mate.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"tube","name":"Arm","p":{"od":12,"wall":1.6,"tail":40}},
      {"op":"feature.add","type":"box","name":"Hub","p":{"dx":10,"dy":10,"dz":10}},
      {"op":"assembly.add","name":"Hub","feature":"Hub","fixed":true},
      {"op":"assembly.add","name":"Arm","feature":"Arm","p":{"ty":"30"}},
      {"op":"mate.add","a":"Hub","b":"Arm","type":"concentric","reference_a":"X","reference_b":"axis"}
    ]})")));
    Value info = accepted(mate.call("assembly_info", Value::makeObj()));
    check(info.find("diagnostics")->find("max_position_residual_mm")->num < 1e-4, "concentric mate accepts the round tube axis");
}

void testShell() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"native_solid","name":"Cube","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"feature.add","type":"native_solid","name":"Case","inputs":["Cube"],"p":{"op":"shell","thickness":"-1"},"native":{"faces":["z_max"]}}
    ]})")));
    check(std::abs(volumeOf(session.document, 2) - 424.0) < 2.0, "open shell of a 10 mm cube is about 424 mm3");
    Value saved;
    check(arkjson::parse(ark::documentToJson(session.document,true,false), saved), "serialize shell");
    check(saved.find("version")->num == 11, "shell is version 11");
    check(ark::documentToJson(session.document).find("z_max") != std::string::npos, "shell stores the authored face name");
    std::string older = ark::documentToJson(session.document,true,false);
    const auto at = older.find("\"version\":11");
    check(at != std::string::npos, "shell version token");
    older.replace(at, std::string("\"version\":11").size(), "\"version\":10");
    ark::Document rejected;
    std::string error;
    check(!ark::documentFromJson(older, rejected, &error), "version 10 rejects a shell");
    check(error.find("version 11") != std::string::npos, "shell rejection names version 11");
}

void testClearanceAndMeasure() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"native_solid","name":"Cube","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"assembly.add","name":"A","feature":"Cube","fixed":true},
      {"op":"assembly.add","name":"B","feature":"Cube","fixed":true,"p":{"tx":"6"}}
    ]})")));
    const auto before = ark::documentToJson(session.document);
    const auto revision = session.revision;
    Value info = accepted(session.call("assembly_info", parse(R"({"clearance":{"a":"A","b":"B"}})")));
    check(info.find("clearance_checked") && info.find("clearance_checked")->b, "exact clearance runs when both bodies have a B-rep");
    check(std::abs(info.find("clearance_mm")->num + 4.0) < 0.05, "overlapping cubes penetrate by 4 mm");
    check(info.find("clearance_state")->str == "interfere", "overlap state is interfere");
    check(!info.find("face_a")->str.empty() && !info.find("face_b")->str.empty(), "clearance names both faces");
    check(session.revision == revision && ark::documentToJson(session.document) == before, "clearance does not change the document");
    accepted(session.call("doc_apply", parse(R"({"commands":[{"op":"assembly.edit","id":"B","p":{"tx":"30"}}]})")));
    info = accepted(session.call("assembly_info", parse(R"({"clearance":{"a":1,"b":"B"}})")));
    check(std::abs(info.find("clearance_mm")->num - 20.0) < 0.05, "separated cubes have 20 mm of clearance");
    check(info.find("clearance_state")->str == "separate", "gap state is separate");
    const auto separated = ark::documentToJson(session.document);
    const auto separatedRevision = session.revision;
    info = accepted(session.call("assembly_info", parse(R"({"measure":{"a":{"node":"A","reference":"origin"},"b":{"node":"B","reference":"origin"}}})")));
    const Value* measure = info.find("measure");
    check(measure && std::abs(measure->find("distance_mm")->num - 30.0) < 1e-4, "measure distance is the frame origin gap");
    check(std::abs(measure->find("angle_deg")->num) < 1e-4, "parallel frames have zero angle");
    check(measure->find("frames")->arr.size() == 2, "measure returns both frames");
    check(measure->find("axis_convention")->str.find("Analytic cylinder, cone and torus meshes run along +Y") != std::string::npos, "measure states the axis convention");
    check(measure->find("axis_convention")->str.find("round tube") != std::string::npos, "measure states the round tube axis");
    check(session.revision == separatedRevision && ark::documentToJson(session.document) == separated, "measure does not change the document");
    accepted(session.call("doc_apply", parse(R"({"commands":[{"op":"assembly.edit","id":"B","p":{"tx":"0","ty":"40","rx":"90"}}]})")));
    info = accepted(session.call("assembly_info", parse(R"({"measure":{"a":{"node":"A"},"b":{"node":"B"}}})")));
    measure = info.find("measure");
    check(std::abs(measure->find("distance_mm")->num - 40.0) < 1e-3, "measure uses the placed origins");
    check(std::abs(measure->find("angle_deg")->num - 90.0) < 1e-3, "measure angle is between the +Z axes");

    ark::AgentSession missing;
    accepted(missing.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"mesh_body","name":"Arm","mesh":{"positions":[0,0,0,12,0,0,0,8,0],"indices":[0,1,2]}},
      {"op":"feature.add","type":"native_solid","name":"Cube","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"assembly.add","name":"Arm","feature":"Arm","fixed":true},
      {"op":"assembly.add","name":"Cube","feature":"Cube","fixed":true,"p":{"tx":"30"}}
    ]})")));
    const auto meshRevision = missing.revision;
    info = accepted(missing.call("assembly_info", parse(R"({"clearance":{"a":"Arm","b":"Cube"}})")));
    check(info.find("clearance_checked") && !info.find("clearance_checked")->b, "a body without a B-rep is not an exact clearance");
    check(info.find("clearance_reason") && !info.find("clearance_reason")->str.empty(), "missing B-rep explains why");
    check(missing.revision == meshRevision, "unchecked clearance keeps the revision");
    const Value bad = missing.call("assembly_info", parse(R"({"measure":{"a":{"feature":"Missing"},"b":{"feature":"Cube"}}})"));
    check(bad.find("isError") && bad.find("isError")->b, "a bad measure name fails the call");
    check(missing.revision == meshRevision, "a bad measure keeps the revision");
}

void testGuides() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"sketch.set","name":"Low","sketch":{"entities":[{"kind":"circle","dof":[0,0,10]}]}},
      {"op":"plane.set","name":"Upper","p":{"oz":"40"}},
      {"op":"sketch.set","name":"High","sketch":{"plane":"Upper","entities":[{"kind":"circle","dof":[0,0,10]}]}},
      {"op":"sketch.set","name":"Bow","sketch":{"plane":"XZ","entities":[{"kind":"line","dof":[10,0,18,20]},{"kind":"line","dof":[18,20,10,40]}]}},
      {"op":"feature.add","type":"loft","name":"Nose","p":{"solid":"1","ruled":"0"},"native":{"sections":["Low","High"],"guides":["Bow"]}}
    ]})")));
    const Value* report = bodyData(session.document, 1)->find("report");
    check(report->find("bounds_mm")->arr[3].num > 16.0, "one guide bows the loft past x=16");
    check(report->find("volume_mm3")->num > 12566.0, "guided loft is larger than the straight cylinder");
    Value saved;
    check(arkjson::parse(ark::documentToJson(session.document,true,false), saved), "serialize guided loft");
    check(saved.find("version")->num == 11, "loft guides are version 11");
    check(saved.find("features")->arr[0].find("native")->find("guides")->arr[0].str == "Bow", "the document stores the guide sketch name");

    ark::AgentSession both;
    accepted(both.call("doc_apply", parse(R"({"commands":[
      {"op":"sketch.set","name":"Low","sketch":{"entities":[{"kind":"circle","dof":[0,0,10]}]}},
      {"op":"plane.set","name":"Upper","p":{"oz":"40"}},
      {"op":"sketch.set","name":"High","sketch":{"plane":"Upper","entities":[{"kind":"circle","dof":[0,0,10]}]}},
      {"op":"sketch.set","name":"East","sketch":{"plane":"XZ","entities":[{"kind":"line","dof":[10,0,14,20]},{"kind":"line","dof":[14,20,10,40]}]}},
      {"op":"sketch.set","name":"West","sketch":{"plane":"XZ","entities":[{"kind":"line","dof":[-10,0,-14,20]},{"kind":"line","dof":[-14,20,-10,40]}]}},
      {"op":"feature.add","type":"loft","name":"Fairing","p":{"solid":"1","ruled":"0"},"native":{"sections":["Low","High"],"guides":["East","West"]}}
    ]})")));
    report = bodyData(both.document, 1)->find("report");
    check(report->find("bounds_mm")->arr[3].num > 12.0, "the east guide reaches past x=12");
    check(report->find("bounds_mm")->arr[0].num < -12.0, "the west guide reaches past x=-12");
    check(report->find("volume_mm3")->num > 12566.0, "two guides enlarge the loft");

    ark::AgentSession three;
    accepted(three.call("doc_apply", parse(R"({"commands":[
      {"op":"sketch.set","name":"Low","sketch":{"entities":[{"kind":"circle","dof":[0,0,10]}]}},
      {"op":"plane.set","name":"Upper","p":{"oz":"40"}},
      {"op":"sketch.set","name":"High","sketch":{"plane":"Upper","entities":[{"kind":"circle","dof":[0,0,10]}]}},
      {"op":"sketch.set","name":"East","sketch":{"plane":"XZ","entities":[{"kind":"line","dof":[10,0,14,20]},{"kind":"line","dof":[14,20,10,40]}]}},
      {"op":"sketch.set","name":"West","sketch":{"plane":"XZ","entities":[{"kind":"line","dof":[-10,0,-14,20]},{"kind":"line","dof":[-14,20,-10,40]}]}},
      {"op":"sketch.set","name":"North","sketch":{"plane":"YZ","entities":[{"kind":"line","dof":[10,0,16,20]},{"kind":"line","dof":[16,20,10,40]}]}},
      {"op":"feature.add","type":"loft","name":"Fairing","p":{"solid":"1","ruled":"0"},"native":{"sections":["Low","High"],"guides":["East","West","North"]}}
    ]})")));
    report = bodyData(three.document, 1)->find("report");
    check(report->find("bounds_mm")->arr[3].num > 12.0, "three guides keep the east bow");
    check(report->find("bounds_mm")->arr[0].num < -12.0, "three guides keep the west bow");
    check(report->find("bounds_mm")->arr[4].num > 14.0, "the north guide reaches past y=14");

    ark::AgentSession empty;
    const Value rejected = empty.call("doc_apply", parse(R"({"commands":[
      {"op":"sketch.set","name":"Low","sketch":{"entities":[{"kind":"circle","dof":[0,0,10]}]}},
      {"op":"plane.set","name":"Upper","p":{"oz":"40"}},
      {"op":"sketch.set","name":"High","sketch":{"plane":"Upper","entities":[{"kind":"circle","dof":[0,0,10]}]}},
      {"op":"feature.add","type":"loft","name":"Bare","p":{"solid":"1","ruled":"0"},"native":{"sections":["Low","High"],"guides":[]}}
    ]})"));
    check(rejected.find("isError") && rejected.find("isError")->b, "empty guides fail");
    check(errorOf(rejected).find("1..8") != std::string::npos, "empty guides name the limit");
    check(empty.revision == 0, "empty guides roll the batch back");
}

void testNameEdits() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"box","name":"Block","p":{"dx":10,"dy":10,"dz":10}},
      {"op":"feature.add","type":"box","name":"Spare","p":{"dx":4,"dy":4,"dz":4}},
      {"op":"feature.edit","id":"Block","p":{"dx":20}},
      {"op":"assembly.add","name":"Group","feature":0},
      {"op":"assembly.add","name":"Pod","feature":"Block"},
      {"op":"assembly.edit","id":"Pod","parent":"Group","p":{"tx":"5"}},
      {"op":"assembly.add","name":"A","feature":"Block","fixed":true},
      {"op":"assembly.add","name":"B","feature":"Block","fixed":true,"p":{"tz":"4"}},
      {"op":"mate.add","a":"A","b":"B","type":"distance","value":"4"},
      {"op":"mate.edit","a":"B","b":"A","type":"angle","value":"0"}
    ]})")));
    check(session.document.features[0].p.at("dx") == "20", "feature.edit accepts a unique name");
    check(session.document.assembly[1].parent == session.document.assembly[0].id, "assembly.edit parent accepts a group name");
    check(session.document.assembly[1].p.at("tx") == "5", "assembly.edit id accepts a unique name");
    check(session.document.mates.size() == 1 && session.document.mates[0].type == "angle" && session.document.mates[0].value == "0", "mate.edit selects by endpoints in either order");
    check((session.document.mates[0].a == 3 && session.document.mates[0].b == 4) || (session.document.mates[0].a == 4 && session.document.mates[0].b == 3), "mate.edit keeps numeric endpoint ids");
    accepted(session.call("doc_apply", parse(R"({"commands":[{"op":"mate.remove","a":"A","b":"B"}]})")));
    check(session.document.mates.empty(), "mate.remove selects by endpoints");
    accepted(session.call("doc_apply", parse(R"({"commands":[{"op":"feature.remove","id":"Spare"}]})")));
    check(session.document.features.size() == 1 && session.document.features[0].name == "Block", "feature.remove accepts a unique name");
    const Value textId = session.call("doc_apply", parse(R"({"commands":[{"op":"mate.edit","id":"A","type":"coincident"}]})"));
    check(errorOf(textId).find("numeric") != std::string::npos, "a string mate id asks for endpoints");
}

void testSnapshotCopy() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"box","name":"Motor","p":{"dx":10,"dy":10,"dz":10}},
      {"op":"assembly.add","name":"Motors","feature":"Motor","p":{"tx":"120"},"pattern":{"type":"circular","count":"4","step":"90","axis":"Z"}}
    ]})")));
    Value all = accepted(session.call("mesh_inspect", parse(R"({"node":"Motors"})")));
    check(all.find("triangles")->num == 48, "a node name without copy includes every placement");
    Value first = accepted(session.call("mesh_inspect", parse(R"({"node":"Motors","copy":0})")));
    Value second = accepted(session.call("mesh_inspect", parse(R"({"node":"Motors","copy":1})")));
    check(first.find("triangles")->num == 12 && second.find("triangles")->num == 12, "copy frames one motor");
    check(first.find("bounds_mm")->find("max")->arr[0].num > 120.0, "copy 0 stays on the +X arm");
    check(second.find("bounds_mm")->find("max")->arr[0].num < 20.0, "copy 1 is the rotated placement");
    const auto path = std::filesystem::temp_directory_path() / "ark-workflow-copy.png";
    Value args = Value::makeObj();
    args.set("path", Value::makeStr(path.string()));
    args.set("node", Value::makeStr("Motors"));
    args.set("copy", Value::makeNum(1));
    Value shot = accepted(session.call("snapshot", args));
    check(shot.find("node")->num == 1 && shot.find("name")->str == "Motors" && shot.find("copy")->num == 1, "snapshot echoes the node name and copy");
    check(shot.find("authority")->str == "display_mesh", "the copy image stays display evidence");
    std::filesystem::remove(path);
}

void testCurvesAndQueries() {
    const double pi = std::acos(-1.0);
    ark::AgentSession arc;
    accepted(arc.call("doc_apply", parse(R"({"commands":[
      {"op":"sketch.set","name":"Profile","sketch":{"entities":[
        {"name":"Base","kind":"line","dof":[0,0,20,0]},
        {"name":"Rim","kind":"arc","dof":[0,8,"5","-90","90"]}
      ],"constraints":[
        {"type":"fixed","a":0,"pa":0,"val":0,"val2":0},
        {"type":"fixed","a":0,"pa":1,"val":20,"val2":0},
        {"type":"tangent","a":0,"b":1}
      ]}}
    ]})")));
    const ark::NamedSketch* profile = arc.document.findSketch("Profile");
    check(profile && profile->sk.ents.size() == 2, "arc sketch");
    const auto& base = profile->sk.ents[0];
    const auto& rim = profile->sk.ents[1];
    check(std::abs(base.dof[0]) < 1e-6 && std::abs(base.dof[1]) < 1e-6 && std::abs(base.dof[2] - 20.0) < 1e-6 && std::abs(base.dof[3]) < 1e-6, "the fixed line stays horizontal");
    check(std::abs(rim.dof[1] - 5.0) < 1e-3, "tangent pulls the arc center to the radius");
    check(std::abs(rim.dof[2] - 5.0) < 1e-6, "the radius expression stays 5");
    Value measured = accepted(arc.call("sketch_inspect", parse(R"({"name":"Profile","entity":"Rim"})")));
    check(measured.find("entities") == nullptr, "an entity query does not dump every dof");
    check(std::abs(measured.find("length_mm")->num - pi * 5.0) < 1e-3, "semicircle length is pi*r");
    check(measured.find("start_tangent")->arr[0].num > 0.99 && std::abs(measured.find("start_tangent")->arr[1].num) < 1e-3, "the semicircle starts along +X");

    ark::AgentSession wire;
    accepted(wire.call("doc_apply", parse(R"({"commands":[
      {"op":"sketch.set","name":"Path","sketch":{"entities":[{"name":"East","kind":"wire","start":[0,20],"segments":[{"kind":"line","to":[14,20]},{"kind":"line","to":[30,0]}]}]}},
      {"op":"sketch.patch","name":"Path","entity":"East","anchor":1,"x":"18"}
    ]})")));
    const ark::NamedSketch* path = wire.document.findSketch("Path");
    check(path && path->sk.ents.size() == 1 && path->sk.ents[0].dof.size() >= 4, "patched wire");
    check(std::abs(path->sk.ents[0].dof[2] - 18.0) < 1e-6, "sketch.patch moves vertex x");
    check(path->sk.ents[0].expressions.size() > 2 && path->sk.ents[0].expressions[2] == "18", "sketch.patch stores the expression");
    check(std::abs(path->sk.ents[0].dof[3] - 20.0) < 1e-6, "sketch.patch leaves the vertex y");

    ark::AgentSession marks;
    accepted(marks.call("doc_apply", parse(R"({"commands":[
      {"op":"sketch.set","name":"Marks","sketch":{"entities":[{"name":"tip","kind":"point","dof":[3,4]}]}}
    ]})")));
    Value point = accepted(marks.call("reference_inspect", parse(R"({"sketch":"Marks","reference":"point.tip"})")));
    check(point.find("kind")->str == "point", "a sketch point is a point frame");
    const auto& origin = point.find("world_column_major")->arr;
    check(std::abs(origin[12].num - 3.0) < 1e-6 && std::abs(origin[13].num - 4.0) < 1e-6 && std::abs(origin[14].num) < 1e-6, "point.tip is the world sketch point");

    ark::AgentSession cone;
    accepted(cone.call("doc_apply", parse(R"({"commands":[
      {"op":"sketch.set","name":"Base","sketch":{"entities":[{"kind":"circle","dof":[0,0,5]}]}},
      {"op":"plane.set","name":"Tip","kind":"point","p":{"oz":"30"}},
      {"op":"feature.add","type":"loft","name":"Cone","p":{"solid":"1","ruled":"1"},"native":{"sections":["Base","Tip"]}}
    ]})")));
    check(volumeOf(cone.document, 1) > 700.0 && volumeOf(cone.document, 1) < 900.0, "a circle lofted to a point is a cone");
    check(bodyData(cone.document, 1)->find("report")->find("bounds_mm")->arr[5].num > 29.0, "the point tip reaches z=30");
    Value saved;
    check(arkjson::parse(ark::documentToJson(cone.document,true,false), saved), "serialize the point loft");
    check(saved.find("version")->num == 12, "a point datum saves version 12");
    std::string older = ark::documentToJson(cone.document,true,false);
    const auto at = older.find("\"version\":12");
    check(at != std::string::npos, "version 12 token");
    older.replace(at, std::string("\"version\":12").size(), "\"version\":11");
    ark::Document rejected;
    std::string error;
    check(!ark::documentFromJson(older, rejected, &error), "version 11 rejects a point datum");
    check(error.find("version 12") != std::string::npos, "point rejection names version 12");

    ark::AgentSession guided;
    accepted(guided.call("doc_apply", parse(R"({"commands":[
      {"op":"sketch.set","name":"Low","sketch":{"entities":[{"kind":"circle","dof":[0,0,10]}]}},
      {"op":"plane.set","name":"Upper","p":{"oz":"40"}},
      {"op":"sketch.set","name":"High","sketch":{"plane":"Upper","entities":[{"kind":"circle","dof":[0,0,10]}]}},
      {"op":"feature.add","type":"curve","name":"Bow","native":{"start":[10,0,0],"segments":[{"kind":"line","to":[18,0,20]},{"kind":"line","to":[10,0,40]}]}},
      {"op":"feature.add","type":"loft","name":"Nose","p":{"solid":"1","ruled":"0"},"native":{"sections":["Low","High"],"guides":["Bow"]}}
    ]})")));
    const Value* bow = bodyData(guided.document, 1)->find("report");
    check(bow->find("curve") && bow->find("curve")->b, "a curve feature reports a curve");
    check(std::abs(bow->find("display_ribbon_mm")->num - 0.3) < 1e-9, "the curve ribbon is 0.3 mm of display evidence");
    check(bow->find("display_mesh_authoritative") && !bow->find("display_mesh_authoritative")->b, "the curve mesh is not the authority");
    check(bow->find("length_mm")->num > 40.0, "the bow is longer than its span");
    const Value* nose = bodyData(guided.document, 2)->find("report");
    check(nose->find("bounds_mm")->arr[3].num > 16.0, "a curve guide bows the loft past x=16");
    check(nose->find("volume_mm3")->num > 12566.0, "a curve guide enlarges the loft");
    check(arkjson::parse(ark::documentToJson(guided.document,true,false), saved) && saved.find("version")->num == 12, "a curve guide saves version 12");

    ark::AgentSession swept;
    accepted(swept.call("doc_apply", parse(R"({"commands":[
      {"op":"sketch.set","name":"Section","sketch":{"entities":[{"kind":"circle","dof":[0,0,2]}]}},
      {"op":"feature.add","type":"curve","name":"Path","native":{"start":[0,0,0],"segments":[{"kind":"line","to":[0,0,20]}]}},
      {"op":"feature.add","type":"sweep","name":"Rod","p":{"solid":"1"},"native":{"section":"Section","path":"Path"}}
    ]})")));
    check(volumeOf(swept.document, 2) > 200.0 && volumeOf(swept.document, 2) < 400.0, "a circle swept along a straight curve");

    ark::AgentSession sliced;
    accepted(sliced.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"native_solid","name":"Cube","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"plane.set","name":"Mid","p":{"oz":"5"}},
      {"op":"feature.add","type":"intersection_curve","name":"Cut","inputs":["Cube"],"native":{"plane":"Mid"}}
    ]})")));
    const Value* cut = bodyData(sliced.document, 2)->find("report");
    check(cut->find("bounds_mm")->arr[3].num > 9.0, "the section spans the box in x");
    check(std::abs(cut->find("length_mm")->num - 40.0) < 1.0, "the midplane section is one 40 mm wire");
    check(std::abs(cut->find("bounds_mm")->arr[2].num - 5.0) < 1.0 && std::abs(cut->find("bounds_mm")->arr[5].num - 5.0) < 1.0, "the section stays on z=5");

    ark::AgentSession span;
    accepted(span.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"native_solid","name":"Cube","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"feature.add","type":"bridge","name":"Span","p":{"continuity":"1"},"native":{
        "a":{"feature":"Cube","reference":"edge.x.ymin.zmin","fraction":0},
        "b":{"feature":"Cube","reference":"edge.x.ymin.zmax","fraction":0}
      }}
    ]})")));
    const Value* bridge = bodyData(span.document, 2)->find("report");
    const Value* samples = bodyData(span.document, 2)->find("mesh")->find("edges");
    if (!samples || samples->arr.empty()) throw std::runtime_error("the bridge has no sampled edge");
    if (samples->arr[0].arr.size() < 9 || samples->arr[0].arr.size() % 3)
        throw std::runtime_error("the bridge sample has " + std::to_string(samples->arr[0].arr.size()/3) + " points");
    double firstHalf = 0;
    const auto& poly = samples->arr[0].arr;
    const size_t pointCount = poly.size() / 3, mid = pointCount / 2;
    double secondHalf = 0, sampledLength = 0;
    for (size_t i = 0; i < pointCount; ++i) {
        const double x = poly[i * 3].num, y = poly[i * 3 + 1].num, z = poly[i * 3 + 2].num;
        check(std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
              std::abs(y) < 1e-6 && z >= -1e-6 && z <= 10 + 1e-6,
              "the bridge edge stays finite on its authored XZ plane");
        if (i <= mid) firstHalf = std::max(firstHalf, x - poly[0].num);
        if (i >= mid) secondHalf = std::min(secondHalf, x - poly[0].num);
        if (i) {
            const double dx = x - poly[(i - 1) * 3].num;
            const double dz = z - poly[(i - 1) * 3 + 2].num;
            sampledLength += std::hypot(dx, dz);
        }
    }
    check(std::abs(poly[0].num) < 1e-6 && std::abs(poly[2].num) < 1e-6 &&
          std::abs(poly[poly.size() - 3].num) < 1e-6 && std::abs(poly.back().num - 10) < 1e-6,
          "the bridge samples retain both exact endpoints");
    check(firstHalf > 0.5, "the bridge leaves along +X");
    check(secondHalf < -0.5, "the bridge arrives from the authored +X tangent");
    check(sampledLength > 10.1 && sampledLength <= bridge->find("length_mm")->num + 1e-4,
          "the adaptive edge samples capture the curved bridge without exceeding its exact length");
    check(bridge->find("length_mm")->num > 10.0, "the bridge is longer than the edge gap");
    check(bridge->find("continuity")->num == 1.0 && bridge->find("curvature_matched") && !bridge->find("curvature_matched")->b, "continuity 1 is tangent only");

    ark::AgentSession design;
    accepted(design.call("doc_apply", parse(R"({"commands":[
      {"op":"param.set","name":"width","expr":"20"},
      {"op":"feature.add","type":"native_solid","name":"Cube","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"assembly.add","name":"Motors","feature":"Cube","pattern":{"type":"circular","count":"4","step":"90","axis":"Z"}},
      {"op":"plane.set","name":"Station","support":{"kind":"on_curve","feature":1,"reference":"edge.z.xmin.ymin","fraction":0.5}}
    ]})")));
    const auto revision = design.revision;
    const auto before = ark::documentToJson(design.document);
    Value view = accepted(design.call("doc_info", parse(R"({"detail":"design"})")));
    check(view.find("parameters")->arr.size() == 1 && view.find("parameters")->arr[0].find("name")->str == "width", "design lists the parameter");
    check(std::abs(view.find("parameters")->arr[0].find("value")->num - 20.0) < 1e-9, "design returns the parameter value");
    check(view.find("parameters")->arr[0].find("expr") == nullptr, "design omits the expression");
    const Value* cube = nullptr;
    for (const auto& feature : view.find("features")->arr) if (feature.find("name")->str == "Cube") cube = &feature;
    check(cube && std::abs(cube->find("volume_mm3")->num - 1000.0) < 1e-2, "design reports the box volume");
    check(view.find("assembly")->arr.size() == 1 && view.find("assembly")->arr[0].find("copies")->num == 4, "design reports pattern copies");
    Value found = accepted(design.call("reference_inspect", parse(R"({"id":1,"find":{"kind":"planar","normal":"Z"}})")));
    check(found.find("reference")->str == "z_max", "find returns the +Z face");
    Value outside = accepted(design.call("native_inspect", parse(R"({"id":1,"point":[5,5,20]})")));
    check(std::abs(outside.find("distance_mm")->num - 10.0) < 0.1 && outside.find("face")->str == "z_max", "a point above the cube is 10 mm from z_max");
    Value inside = accepted(design.call("native_inspect", parse(R"({"id":1,"point":[5,5,5]})")));
    check(inside.find("distance_mm")->num < -4.0 && inside.find("distance_mm")->num > -6.0, "a point inside the cube is negative");
    Value station = accepted(design.call("plane_inspect", parse(R"({"name":"Station"})")));
    check(std::abs(station.find("world_column_major")->arr[14].num - 5.0) < 0.05, "on_curve fraction 0.5 sits at z=5");
    check(design.revision == revision && ark::documentToJson(design.document) == before, "numeric queries do not change the document");

    check(accepted(design.call("agent_onboard", parse(R"({"topic":"sketch"})"))).dump().find("sketch.patch") != std::string::npos, "sketch onboarding names sketch.patch");
    check(accepted(design.call("agent_onboard", parse(R"({"topic":"surfacing"})"))).dump().find("zero end curvature") != std::string::npos, "surfacing onboarding names zero end curvature");
    check(accepted(design.call("agent_onboard", parse(R"({"topic":"overview"})"))).dump().find("detail design") != std::string::npos, "workflow onboarding names detail design");
}
}

void testDrivenSketch() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"native_solid","name":"Cube","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"sketch.set","name":"Trace","sketch":{"plane":"XY","entities":[
        {"kind":"project","name":"Edge","feature":"Cube","reference":"edge.x.ymin.zmin"},
        {"kind":"project","name":"Top","feature":"Cube","reference":"z_max"},
        {"kind":"project","name":"Side","feature":"Cube","reference":"y_max"},
        {"kind":"offset","name":"Beside","source":"Edge","distance":"2"}
      ]}}
    ]})")));
    check(std::abs(volumeOf(session.document, 1) - 1000.0) < 1.0, "projecting the cube does not change its volume");
    Value saved;
    check(arkjson::parse(ark::documentToJson(session.document,true,false), saved) && saved.find("version")->num == 13.0, "project and offset save as version 13");
    auto entity = [&](const char* name) {
        std::string args = std::string("{\"name\":\"Trace\",\"entity\":\"") + name + "\"}";
        return accepted(session.call("sketch_inspect", parse(args.c_str())));
    };
    Value edge = entity("Edge");
    const auto& edgeBox = edge.find("bbox_mm")->arr;
    check(std::abs(edge.find("length_mm")->num - 10.0) < 0.05, "projected edge is 10 mm");
    check(std::abs(edgeBox[1].num) < 0.05 && std::abs(edgeBox[3].num) < 0.05, "projected edge lies on y=0");
    Value top = entity("Top");
    const auto& topBox = top.find("bbox_mm")->arr;
    check(std::abs(top.find("length_mm")->num - 40.0) < 0.2, "projected top perimeter is 40 mm");
    check(topBox[2].num - topBox[0].num > 9.5 && topBox[3].num - topBox[1].num > 9.5, "projected top covers the 10 mm face");
    Value side = entity("Side");
    const auto& sideBox = side.find("bbox_mm")->arr;
    check(std::abs(side.find("length_mm")->num - 10.0) < 0.05, "y_max meets the sketch plane in a 10 mm segment");
    check(std::abs(sideBox[1].num - 10.0) < 0.05 && std::abs(sideBox[3].num - 10.0) < 0.05, "the y_max intersection lies at y=10");
    Value beside = entity("Beside");
    const auto& besideBox = beside.find("bbox_mm")->arr;
    check(std::abs(beside.find("length_mm")->num - 10.0) < 0.05, "the offset of the projected edge stays 10 mm");
    check(std::abs(std::abs(besideBox[1].num) - 2.0) < 0.05 && std::abs(besideBox[1].num - besideBox[3].num) < 0.05, "the offset stays 2 mm away and horizontal");
    const auto revision = session.revision;
    const auto before = ark::documentToJson(session.document);
    Value blocked = session.call("doc_apply", parse(R"({"commands":[{"op":"feature.edit","id":"Cube","suppressed":true}]})"));
    check(blocked.find("isError") && blocked.find("isError")->b, "suppressing a projected source fails");
    check(blocked.find("structuredContent")->find("error")->str.find("Project source is suppressed") != std::string::npos, "the error names the suppressed project source");
    check(session.revision == revision && ark::documentToJson(session.document) == before, "the failed suppress rolls back");
}

void testRectangularTube() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"tube","name":"Arm","p":{"section":"rect","width":20,"height":10,"wall":1,"tail":40}},
      {"op":"feature.add","type":"native_solid","name":"Hub","p":{"op":"box","width":"10","depth":"10","height":"10"}},
      {"op":"feature.add","type":"native_solid","name":"Joined","inputs":["Arm","Hub"],"p":{"op":"union"}}
    ]})")));
    Value saved;
    check(arkjson::parse(ark::documentToJson(session.document,true,false), saved), "serialize rectangular-tube union");
    check(saved.find("version")->num == 7, "rectangular-tube union of two inputs stays version 7");
    check(std::abs(volumeOf(session.document, 1) - 2240.0) < 1.0, "straight rectangular tube volume is 2240 mm3");
    const auto& bounds = bodyData(session.document, 1)->find("report")->find("bounds_mm")->arr;
    check(bounds[0].num > -0.05 && bounds[3].num > 39.9 && bounds[3].num < 40.05, "rectangular tube runs from x 0 to length");
    check(bounds[1].num > -10.05 && bounds[4].num < 10.05 && bounds[4].num > 9.9, "width lies along Y");
    check(bounds[2].num > -5.05 && bounds[5].num < 5.05 && bounds[5].num > 4.9, "height lies along Z");
    Value axis = accepted(session.call("reference_inspect", parse(R"({"id":1,"reference":"axis"})")));
    check(axis.find("world_column_major")->arr[8].num > 0.99, "rectangular tube axis starts along +X");
    check(bodyData(session.document, 3)->find("report")->find("solids")->num == 1, "rectangular tube union is one solid");
}

void testMass() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"box","name":"Cube","p":{"dx":"10","dy":"10","dz":"10","density":"2.7"}}
    ]})")));
    Value saved;
    check(arkjson::parse(ark::documentToJson(session.document,true,false), saved) && saved.find("version")->num == 1, "density does not change the document version");
    Value inspected = accepted(session.call("native_inspect", parse(R"({"id":1})")));
    check(std::abs(inspected.find("mass_g")->num - 2.7) < 1e-6, "a 10 mm cube at 2.7 g/cm3 weighs 2.7 g");
    const auto& center = inspected.find("center_mm")->arr;
    check(std::abs(center[0].num - 5) < 1e-6 && std::abs(center[1].num - 5) < 1e-6 && std::abs(center[2].num - 5) < 1e-6, "box centroid is the center");
    ark::AgentSession plainSession;
    accepted(plainSession.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"box","name":"Plain","p":{"dx":"10","dy":"10","dz":"10"}}
    ]})")));
    inspected = accepted(plainSession.call("native_inspect", parse(R"({"id":1})")));
    check(!inspected.find("mass_g") && !inspected.find("center_mm"), "mass stays absent without density");

    ark::AgentSession row;
    accepted(row.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"box","name":"Cube","p":{"dx":"10","dy":"10","dz":"10","density":"1"}},
      {"op":"assembly.add","name":"Row","feature":"Cube","pattern":{"type":"linear","count":"2","dx":"10"}}
    ]})")));
    Value info = accepted(row.call("assembly_info", Value::makeObj()));
    check(info.find("mass_checked") && info.find("mass_checked")->b, "uniform density reports a checked mass");
    check(std::abs(info.find("mass_g")->num - 2.0) < 1e-4, "two 10 mm cubes at 1 g/cm3 weigh 2 g");
    check(std::abs(info.find("center_mm")->arr[0].num - 10.0) < 1e-4, "the pattern center is the mass-weighted centroid");
    accepted(row.call("doc_apply", parse(R"({"commands":[
      {"op":"feature.add","type":"box","name":"Bare","p":{"dx":"10","dy":"10","dz":"10"}},
      {"op":"assembly.add","name":"Bare","feature":"Bare","fixed":true,"p":{"ty":"30"}}
    ]})")));
    info = accepted(row.call("assembly_info", Value::makeObj()));
    check(info.find("mass_checked") && !info.find("mass_checked")->b, "a missing density is not a partial mass");
    check(info.find("mass_reason")->str.find("has no density") != std::string::npos, "the omission names the part");
    check(!info.find("mass_g"), "a partial assembly does not invent a mass");
}

void testSketchFilletSolid() {
    ark::AgentSession session;
    accepted(session.call("doc_apply", parse(R"({"commands":[
      {"op":"sketch.set","name":"Plate","sketch":{"tolerance":0.01,"entities":[{"kind":"wire","name":"Outline","start":[0,0],"segments":[{"kind":"line","to":[10,0]},{"kind":"line","to":[10,10]},{"kind":"line","to":[0,10]},{"kind":"line","to":[0,0]}]}]}},
      {"op":"sketch.fillet","name":"Plate","entity":"Outline","corner":0,"radius":"2"},
      {"op":"sketch.fillet","name":"Plate","entity":"Outline","corner":1,"radius":"2"},
      {"op":"sketch.fillet","name":"Plate","entity":"Outline","corner":2,"radius":"2"},
      {"op":"sketch.fillet","name":"Plate","entity":"Outline","corner":3,"radius":"2"},
      {"op":"feature.add","type":"native_solid","name":"Plate","p":{"op":"extrude","sketch":"Plate","depth":"1"}}
    ]})")));
    const double area = 100.0 - 16.0 * (1.0 - 3.141592653589793 / 4.0);
    check(std::abs(volumeOf(session.document, 1) - area) < 0.2, "filleted plate volume matches the trimmed square");
    const auto& bounds = bodyData(session.document, 1)->find("report")->find("bounds_mm")->arr;
    check(bounds[3].num > 9.9 && bounds[3].num < 10.05 && bounds[4].num > 9.9 && bounds[4].num < 10.05, "fillets leave the square bounds");
}

int main() {
    try {
        testNames();
        testPatterns();
        testInterference();
        testCandidate();
        testExactBooleans();
        testSnapshotAndContract();
        testMergedFaces();
        testRoundTube();
        testRectangularTube();
        testShell();
        testClearanceAndMeasure();
        testMass();
        testSketchFilletSolid();
        testGuides();
        testNameEdits();
        testSnapshotCopy();
        testCurvesAndQueries();
        testDrivenSketch();
        std::cout << "Modeling workflow tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
