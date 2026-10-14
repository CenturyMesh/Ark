#!/usr/bin/env bash
set -euo pipefail

# Run against an extracted runtime in a clean Linux environment without an SDK.
package=$(cd -- "${1:?Usage: linux_package_smoke.sh <extracted-package-directory>}" && pwd)
work=$(mktemp -d /tmp/ark-linux-package-XXXXXXXX)
trap 'rm -rf -- "$work"' EXIT
cd -- "$work"
unset ARK_OCCT_WORKER OpenCASCADE_DIR LD_LIBRARY_PATH CASROOT CSF_XSTEPDefaults CSF_XSMessage
cat > requests.jsonl <<EOF
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25"}}
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"kernel_info","arguments":{}}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"doc_apply","arguments":{"expected_revision":0,"commands":[{"op":"feature.add","type":"native_solid","name":"Package smoke","native":{"op":"box"},"p":{"width":10,"depth":20,"height":30}}]}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"native_inspect","arguments":{"id":1}}}
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"export_step","arguments":{"id":1,"path":"$work/smoke.step"}}}
{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"doc_save","arguments":{"path":"$work/smoke.ark"}}}
{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"doc_load","arguments":{"path":"$work/smoke.ark"}}}
EOF
timeout 90 "$package/bin/ark-mcp" < requests.jsonl > replies.jsonl
[[ $(wc -l < replies.jsonl) -eq 7 ]] || { cat replies.jsonl; echo 'Missing MCP replies.' >&2; exit 1; }
if grep -qE '"isError":true|"error":\{' replies.jsonl; then cat replies.jsonl; echo 'MCP operation rejected.' >&2; exit 1; fi
runtime_version=$(sed -nE 's/.*"version":"([0-9]+\.[0-9]+\.[0-9]+)".*/\1/p' "$package/app-version.json")
iteration_code=$(sed -nE 's/.*"iteration_code":"([0-9]{3}[A-G])".*/\1/p' "$package/app-version.json")
[[ -n $runtime_version && -n $iteration_code ]]
head -1 replies.jsonl | grep -qF "\"version\":\"$runtime_version\""
head -1 replies.jsonl | grep -qF "\"iteration_code\":\"$iteration_code\""
grep -q '"kernel":"OCCT"' replies.jsonl
grep -q '"version":"8.0.1"' replies.jsonl
grep -q '"volume_mm3":6000' replies.jsonl
grep -q '"step_sha256"' replies.jsonl
[[ -s smoke.step && -s smoke.ark ]]
grep -q 'ISO-10303-21' smoke.step
grep -q '"platform":"linux-x64-headless"' "$package/app-version.json"
[[ -s $package/licenses/LICENSE_LGPL_21.txt && -s $package/licenses/OCCT_LGPL_EXCEPTION.txt ]]
[[ -s $package/licenses/stb-image-LICENSE.txt && -s $package/licenses/stb-image-provenance.md ]]
[[ -s $package/LICENSE && -s $package/NOTICE ]]
if find "$package" -type f \( -name '*.ark' -o -name '.ark-local.json' \) | grep -q .; then echo 'Package contains local document/settings content.' >&2; exit 1; fi
printf 'Linux relocated package: MCP identity, OCCT 8.0.1, exact box volume, STEP readback, document save/load and notices passed.\n'
