#include "ark/agent.hpp"
#include "ark/io.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/serialize.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using arkjson::Value;

Value parse(const char* text) {
    Value value;
    if (!arkjson::parse(text, value)) throw std::runtime_error("invalid mesh test JSON");
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

std::string failure(ark::AgentSession& session, const char* tool, const Value& args) {
    const Value response = session.call(tool, args);
    const Value* error = response.find("isError");
    if (!error || !error->b) throw std::runtime_error(std::string(tool) + " should have failed");
    return response.find("structuredContent")->find("error")->str;
}

Value grid(int cells, int bumpX, int bumpY, double bumpZ) {
    const int n = cells + 1;
    Value mesh = Value::makeObj(), positions = Value::makeArr(), indices = Value::makeArr();
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            positions.arr.push_back(Value::makeNum(x));
            positions.arr.push_back(Value::makeNum(y));
            positions.arr.push_back(Value::makeNum(x == bumpX && y == bumpY ? bumpZ : 0));
        }
    }
    for (int y = 0; y < cells; ++y) {
        for (int x = 0; x < cells; ++x) {
            const int i00 = y * n + x, i10 = i00 + 1, i01 = i00 + n, i11 = i01 + 1;
            for (int index : {i00, i10, i01, i10, i11, i01}) indices.arr.push_back(Value::makeNum(index));
        }
    }
    mesh.set("positions", std::move(positions));
    mesh.set("indices", std::move(indices));
    return mesh;
}

Value applyMesh(ark::AgentSession& session, const char* type, Value mesh, const Value& extra = Value::makeObj(), const std::vector<double>& inputs = {}) {
    Value command = Value::makeObj(), args = Value::makeObj(), commands = Value::makeArr();
    command.set("op", Value::makeStr("feature.add"));
    command.set("type", Value::makeStr(type));
    if (mesh.t == Value::Obj) command.set("mesh", std::move(mesh));
    if (extra.t == Value::Obj && !extra.obj.empty()) command.set("p", extra);
    if (!inputs.empty()) {
        Value parents = Value::makeArr();
        for (double id : inputs) parents.arr.push_back(Value::makeNum(id));
        command.set("inputs", std::move(parents));
    }
    commands.arr.push_back(std::move(command));
    args.set("commands", std::move(commands));
    return accepted(session.call("doc_apply", args));
}

Value params(std::initializer_list<std::pair<const char*, const char*>> values) {
    Value p = Value::makeObj();
    for (const auto& item : values) p.set(item.first, Value::makeStr(item.second));
    return p;
}

Value modelOf(const Value& inspection) {
    const Value* model = inspection.find("mesh_model");
    if (!model) throw std::runtime_error("mesh_inspect did not attach mesh_model");
    return *model;
}

Value inspect(ark::AgentSession& session, double id) {
    Value args = Value::makeObj();
    args.set("id", Value::makeNum(id));
    return accepted(session.call("mesh_inspect", args));
}

void testWeld() {
    ark::AgentSession session;
    Value mesh = Value::makeObj(), positions = Value::makeArr(), indices = Value::makeArr();
    for (double value : {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0}) positions.arr.push_back(Value::makeNum(value));
    for (double value : {0, 1, 2, 3, 4, 5}) indices.arr.push_back(Value::makeNum(value));
    mesh.set("positions", std::move(positions));
    mesh.set("indices", std::move(indices));
    applyMesh(session, "mesh_body", std::move(mesh));
    applyMesh(session, "mesh_weld", {}, params({{"epsilon", "0.0001"}}), {1});
    const Value& model = modelOf(inspect(session, 2));
    check(model.find("output_vertices")->num == 4, "welded quad vertex count");
    check(model.find("output_triangles")->num == 2, "welded quad triangle count");
    check(model.find("exact_geometry")->b == false && model.find("mesh_authoritative")->b == true, "weld stays mesh authority");
}

void testSmooth() {
    ark::AgentSession session;
    applyMesh(session, "mesh_body", grid(4, 2, 2, 4));
    applyMesh(session, "mesh_smooth", {}, params({{"iterations", "1"}, {"lambda", "0.5"}, {"pin_boundary", "1"}}), {1});
    Value args = Value::makeObj();
    args.set("id", Value::makeNum(2));
    const Value positions = *accepted(session.call("mesh_get", args)).find("positions");
    const double center = positions.arr[12 * 3 + 2].num;
    check(std::abs(center - 2.0) < 1.0e-4, "pinned Laplacian lowers the center");
    check(positions.arr[2].num == 0 && positions.arr[(24 * 3) + 2].num == 0, "boundary vertices stay put");
    check(modelOf(inspect(session, 2)).find("pin_boundary")->b, "smooth report pins the boundary");
}

void testDecimate() {
    ark::AgentSession session;
    applyMesh(session, "mesh_body", grid(4, -1, -1, 0));
    applyMesh(session, "mesh_decimate", {}, params({{"ratio", "0.5"}}), {1});
    const Value inspection = inspect(session, 2);
    const Value& model = modelOf(inspection);
    check(model.find("target_reached")->b, "planar decimation reached the ratio");
    check(model.find("output_triangles")->num <= 16, "decimated triangle count");
    check(model.find("boundary_preserved")->b, "decimation reports a preserved boundary");
    check(inspection.find("boundary_edges")->num == 16, "grid boundary edge count");
    check(std::abs(inspection.find("area_mm2")->num - 16) < 1.0e-3, "planar decimation keeps area");
}

void testOffsetAndFill() {
    ark::AgentSession session;
    applyMesh(session, "mesh_body", grid(1, -1, -1, 0));
    applyMesh(session, "mesh_offset", {}, params({{"distance", "2"}}), {1});
    Value args = Value::makeObj();
    args.set("id", Value::makeNum(2));
    const Value positions = *accepted(session.call("mesh_get", args)).find("positions");
    for (size_t i = 2; i < positions.arr.size(); i += 3)
        check(std::abs(positions.arr[i].num - 2) < 1.0e-4, "planar offset follows the face normal");
    const Value& offset = modelOf(inspect(session, 2));
    check(offset.find("offset_model")->str == "vertex_normal", "offset model name");
    check(offset.find("self_intersections_checked")->b == false, "offset does not claim intersection checks");

    Value tet = Value::makeObj(), tetPositions = Value::makeArr(), tetIndices = Value::makeArr();
    for (double value : {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1}) tetPositions.arr.push_back(Value::makeNum(value));
    for (double value : {0, 2, 1, 0, 1, 3, 0, 3, 2}) tetIndices.arr.push_back(Value::makeNum(value));
    tet.set("positions", std::move(tetPositions));
    tet.set("indices", std::move(tetIndices));
    ark::AgentSession filled;
    applyMesh(filled, "mesh_body", std::move(tet));
    applyMesh(filled, "mesh_fill", {}, {}, {1});
    const Value fill = inspect(filled, 2);
    const Value& fillModel = modelOf(fill);
    check(fill.find("closed_oriented_edges")->b, "filled tet is closed and oriented");
    check(std::abs(fill.find("signed_volume_mm3")->num - 1.0 / 6.0) < 1.0e-5, "filled tet volume");
    check(fill.find("signed_volume_mm3")->num > 0, "filled tet points outward");
    check(fillModel.find("residual_boundary_edges")->num == 0, "fill closes the boundary");
    check(fillModel.find("fill_model")->str == "planar_ear_clip", "fill model name");
    const std::string native = failure(filled, "native_inspect", args);
    check(native.find("mesh_inspect") != std::string::npos, "native inspection rejects a mesh");
}

void testPhotoGrid() {
    ark::AgentSession session;
    Value mesh = Value::makeObj(), positions = Value::makeArr(), grid = Value::makeObj();
    for (double value : {0, 0, 0, 10, 0, 0, 0, 4, 0, 10, 4, 1}) positions.arr.push_back(Value::makeNum(value));
    grid.set("nu", Value::makeNum(2));
    grid.set("nv", Value::makeNum(2));
    mesh.set("positions", std::move(positions));
    mesh.set("grid", std::move(grid));
    applyMesh(session, "mesh_body", mesh);
    const Value report = inspect(session, 1);
    check(report.find("triangles")->num == 2 && report.find("vertices")->num == 4, "a photo grid becomes two triangles");
    check(report.find("area_mm2")->num > 40.0 && report.find("area_mm2")->num < 41.0, "the grid covers the 10 by 4 mm patch");
    Value both = Value::makeObj(), again = Value::makeArr(), indices = Value::makeArr(), spec = Value::makeObj();
    for (double value : {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0}) again.arr.push_back(Value::makeNum(value));
    for (double value : {0, 1, 2}) indices.arr.push_back(Value::makeNum(value));
    spec.set("nu", Value::makeNum(2));
    spec.set("nv", Value::makeNum(2));
    both.set("positions", std::move(again));
    both.set("grid", std::move(spec));
    both.set("indices", std::move(indices));
    Value command = Value::makeObj(), args = Value::makeObj(), commands = Value::makeArr();
    command.set("op", Value::makeStr("feature.add"));
    command.set("type", Value::makeStr("mesh_body"));
    command.set("mesh", std::move(both));
    commands.arr.push_back(std::move(command));
    args.set("commands", std::move(commands));
    check(failure(session, "doc_apply", args).find("mutually exclusive") != std::string::npos, "grid and indices cannot be combined");
}

void testImportRoundtripAndVersion() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("ark-mesh-model-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(directory), "create mesh test directory");
    const auto stl = directory / "square.stl";
    const auto obj = directory / "square.obj";
    const auto saved = directory / "mesh.ark";
    {
        std::ofstream file(obj);
        file << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n";
    }
    ark::AgentSession imported;
    Value objArgs = Value::makeObj();
    objArgs.set("path", Value::makeStr(obj.string()));
    objArgs.set("name", Value::makeStr("Quad"));
    const Value objResult = accepted(imported.call("import_mesh", objArgs));
    check(objResult.find("triangles")->num == 2 && objResult.find("vertices")->num == 4, "OBJ quad fan");
    check(objResult.find("file_linked")->b == false && objResult.find("exact_geometry")->b == false, "import is an unlinked mesh snapshot");
    check(std::abs(inspect(imported, 1).find("area_mm2")->num - 1) < 1.0e-5, "imported quad area");

    ark::Mesh square = ark::importMeshPath(obj.string());
    check(ark::exportSTL(stl.string().c_str(), square), "write STL");
    ark::AgentSession stlSession;
    Value stlArgs = Value::makeObj();
    stlArgs.set("path", Value::makeStr(stl.string()));
    const Value stlResult = accepted(stlSession.call("import_mesh", stlArgs));
    check(stlResult.find("input_triangles")->num == 2 && stlResult.find("vertices")->num == 6, "binary STL stays unwelded");
    check(std::abs(inspect(stlSession, 1).find("area_mm2")->num - 1) < 1.0e-4, "STL area roundtrip");

    ark::AgentSession session;
    applyMesh(session, "mesh_body", grid(1, -1, -1, 0));
    applyMesh(session, "mesh_offset", {}, params({{"distance", "2"}}), {1});
    Value save = Value::makeObj();
    save.set("path", Value::makeStr(saved.string()));
    accepted(session.call("doc_save", save));
    Value stored;
    check(arkjson::parse(ark::documentToJson(session.document), stored), "saved mesh document parses");
    check(stored.find("version")->num == 8, "mesh document is version 8");
    ark::AgentSession loaded;
    accepted(loaded.call("doc_load", save));
    check(loaded.document.features[0].type == ark::FeatureType::MeshBody, "mesh body persisted");
    check(loaded.document.features[1].type == ark::FeatureType::MeshOffset, "mesh offset persisted");
    Value loadedArgs = Value::makeObj();
    loadedArgs.set("id", Value::makeNum(2));
    const Value loadedPositions = *accepted(loaded.call("mesh_get", loadedArgs)).find("positions");
    check(std::abs(loadedPositions.arr[2].num - 2) < 1.0e-4, "offset survives save and load");

    ark::Document rejected;
    std::string error;
    check(!ark::documentFromJson(R"({"version":7,"features":[{"id":1,"type":"mesh_body","name":"m","op":0,"inputs":[],"p":{},"mesh":{"positions":[0,0,0,1,0,0,0,1,0],"indices":[0,1,2]}}]})", rejected, &error),
          "version 7 cannot carry a mesh feature");
    check(error.find("version 8") != std::string::npos, "version 7 mesh rejection names version 8");
    std::filesystem::remove(stl);
    std::filesystem::remove(obj);
    std::filesystem::remove(saved);
    std::filesystem::remove(directory);
}
} // namespace

int main() {
    try {
        testWeld();
        testSmooth();
        testDecimate();
        testOffsetAndFill();
        testPhotoGrid();
        testImportRoundtripAndVersion();
        std::cout << "Mesh model tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Mesh model tests failed: " << error.what() << '\n';
        return 1;
    }
}
