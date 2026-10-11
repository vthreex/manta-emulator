"""Requires a local original firmware; no guest substitution or binary fixtures."""
import argparse,json,subprocess,tempfile,sys,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from firmware_extract import validate,inflate_at,KNOWN
p=argparse.ArgumentParser();p.add_argument('--firmware',required=True);p.add_argument('--emulator',required=True);p.add_argument('--report',required=True);a=p.parse_args()
b=Path(a.firmware).read_bytes();validate(b)
streams=[]
for s,e,n,h in KNOWN:
 end,data=inflate_at(b[:0x800000],s)
 assert (end,len(data),hashlib.sha256(data).hexdigest())==(e,n,h)
 streams.append({'start':hex(s),'length':n,'sha256':h})
manifest=json.loads((Path(__file__).resolve().parents[1]/'evidence/forensics/manifest.json').read_text())
for r in manifest['streams'][4:]:
 end,data=inflate_at(b[:0x800000],r['start']);assert (end,len(data),hashlib.sha256(data).hexdigest())==(r['end'],r['length'],r['sha256']);streams.append(r)
with tempfile.TemporaryDirectory(prefix='manta-regression-') as temp:
 base=[a.emulator,'--firmware',a.firmware,'--headless'];temp=Path(temp)
 def run(name,extra):
  path=temp/(name+'.json');result=subprocess.run(base+extra+['--summary',str(path)],capture_output=True,text=True,check=False)
  assert result.returncode==2,(result.returncode,result.stderr)
  return json.loads(path.read_text())
 trace=temp/'reset.jsonl'
 reset=run('reset',['--trace-instructions','--record-trace',str(trace)])
 assert reset['stop_pc']=='0xbfc00c60' and reset['stop_address']=='0x1f001c74' and reset['instruction_dispatches']>=110 and not reset['runtime_entry_reached']
 replay=run('replay',['--trace-instructions','--replay-trace',str(trace)])
 assert reset==replay
 crippled=run('crippled',['--break-pc','0xbfc00000'])
 assert not crippled['runtime_entry_reached'] and crippled['instruction_dispatches']<reset['instruction_dispatches']
 stageTrace=temp/'stage.jsonl'
 stage=run('stage',['--diagnostic-stage2','--symbolic-init','--candidate-bdma','--max-instructions','200000000','--record-trace',str(stageTrace)])
 events=[json.loads(line) for line in stageTrace.read_text().splitlines()]
 hashes=[e for e in events if e['kind']=='guest_inflate_hash']
 assert stage['guest_inflate_entered'] and stage['runtime_entry_reached'] and stage['instruction_dispatches']>80000000
 assert len(hashes)==1 and hashes[0]['detail']=='sha256='+KNOWN[0][3]
 assert any(e['kind']=='bdma_crc' and e['value']=='0x5f65d0d4' for e in events)
Path(a.report).write_text(json.dumps({'result':'PASS','streams_verified':len(streams),'strict_reset':reset,'strict_replay':replay,'cpu_disabled_checkpoint':crippled,'diagnostic_stage2':stage,'guest_inflate_sha256':KNOWN[0][3]},indent=2)+'\n')
print('PASS: twelve streams, strict reset/replay, CPU dependency, stock guest inflate hash and runtime entry')
