"""Create a synthetic launch demonstrator via revision-checked native MCP.
No user drawings or network providers are used. All outputs remain under out/.
"""
import argparse, atexit, json, pathlib, subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / 'out/launch-demo'
OUT.mkdir(parents=True, exist_ok=True)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runtime-dir',type=pathlib.Path,default=ROOT/'out/native-release/Release',help='Directory containing the native ark-mcp.exe runtime')
options=parser.parse_args()
EXE = options.runtime_dir.resolve() / 'ark-mcp.exe'
if not EXE.is_file(): parser.error('Native MCP runtime missing: '+str(EXE))
p = subprocess.Popen([str(EXE)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8')
def close_process():
    if not p.stdin.closed: p.stdin.close()
    try: p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        p.kill();p.wait(timeout=10)
atexit.register(close_process)
receipts=[]
def rpc(method, params):
    idx=len(receipts)+1
    p.stdin.write(json.dumps({'jsonrpc':'2.0','id':idx,'method':method,'params':params})+'\n');p.stdin.flush()
    while True:
        r=json.loads(p.stdout.readline())
        if r.get('id')==idx: break
    receipts.append(r)
    (OUT/'model-receipts.json').write_text(json.dumps(receipts,indent=2),encoding='utf-8')
    if 'error' in r: raise RuntimeError(r)
    data=r.get('result',{})
    if data.get('isError'): raise RuntimeError(data)
    return data.get('structuredContent',data)
def tool(name,**args): return rpc('tools/call',{'name':name,'arguments':args})
def apply(commands):
    info=tool('doc_info')
    return tool('doc_apply',expected_revision=info['revision'],commands=commands)
rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'ARK launch demonstrator','version':'1'}})
tool('agent_onboard',topic='overview');tool('agent_onboard',topic='surfacing');tool('agent_onboard',topic='materials');tool('agent_onboard',topic='bom')
apply([{'op':'param.set','name':'plate_width','expr':'100'},
       {'op':'material.set','name':'Aluminium nominal','density':2.7,'appearance':{'base_color':[0.15,0.43,0.67],'metallic':0.65,'roughness':0.26}},
       {'op':'feature.add','type':'native_solid','name':'Plate blank','native':{'op':'box'},'p':{'width':'plate_width','depth':'65','height':'6'}},
       {'op':'feature.add','type':'native_solid','name':'Rounded blank','inputs':['Plate blank'],'native':{'op':'fillet','selection':'all'},'p':{'radius':'1.5'}}])
names=[]
for i,(x,y) in enumerate([('12','12'),('plate_width-12','12'),('12','53'),('plate_width-12','53')],1):
    name='Mount hole '+str(i);names.append(name)
    apply([{'op':'feature.add','type':'native_solid','name':name+' tool','native':{'op':'cylinder'},'p':{'radius':'4','height':'10'}},
           {'op':'feature.add','type':'native_solid','name':name,'inputs':[name+' tool'],'native':{'op':'transform'},'p':{'tx':x,'ty':y,'tz':'-2'}}])
final=apply([{'op':'feature.add','type':'native_solid','name':'Sensor mounting plate','inputs':['Rounded blank']+names,'native':{'op':'cut'},'p':{},'material':'Aluminium nominal','part':{'part_number':'DEMO-PLATE-001','description':'Synthetic sensor mounting plate','part_revision':'A','category':'mount','procurement':'make','representation':'exact'}}])
part_id=final['created_ids'][0]
before=tool('native_inspect',id=part_id)
tool('doc_save',path=str(OUT/'plate-before.ark'))
apply([{'op':'param.set','name':'plate_width','expr':'140'}])
after=tool('native_inspect',id=part_id)
tool('doc_save',path=str(OUT/'plate-after.ark'))
bom=tool('bom_inspect');(OUT/'bom.json').write_text(json.dumps(bom,indent=2),encoding='utf-8')
review=tool('bom_review',expected_revision=tool('doc_info')['revision'],scope='concept',fastening_review='Four mounting holes modelled. Hardware, mating interfaces and fabrication qualification remain outside this synthetic demonstration.',pending_notes='Nominal material is illustrative; production method, tolerances and hardware are unspecified.')
(OUT/'evidence.json').write_text(json.dumps({'before':before,'after':after,'bom_review':review,'receipts':receipts},indent=2),encoding='utf-8')
print(json.dumps({'before_bounds_mm':before['report']['bounds_mm'],'after_bounds_mm':after['report']['bounds_mm'],'brep_valid':[before['report']['brep_valid'],after['report']['brep_valid']],'bom_items':len(bom.get('items',[]))}))
close_process()


