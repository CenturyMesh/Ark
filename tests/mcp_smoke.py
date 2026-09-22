"""Real stdio protocol, replay, file IO and export tests; standard library only."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve())
sample = Path(__file__).resolve().parents[1] / 'examples' / 'harmonic-ring.ark'
with tempfile.TemporaryDirectory(prefix='ark-mcp-') as directory:
    work = Path(directory)
    messages = [
        {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'protocolVersion': '2025-11-25'}},
        {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
        {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'},
    ]
    def tool(name, args):
        messages.append({'jsonrpc': '2.0', 'id': len(messages), 'method': 'tools/call',
                         'params': {'name': name, 'arguments': args}})
    tool('doc_load', {'path': str(sample)})
    tool('mesh_inspect', {})
    tool('doc_apply', {'expected_revision': 0, 'commands': [{'op': 'param.set', 'name': 'waves', 'expr': '10'}]})
    tool('doc_apply', {'expected_revision': 1, 'commands': [{'op': 'param.set', 'name': 'waves', 'expr': '9'}]})
    tool('doc_save', {'path': str(work / 'roundtrip.ark')})
    tool('export_mesh', {'path': str(work / 'ring.stl'), 'format': 'stl'})
    tool('snapshot', {'path': str(work / 'ring.png')})
    tool('doc_load', {'path': str(work / 'roundtrip.ark')})
    tool('eval_expr', {'expr': 'waves + 2e-3'})
    payload = '\n'.join(json.dumps(m) for m in messages) + '\n{invalid}\n'
    process = subprocess.run([exe], input=payload, text=True, capture_output=True, timeout=20, check=True)
    replies = [json.loads(line) for line in process.stdout.splitlines()]
    assert len(replies) == len(messages), 'Notifications must not receive replies; invalid JSON must'
    assert replies[0]['result']['protocolVersion'] == '2025-11-25'
    assert len(replies[1]['result']['tools']) == 13
    calls = replies[2:-1]
    for index, reply in enumerate(calls):
        assert reply['result']['isError'] == (index == 2), reply
    assert calls[1]['result']['structuredContent']['triangles'] == 20480
    assert calls[2]['result']['structuredContent']['revision'] == 1
    assert abs(calls[-1]['result']['structuredContent']['value'] - 9.002) < 1e-12
    assert replies[-1]['error']['code'] == -32700
    stl = (work / 'ring.stl').read_bytes()
    triangles, = struct.unpack_from('<I', stl, 80)
    assert triangles == 20480 and len(stl) == 84 + triangles * 50
    assert (work / 'ring.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
    assert json.loads((work / 'roundtrip.ark').read_text())['format'] == 'ark'
print('MCP stdio: discovery, revision conflict, batch, persistence, STL and PNG passed')
