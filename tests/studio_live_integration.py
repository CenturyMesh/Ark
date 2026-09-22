"""Opt-in real Windows Studio + MCP proxy test. Owns only its temporary process/files."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

studio, mcp = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="ark-live-studio-") as directory:
    work = Path(directory)
    endpoint_file = work / "endpoint.txt"
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process = subprocess.Popen(
        [studio, "--load", str(root / "examples/freeform-plate.ark"),
         "--live-endpoint-file", str(endpoint_file), "--settings-file", str(work / "settings.json"),
         "--exit-after-ms", "10000", "--size", "1180", "840"],
        cwd=root, startupinfo=startup, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    try:
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError("Studio exited before its live endpoint was ready", process.returncode)
            if endpoint_file.exists() and endpoint_file.read_text().strip():
                break
            time.sleep(.05)
        endpoint = endpoint_file.read_text().strip()

        def tool(name, arguments):
            request = {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                       "params": {"name": name, "arguments": arguments}}
            reply = subprocess.run([mcp, "--connect", endpoint],
                                   input=json.dumps(request) + "\n", text=True,
                                   capture_output=True, timeout=8, check=True)
            envelope = json.loads(reply.stdout)
            assert "error" not in envelope, envelope
            result = envelope["result"]
            assert not result["isError"], result
            return result["structuredContent"]

        onboard = tool("agent_onboard", {"topic": "sketch"})
        before = onboard["document"]
        assert before["feature_count"] == 2
        changed = tool("doc_apply", {"expected_revision": before["revision"], "commands": [
            {"op": "param.set", "name": "width", "expr": "144"}]})
        assert changed["revision"] == before["revision"] + 1
        holes = tool("sketch_inspect", {"name": "openings"})
        assert abs(holes["entities"][1]["resolved_dof"][0] - 58) < 1e-12
        report = tool("mesh_inspect", {})
        assert report["closed_oriented_edges"] and report["boundary_edges"] == 0
        saved = work / "from-live-agent.ark"
        tool("doc_save", {"path": str(saved)})
        document = json.loads(saved.read_text())
        assert document["version"] == 5
        assert next(p for p in document["params"] if p["name"] == "width")["expr"] == "144"
        tool("doc_undo", {})
        restored = tool("sketch_inspect", {"name": "openings"})
        assert abs(restored["entities"][1]["resolved_dof"][0] - 46) < 1e-12
        current = tool("doc_info", {})
        empty = tool("doc_new", {"expected_revision": current["revision"]})
        assert empty["feature_count"] == 0 and empty["revision"] > current["revision"]
        restored = tool("doc_undo", {})
        assert restored["feature_count"] == 2
        process.wait(timeout=12)
        assert process.returncode == 0, process.communicate()
        request = {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                   "params": {"name": "doc_info", "arguments": {}}}
        disconnected = subprocess.run([mcp, "--connect", endpoint], input=json.dumps(request)+"\n",
                                      text=True, capture_output=True, timeout=8, check=True)
        assert "error" in json.loads(disconnected.stdout), "Disconnected Studio must never become an empty standalone document"
        print("Live Studio + independent MCP processes: onboarding, parameter change, actual holes, closed mesh, save, undo, New, disconnect passed")
    finally:
        if process.poll() is None:
            process.terminate()  # only this test-owned process, never the operator's window
            process.wait(timeout=5)
