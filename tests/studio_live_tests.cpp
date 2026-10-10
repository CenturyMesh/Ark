#include "native_process.hpp"
#include "ark/json.hpp"

#include <cmath>
#include <iostream>

using arkjson::Value;
using native_test::check;
namespace fs = std::filesystem;

static Value parse(const std::string& text) {
    Value value;
    check(arkjson::parse(text, value), "Invalid JSON: " + text.substr(0, 200));
    return value;
}
static const Value& field(const Value& value, const char* name) {
    const Value* found = value.find(name);
    check(found != nullptr, std::string("Missing field ") + name);
    return *found;
}
static Value rpcCall(const std::string& name, Value args) {
    Value params = Value::makeObj();
    params.set("name", Value::makeStr(name));
    params.set("arguments", std::move(args));
    Value request = Value::makeObj();
    request.set("jsonrpc", Value::makeStr("2.0"));
    request.set("id", Value::makeNum(1));
    request.set("method", Value::makeStr("tools/call"));
    request.set("params", std::move(params));
    return request;
}
static Value pathArgs(const fs::path& path) {
    Value args = Value::makeObj();
    args.set("path", Value::makeStr(native_test::pathUtf8(path)));
    return args;
}
static Value tool(const fs::path& mcp, const fs::path& root,
                  const std::string& endpoint, const std::string& name, Value args) {
    const std::string reply = native_test::run(mcp, {"--connect", endpoint}, root,
                                               rpcCall(name, std::move(args)).dump() + "\n",
                                               std::chrono::seconds(8));
    const Value envelope = parse(reply);
    check(!envelope.find("error"), name + " returned JSON-RPC error: " + reply);
    const Value& result = field(envelope, "result");
    check(field(result, "isError").t == Value::Bool && !field(result, "isError").b,
          name + " failed: " + reply);
    return field(result, "structuredContent");
}
static double holeX(const Value& inspect) {
    const Value& entities = field(inspect, "entities");
    check(entities.t == Value::Arr && entities.arr.size() >= 2, "Missing opening entities");
    const Value& coordinates = field(entities.arr[1], "resolved_dof");
    check(coordinates.t == Value::Arr && !coordinates.arr.empty(), "Missing resolved hole coordinates");
    return coordinates.arr[0].asNum();
}

int main(int argc, char** argv) {
    try {
        check(argc == 4, "Usage: ark-studio-live-tests <studio-exe> <mcp-exe> <repo-root>");
        const fs::path studioExe = fs::absolute(argv[1]);
        const fs::path mcpExe = fs::absolute(argv[2]);
        const fs::path root = fs::absolute(argv[3]);
        native_test::TempDirectory temp("ark-live-studio-native-");
        const fs::path endpointFile = temp.path() / "endpoint.txt";
        const fs::path settingsFile = temp.path() / "settings.json";
        const fs::path sample = root / "examples/freeform-plate.ark";
        check(fs::is_regular_file(sample), "Missing freeform plate sample");
        native_test::Process studio(studioExe,
            {"--load", native_test::pathUtf8(sample),
             "--live-endpoint-file", native_test::pathUtf8(endpointFile),
             "--settings-file", native_test::pathUtf8(settingsFile),
             "--exit-after-ms", "10000", "--size", "1180", "840"}, root);

        const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        std::string endpoint;
        while (std::chrono::steady_clock::now() < readyDeadline) {
            check(studio.running(), "Studio exited before its live endpoint was ready: " + studio.error());
            if (fs::is_regular_file(endpointFile)) {
                endpoint = native_test::readFile(endpointFile);
                const auto first = endpoint.find_first_not_of(" \r\n\t");
                if (first != std::string::npos) {
                    const auto last = endpoint.find_last_not_of(" \r\n\t");
                    endpoint = endpoint.substr(first, last - first + 1);
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        check(!endpoint.empty(), "Studio live endpoint was not ready in 8 seconds");

        const Value onboard = tool(mcpExe, root, endpoint, "agent_onboard", parse(R"({"topic":"sketch"})"));
        const Value& before = field(onboard, "document");
        check(field(before, "feature_count").asNum() == 2, "Sample feature count mismatch");
        Value change = parse(R"({"commands":[{"op":"param.set","name":"width","expr":"144"}]})");
        change.set("expected_revision", field(before, "revision"));
        const Value preview = tool(mcpExe, root, endpoint, "doc_preview", std::move(change));
        check(field(preview,"valid").b&&field(preview,"candidate_cached").b,"Live preview did not cache a candidate");
        const Value unchanged=tool(mcpExe,root,endpoint,"doc_info",Value::makeObj());
        check(field(unchanged,"revision").asNum()==field(before,"revision").asNum(),"Live preview changed the document");
        const Value changed = tool(mcpExe, root, endpoint, "doc_apply", field(preview,"apply"));
        check(field(changed,"applied_prepared").b&&!field(changed,"geometry_recomputed").b,"Live commit rebuilt candidate geometry");
        check(field(changed, "revision").asNum() == field(before, "revision").asNum() + 1,
              "Width edit did not increment revision");
        const Value holes = tool(mcpExe, root, endpoint, "sketch_inspect", parse(R"({"name":"openings"})"));
        check(std::abs(holeX(holes) - 58) < 1e-12, "Parametric hole did not move to x=58");
        const Value report = tool(mcpExe, root, endpoint, "mesh_inspect", Value::makeObj());
        check(field(report, "closed_oriented_edges").b && field(report, "boundary_edges").asNum() == 0,
              "Edited plate mesh is not closed");

        const fs::path savedPath = temp.path() / "from-live-agent.ark";
        tool(mcpExe, root, endpoint, "doc_save", pathArgs(savedPath));
        const Value saved = parse(native_test::readFile(savedPath));
        check(field(saved, "version").asNum() >= 5, "Saved document version regressed");
        const Value& params = field(saved, "params");
        bool widthSaved = false;
        check(params.t == Value::Arr, "Saved parameters missing");
        for (const Value& param : params.arr)
            if (field(param, "name").asStr() == "width")
                widthSaved = field(param, "expr").asStr() == "144";
        check(widthSaved, "Edited width was not persisted");

        tool(mcpExe, root, endpoint, "doc_undo", Value::makeObj());
        const Value restoredHoles = tool(mcpExe, root, endpoint, "sketch_inspect", parse(R"({"name":"openings"})"));
        check(std::abs(holeX(restoredHoles) - 46) < 1e-12, "Undo did not restore x=46");
        const Value current = tool(mcpExe, root, endpoint, "doc_info", Value::makeObj());
        Value newArgs = Value::makeObj();
        newArgs.set("expected_revision", field(current, "revision"));
        const Value empty = tool(mcpExe, root, endpoint, "doc_new", std::move(newArgs));
        check(field(empty, "feature_count").asNum() == 0 &&
              field(empty, "revision").asNum() > field(current, "revision").asNum(),
              "New document did not reset feature history or advance revision");
        const Value restored = tool(mcpExe, root, endpoint, "doc_undo", Value::makeObj());
        check(field(restored, "feature_count").asNum() == 2, "Undo New did not restore plate");

        check(studio.wait(std::chrono::seconds(12)) == 0, "Studio did not exit cleanly: " + studio.error());
        const std::string after = native_test::run(mcpExe, {"--connect", endpoint}, root,
                                                    rpcCall("doc_info", Value::makeObj()).dump() + "\n",
                                                    std::chrono::seconds(8));
        check(parse(after).find("error") != nullptr,
              "Disconnected Studio became an unrelated standalone document");
        std::cout << "Live Studio + MCP: prepared edit, holes, mesh, save, undo, New, disconnect passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Live Studio test failed: " << error.what() << '\n';
        return 1;
    }
}
