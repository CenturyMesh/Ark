"""Opt-in live public KiCad STEP -> native library -> insertion -> reopen proof.

No provider account, browser session, credentials or user design is used. Stops
at the first failure and writes an honest bounded receipt. Run against a freshly
built native ark-mcp with the sibling OCCT worker. Not a default offline test.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import subprocess
import tempfile
import threading
import time
from urllib.parse import parse_qsl, urlsplit


def verify_graph_privacy(value):
    """Reject authentication fields/URLs; approval-at-retrieval is historical."""
    forbidden = {"authorization", "password", "passwd", "api_key", "apikey", "api-key", "token", "access_token", "id_token", "client_secret", "cookie", "cookies", "headers", "credentials", "signature", "secret", "session", "sessionid", "jwt", "sig", "x-amz-credential", "x-amz-signature", "x-goog-credential", "x-goog-signature"}
    if isinstance(value, dict):
        for key, item in value.items():
            if key.casefold() in forbidden:
                raise AssertionError("Authentication field leaked into public component graph")
            if key == "credentials_sent" and item is not False:
                raise AssertionError("Graph unexpectedly reports authenticated network access")
            verify_graph_privacy(item)
    elif isinstance(value, list):
        for item in value:
            verify_graph_privacy(item)
    elif isinstance(value, str):
        if value.casefold().startswith("bearer "):
            raise AssertionError("Bearer credential leaked into public component graph")
        if value.startswith("https://"):
            parsed = urlsplit(value)
            if parsed.username is not None or parsed.password is not None or any(key.casefold() in forbidden for key, _ in parse_qsl(parsed.query)):
                raise AssertionError("Credential-bearing URL leaked into public component graph")


def preserve_previous_receipt(path):
    if not path.exists():
        return None
    previous = path.read_bytes()
    digest = hashlib.sha256(previous).hexdigest()
    backup = path.with_name(path.stem + ".previous-" + digest[:16] + path.suffix)
    try:
        with backup.open("xb") as file:
            file.write(previous)
    except FileExistsError:
        if hashlib.sha256(backup.read_bytes()).hexdigest() != digest:
            raise RuntimeError("Previous receipt archive conflicts with the original receipt")
    try:
        original = json.loads(previous)
        summary = {"status": original.get("status"), "attempts": original.get("attempts", [])}
    except (ValueError, UnicodeDecodeError):
        summary = {"status": "unreadable_prior_receipt"}
    return {"file": backup.name, "sha256": digest, **summary}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mcp", required=True, type=Path)
    parser.add_argument("--receipt", required=True, type=Path)
    args = parser.parse_args()
    receipt = {"status": "running", "scope": "live public KiCad repository/native import, not supplier or manufacture qualification", "attempts": []}
    previous = preserve_previous_receipt(args.receipt)
    if previous is not None:
        receipt["previous_receipt"] = previous
    began = time.monotonic()
    process = None
    workspace = None
    stage = "start_mcp"
    try:
        workspace = tempfile.TemporaryDirectory(prefix="ark-public-component-035b-")
        directory = workspace.name
        process = subprocess.Popen([str(args.mcp.resolve())], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")
        replies = queue.Queue()

        def read_stdout():
            for line in process.stdout:
                replies.put(line)
            replies.put(None)

        threading.Thread(target=read_stdout, daemon=True).start()
        # Keep diagnostics from filling the pipe, without copying geometry,
        # local paths or subprocess payloads into the public receipt.
        def discard_stderr():
            for _ in process.stderr:
                pass

        threading.Thread(target=discard_stderr, daemon=True).start()
        serial = 0

        def call(name, arguments):
            nonlocal serial, stage
            stage = name
            serial += 1
            request = {"jsonrpc": "2.0", "id": serial, "method": "tools/call", "params": {"name": name, "arguments": arguments}}
            process.stdin.write(json.dumps(request, separators=(",", ":")) + "\n")
            process.stdin.flush()
            remaining = 290 - (time.monotonic() - began)
            if remaining <= 0:
                raise RuntimeError("Live smoke process reached its 290 second watchdog")
            line = replies.get(timeout=remaining)
            if line is None:
                raise RuntimeError("MCP ended without a reply")
            response = json.loads(line)
            if response.get("id") != serial or "error" in response:
                raise RuntimeError("MCP protocol/request failure")
            result = response["result"]
            if result.get("isError"):
                # Tool errors are bounded; the harness submits public URLs
                # and synthetic names only, never secrets or user documents.
                raise RuntimeError(str(result.get("structuredContent", {}).get("error", "Tool rejected request"))[:1500])
            receipt["attempts"].append({"tool": name, "status": "passed"})
            return result["structuredContent"]

        policy = call("library_vendors", {"operation": "list", "root": directory})
        receipt["policy_sha256"] = policy["policy_sha256"]
        found = call("library_source_search", {"root": directory, "source": "kicad", "path": "Connector_PinHeader_2.54mm.3dshapes", "query": "1x02"})
        item = next((item for item in found["items"] if item["kind"] == "step"), None)
        if item is None:
            raise RuntimeError("KiCad page contained no matching STEP candidate; no filename or URL invented")
        receipt["observed_catalog_item"] = item
        record = call("library_fetch", {"root": directory, "vendor": "kicad", "url": item["download_url"], "all_bodies": True, "metadata": {"name": "KiCad community reference " + item["name"], "category": "electrical.reference", "source_url": item["source_url"], "license": item["license"], "provenance": "Public KiCad community library model; no manufacturer or sourcing qualification"}})
        receipt["download_evidence"] = record["download_evidence"]
        assert record["download_source_verified"] and not record["sourcing_qualified"] and not record["manufacturing_qualified"]
        component = call("library_get", {"root": directory, "id": record["id"]})
        assert component["metadata"]["download_evidence"]["sha256"] == component["source_sha256"]
        inserted = call("library_insert", {"root": directory, "id": record["id"], "expected_revision": 0})
        native = call("native_inspect", {"id": inserted["feature_id"], "detail": "properties"})
        receipt["component_id"] = record["id"]
        receipt["native_report"] = {key: native.get("report", {})[key] for key in ("brep_valid", "solids", "faces", "edges", "volume_mm3", "area_mm2", "source_selection_verified") if key in native.get("report", {})}
        drawing = str(Path(directory) / "public-reference.ark")
        call("doc_save", {"path": drawing})
        call("doc_load", {"path": drawing})
        reopened = call("native_inspect", {"id": inserted["feature_id"], "detail": "properties"})
        assert reopened.get("report", {}).get("brep_valid")
        receipt["portable_reopen_verified"] = True
        feature_key = "feature:" + str(int(inserted["feature_id"]))
        graph = call("context_graph", {"keys": [feature_key], "depth": 2, "limit": 64})
        verify_graph_privacy(graph)
        nodes = {node["key"]: node for node in graph["nodes"]}
        edges = graph["edges"]
        artifact_key = "step_artifact:" + record["download_evidence"]["sha256"]
        assert not graph["manufacturing_qualified"], "Navigation graph cannot qualify manufacturing"
        assert not graph["partial"], "Synthetic single-component graph should fit the requested 64-node scope"
        assert artifact_key in nodes and nodes[artifact_key]["kind"] == "step_artifact", "Actual downloaded STEP hash is missing from reopened graph"
        assert any(edge["from"] == feature_key and edge["relation"] == "retains_step_artifact" and edge["to"] == artifact_key for edge in edges), "Feature does not retain its source STEP artifact in graph"
        artifact = nodes[artifact_key]["facts"]["binding"]
        assert artifact["sha256"] == component["source_sha256"] and artifact["byte_size"] == record["download_evidence"]["byte_size"] and artifact["http_status"] == 200, "Graph source custody differs from actual immutable download"
        assert artifact["network_policy_at_retrieval"]["vendor"] == "kicad", "Historical download vendor was not retained"
        sources = [node for node in nodes.values() if node["kind"] == "source_link"]
        assert any(node["facts"]["url"] == item["source_url"] for node in sources), "Observed catalog source is absent from graph"
        assert any(node["facts"]["url"] == record["download_evidence"]["final_url"] for node in sources), "Actual downloaded source is absent from graph"
        vendor_key = "vendor:" + urlsplit(item["source_url"]).hostname
        assert vendor_key in nodes and nodes[vendor_key]["kind"] == "vendor", "Observed source-host vendor node is absent"
        assert any(edge["relation"] == "hosted_by" and edge["to"] == vendor_key for edge in edges), "Source URL host relationship is absent"
        receipt["graph_provenance"] = {"verified": True, "privacy_verified": True, "feature_key": feature_key, "step_artifact_key": artifact_key, "vendor_key": vendor_key, "manufacturing_qualified": graph["manufacturing_qualified"], "graph": graph}
        receipt["status"] = "passed"
    except Exception as error:
        receipt["status"] = "blocked" if stage in ("library_source_search", "library_fetch") else "failed"
        receipt["attempts"].append({"tool": stage, "status": receipt["status"], "error": str(error)[:1500]})
    finally:
        if process is not None:
            if os.name == "nt" and process.poll() is None:
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True, check=False)
            elif process.poll() is None:
                process.kill()
            process.wait(timeout=10)
        if workspace is not None:
            workspace.cleanup()
        receipt["elapsed_seconds"] = round(time.monotonic() - began, 3)
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(json.dumps(receipt, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(json.dumps({"status": receipt["status"], "stage": stage, "receipt": str(args.receipt)}, ensure_ascii=False))
    return 0 if receipt["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
