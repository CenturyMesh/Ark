#include "native_process.hpp"
#include "ark/json.hpp"
#include "ark/version.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <unordered_set>

using arkjson::Value;
using native_test::check;
namespace fs = std::filesystem;

static Value parse(const std::string& text) {
    Value value;
    check(arkjson::parse(text, value), "Invalid JSON in test or MCP reply: " + text.substr(0, 200));
    return value;
}
static const Value& field(const Value& value, const char* name) {
    const Value* found = value.find(name);
    check(found != nullptr, std::string("Missing field ") + name);
    return *found;
}
static Value request(int id, const std::string& method, Value params) {
    Value item = Value::makeObj();
    item.set("jsonrpc", Value::makeStr("2.0"));
    item.set("id", Value::makeNum(id));
    item.set("method", Value::makeStr(method));
    item.set("params", std::move(params));
    return item;
}
static Value call(int id, const std::string& name, Value args) {
    Value params = Value::makeObj();
    params.set("name", Value::makeStr(name));
    params.set("arguments", std::move(args));
    return request(id, "tools/call", std::move(params));
}
static Value argumentsPath(const fs::path& path) {
    Value args = Value::makeObj();
    args.set("path", Value::makeStr(native_test::pathUtf8(path)));
    return args;
}
static uint32_t stlCount(const std::string& bytes) {
    check(bytes.size() >= 84, "STL header too short");
    const auto* b = reinterpret_cast<const unsigned char*>(bytes.data() + 80);
    return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) |
           (uint32_t(b[3]) << 24);
}

int main(int argc, char** argv) {
    try {
        check(argc == 3, "Usage: ark-mcp-stdio-tests <mcp-exe> <repo-root>");
        const fs::path mcp = fs::absolute(argv[1]);
        const fs::path root = fs::absolute(argv[2]);
        native_test::TempDirectory temp("ark-mcp-native-");
        const fs::path work = temp.path();
        const fs::path sample = root / "examples/harmonic-ring.ark";
        check(fs::is_regular_file(sample), "Missing harmonic-ring sample");

        std::vector<Value> messages;
        messages.push_back(request(1, "initialize", parse(R"({"protocolVersion":"2025-11-25"})")));
        Value initialized = Value::makeObj();
        initialized.set("jsonrpc", Value::makeStr("2.0"));
        initialized.set("method", Value::makeStr("notifications/initialized"));
        messages.push_back(std::move(initialized));
        messages.push_back(request(2, "tools/list", Value::makeObj()));
        int id = 3;
        messages.push_back(call(id++, "doc_load", argumentsPath(sample)));
        messages.push_back(call(id++, "mesh_inspect", Value::makeObj()));
        messages.push_back(call(id++, "doc_apply", parse(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"waves","expr":"10"}]})")));
        messages.push_back(call(id++, "doc_apply", parse(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"waves","expr":"9"}]})")));
        messages.push_back(call(id++, "doc_save", argumentsPath(work / "roundtrip.ark")));
        Value meshArgs = argumentsPath(work / "ring.stl");
        meshArgs.set("format", Value::makeStr("stl"));
        messages.push_back(call(id++, "export_mesh", std::move(meshArgs)));
        messages.push_back(call(id++, "snapshot", argumentsPath(work / "ring.png")));
        messages.push_back(call(id++, "doc_load", argumentsPath(work / "roundtrip.ark")));
        messages.push_back(call(id++, "eval_expr", parse(R"({"expr":"waves + 2e-3"})")));
        std::string input;
        for (const auto& message : messages) input += message.dump() + "\n";
        input += "{invalid}\n";

        const std::string output = native_test::run(mcp, {}, root, input, std::chrono::seconds(20));
        std::vector<Value> replies;
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) if (!line.empty()) replies.push_back(parse(line));
        check(replies.size() == messages.size(), "Notifications must have no reply; malformed JSON must have one");
        check(field(field(replies[0], "result"), "protocolVersion").asStr() == "2025-11-25", "Protocol mismatch");
        const Value& identity = field(field(replies[0], "result"), "serverInfo");
        check(field(identity, "version").asStr() == ARK_APP_VERSION, "MCP runtime version differs from release metadata");
        check(field(identity, "iteration_code").asStr() == ARK_ITERATION_CODE, "MCP iteration code differs from release metadata");

        const Value& tools = field(field(replies[1], "result"), "tools");
        check(tools.t == Value::Arr, "tools/list did not return an array");
        std::unordered_set<std::string> names;
        for (const Value& entry : tools.arr) names.insert(field(entry, "name").asStr());
        for (const char* name : {"agent_onboard", "doc_load", "doc_save", "doc_apply",
                                 "mesh_inspect", "export_mesh", "snapshot", "eval_expr",
                                 "doc_new", "sketch_inspect"})
            check(names.count(name) == 1, std::string("Missing tool ") + name);

        for (size_t i = 2; i + 1 < replies.size(); ++i) {
            const Value& result = field(replies[i], "result");
            check(field(result, "isError").t == Value::Bool, "Missing isError boolean");
            check(field(result, "isError").b == (i == 4), "Unexpected MCP tool error at reply " + std::to_string(i));
        }
        const Value& report = field(field(replies[3], "result"), "structuredContent");
        check(field(report, "triangles").asNum() == 20480, "Harmonic ring triangle count changed");
        const Value& conflict = field(field(replies[4], "result"), "structuredContent");
        check(field(conflict, "revision").asNum() == 1, "Revision conflict did not report current revision");
        const Value& changed = field(field(replies[5], "result"), "structuredContent");
        check(field(changed, "revision").asNum() == 2, "Successful batch revision mismatch");
        const Value& value = field(field(replies[10], "result"), "structuredContent");
        check(std::abs(field(value, "value").asNum() - 9.002) < 1e-12, "Expression result mismatch");
        check(field(field(replies.back(), "error"), "code").asNum() == -32700, "Invalid JSON was not rejected");

        const std::string stl = native_test::readFile(work / "ring.stl");
        check(stlCount(stl) == 20480 && stl.size() == 84 + size_t(20480) * 50,
              "Binary STL size or triangle count mismatch");
        const std::string png = native_test::readFile(work / "ring.png");
        check(png.size() >= 8 && std::memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8) == 0,
              "Snapshot is not a PNG");
        const std::string saved = native_test::readFile(work / "roundtrip.ark");
        check(saved.size() >= 16 && std::memcmp(saved.data(), "SQLite format 3\0", 16) == 0, "Saved document is a binary container");
        std::cout << "MCP stdio: discovery, revision conflict, batch, persistence, STL and PNG passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "MCP stdio test failed: " << error.what() << '\n';
        return 1;
    }
}
