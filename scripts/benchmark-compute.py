"""Transfer-inclusive warm CPU/CUDA benchmark through the actual MCP process."""
import json
from pathlib import Path
import subprocess
import sys

exe = Path(sys.argv[1] if len(sys.argv) > 1 else 'out/next/ark-mcp.exe').resolve()
messages = [dict(jsonrpc='2.0', id=i, method='tools/call',
                 params=dict(name='compute_benchmark', arguments=dict(vertices=count)))
            for i, count in enumerate((262144, 1048576, 4000000), 1)]
process = subprocess.run([str(exe)], input='\n'.join(map(json.dumps, messages)) + '\n',
                         capture_output=True, text=True, check=True, timeout=30)
results = [json.loads(line)['result']['structuredContent'] for line in process.stdout.splitlines()]
print(json.dumps({'results': results, 'warmup_excluded': True,
                  'cold_runtime_initialization_reported_separately': True}, indent=2))
