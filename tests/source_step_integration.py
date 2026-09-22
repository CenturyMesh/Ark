"""Optional real-source MCP lifecycle; source fixture stays outside the repository.

Usage: python tests/source_step_integration.py path/to/ark-mcp.exe source.step
The input is copied into an owned temporary directory before tamper checks.
"""
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

engine=str(Path(sys.argv[1]).resolve());fixture=Path(sys.argv[2]).resolve()


def calls(messages):
    data='\n'.join(json.dumps(dict(jsonrpc='2.0',id=i,method='tools/call',params=dict(name=name,arguments=args))) for i,(name,args) in enumerate(messages))+'\n'
    result=subprocess.run([engine],input=data,encoding='utf-8',text=True,capture_output=True,check=True,timeout=55)
    return [json.loads(line)['result'] for line in result.stdout.splitlines()]


original_hash=hashlib.sha256(fixture.read_bytes()).hexdigest()
with tempfile.TemporaryDirectory(prefix='ark-source-mcp-') as folder:
    work=Path(folder);source=work/'peça original.STEP';shutil.copyfile(fixture,source)
    document=work/'source.ark';export=work/'projected.STEP'
    result=calls([('import_step',dict(path=str(source),replace=True)),('doc_save',dict(path=str(document))),
                  ('source_inspect',dict(id=1,progress=1)),('doc_load',dict(path=str(document))),
                  ('export_step',dict(id=1,path=str(export))),
                  ('doc_apply',dict(commands=[dict(op='feature.edit',id=1,p=dict(width_mm=1))])),('doc_info',{})])
    assert all(not x['isError'] for x in result[:5]),result
    assert result[5]['isError'] and result[6]['structuredContent']['revision']==2
    report=result[2]['structuredContent']['report']
    assert report['source_projection_verified'] and report['manufacturing_authoritative'] is False
    assert report['refold_symmetric_difference_volume_mm3']<=report['volume_tolerance_mm3']
    assert len(result[2]['structuredContent']['holes'])==report['opening_count']
    receipt=json.loads(export.with_suffix('.validation.json').read_text())
    assert receipt['export_pose']=='straight' and receipt['step_sha256']==hashlib.sha256(export.read_bytes()).hexdigest()
    assert receipt['source_sha256']==original_hash
    # The .ark embeds the verified exact STEP payload, so moved projects do not
    # depend on an old cache directory to export their saved geometry.
    contents=json.loads(document.read_text(encoding='utf-8'))
    package=contents['features'][0]['source']
    assert package['flat_step_base64']
    package['flat_step_path']=str(work/'nonexistent-cache.step')
    portable=work/'portable.ark';portable.write_text(json.dumps(contents),encoding='utf-8')
    recovered=calls([('doc_load',dict(path=str(portable))),('export_step',dict(id=1,path=str(work/'portable.step')))])
    assert not recovered[0]['isError'] and not recovered[1]['isError']
    # Source drift must invalidate export, not silently reuse a differently named source.
    source.write_bytes(source.read_bytes()+b'\n')
    failure=work/'must-not-export.step'
    rejected=calls([('doc_load',dict(path=str(document))),('export_step',dict(id=1,path=str(failure))),('doc_info',{})])
    assert not rejected[0]['isError'] and rejected[1]['isError'] and not failure.exists()
    assert rejected[2]['structuredContent']['revision']==1
assert hashlib.sha256(fixture.read_bytes()).hexdigest()==original_hash
print('Source MCP import/save/load, projected STEP hash, read-only geometry and source-drift rejection passed')
