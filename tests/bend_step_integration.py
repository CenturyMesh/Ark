"""Optional Windows STEP worker integration, including pose invariance and failure."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

engine = str(Path(sys.argv[1]).resolve())
sample = Path(__file__).resolve().parents[1] / 'examples/laser-tube.ark'


def run(calls, environment=None):
    messages = [dict(jsonrpc='2.0', id=i, method='tools/call', params=dict(name=name, arguments=args))
                for i, (name,args) in enumerate(calls)]
    process = subprocess.run([engine], input='\n'.join(map(json.dumps,messages))+'\n',
                             text=True, encoding="utf-8", capture_output=True, check=True, timeout=40, env=environment)
    return [json.loads(line)['result'] for line in process.stdout.splitlines()]


with tempfile.TemporaryDirectory(prefix='ark-bend-step-') as folder:
    directory = Path(folder)
    path = directory / 'tubo reto com espaço.STEP'
    calls = [('doc_load',dict(path=str(sample))), ('doc_get',{}),
             ('notch_inspect',dict(id=1,progress=1)), ('export_step',dict(id=1,path=str(path))),
             ('doc_get',{}), ('doc_apply',dict(commands=[dict(op='feature.edit',id=1,notches=[])])),
             ('export_step',dict(id=1,path=str(directory/'zero-points.step')))]
    results = run(calls)
    assert len(results)==len(calls) and all(not r['isError'] for r in results), results
    assert results[1]['structuredContent']['document']==results[4]['structuredContent']['document']
    assert results[1]['structuredContent']['revision']==results[4]['structuredContent']['revision']
    assert len(results[2]['structuredContent']['segment_transforms_column_major'])==3
    assert results[3]['structuredContent']['export_pose']=='straight'
    assert results[3]['structuredContent']['path']==str(path)
    receipt = json.loads(path.with_suffix('.validation.json').read_text())
    assert receipt['brep_valid'] and receipt['solids']==1
    assert receipt['roundtrip_symmetric_difference_volume_mm3']<receipt['volume_tolerance_mm3']
    assert json.loads(path.with_suffix('.arkcut').read_text())['stock']['length_mm']==600
    blank = results[-1]['structuredContent']
    assert blank['expected_removed_volume_mm3']==0 and blank['export_pose']=='straight'
    environment = dict(os.environ, ARK_OCCT_PYTHON=str(directory/'missing-python.exe'))
    failed = directory/'must-not-exist.step'
    errors=run([('doc_load',dict(path=str(sample))),('export_step',dict(id=1,path=str(failed))),('doc_get',{})],environment)
    assert errors[1]['isError'] and not failed.exists()
    assert errors[2]['structuredContent']['revision']==1
print('STEP worker passed: folded inspection leaves model unchanged, straight export, Unicode path, zero bends, explicit missing-backend failure')
