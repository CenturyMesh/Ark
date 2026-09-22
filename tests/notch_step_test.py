"""Optional independent OCCT integration test. Pass an ARK MCP executable."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from export_notch_step import export

engine = str(Path(sys.argv[1]).resolve())
base = dict(width_mm=40, height_mm=30, wall_mm=2, length_mm=500,
            hinge_offset_mm=1, notches=[])
requests = []
for index, face in enumerate(('bottom','top','left','right'),1):
    args = copy.deepcopy(base)
    args['notches'] = [dict(station_mm=250, angle_deg=65, hinge_face=face)]
    requests.append(dict(jsonrpc='2.0', id=index, method='tools/call', params=dict(name='notch_plan', arguments=args)))
process = subprocess.run([engine], input='\n'.join(map(json.dumps,requests))+'\n', text=True,
                         capture_output=True, check=True, timeout=15)
with tempfile.TemporaryDirectory(prefix='ark-notch-') as folder:
    for index, line in enumerate(process.stdout.splitlines()):
        result = json.loads(line)['result']
        assert not result['isError']
        plan = result['structuredContent']
        report = export(plan, Path(folder) / f'case-{index}.step')
        assert report['solids'] == 1 and report['brep_valid']
        assert report['retained_wall_volume_max_error_mm3'] < report['volume_tolerance_mm3']
        assert report['roundtrip_symmetric_difference_volume_mm3'] < report['volume_tolerance_mm3']
        assert report['manufacturing_authoritative'] is False
        nonfinite = copy.deepcopy(plan)
        nonfinite['notches'][0]['cutter_polygon'][0][0] = float('nan')
        try:
            export(nonfinite, Path(folder) / 'nonfinite.step')
        except ValueError:
            pass
        else:
            raise AssertionError('Non-finite cutter escaped validation')
        altered = copy.deepcopy(plan)
        altered['notches'][0]['cutter_polygon'][0][0] += 1
        try:
            export(altered, Path(folder) / 'tampered.step')
        except ValueError:
            pass
        else:
            raise AssertionError('Modified cutter escaped independent validation')
print('Four retained-wall orientations: solid validity, volume, STEP roundtrip and tamper rejection passed')
