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
  (temp/(name+'.console')).write_text(result.stdout)
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
 coldFlags=['--symbolic-init','--candidate-miu','--candidate-bdma','--candidate-uart','--max-instructions','200000000']
 noGpio=run('without-gpio',coldFlags)
 assert noGpio['runtime_entry_reached'] and noGpio['stop_pc']=='0x8053bde4' and noGpio['stop_address']=='0x1f203d05'
 assert '@@@ Hello T4' in (temp/'without-gpio.console').read_text()
 coldTrace=temp/'cold.jsonl'
 cold=run('cold',coldFlags+['--gpio-i2c','--record-trace',str(coldTrace)])
 coldEvents=[json.loads(line) for line in coldTrace.read_text().splitlines()]
 assert cold['mode']=='firmware from reset flash' and not cold['diagnostic_init_prefix']
 assert cold['second_stage_reached'] and cold['runtime_entry_reached'] and cold['guest_decompressed_sha256']==KNOWN[0][3]
 assert cold['stop_pc']=='0x80469898' and cold['stop_address']=='0x1f200a38'
 assert all(s in (temp/'cold.console').read_text() for s in ['BIST0-OK','BIST1-OK','@@@ Hello T4'])
 assert any(e['kind']=='bist_compare' and e['address']=='0x8000000' and e['width']==0x8000000 for e in coldEvents)
 assert any(e['kind']=='bist_compare' and e['address']=='0x0' and e['width']==0x8000000 for e in coldEvents)
 samples=[e for e in coldEvents if e['kind']=='read' and e['pc']=='0x8053bde4' and e['address']=='0x1f203d05']
 assert len(samples)>=2 and all(int(e['value'],16)&6==6 for e in samples)
 assert any(e['kind']=='write' and e['address']=='0x1f2033d0' and int(e['value'],16)&65535==65535 for e in coldEvents)
 faultTrace=temp/'fault.jsonl'
 fault=run('fault',coldFlags+['--gpio-i2c','--i2c-sda-stuck','--record-trace',str(faultTrace)])
 faultEvents=[json.loads(line) for line in faultTrace.read_text().splitlines()]
 transitions=[e['value'] for e in faultEvents if e['kind']=='i2c_lines' and e['address']=='0x1f203d05']
 assert transitions.count('0x2')==9 and transitions.count('0x0')==9,transitions
 assert 'IIC init fail SDA:0 SCL:1' in (temp/'fault.console').read_text()
 assert fault['runtime_entry_reached'] and fault['i2c_sda_stuck_fixture']
Path(a.report).write_text(json.dumps({'result':'PASS','streams_verified':len(streams),'strict_reset':reset,'strict_replay':replay,'cpu_disabled_checkpoint':crippled,'diagnostic_stage2':stage,'without_gpio':noGpio,'firmware_from_reset':cold,'stuck_sda_fixture':fault,'guest_inflate_sha256':KNOWN[0][3]},indent=2)+'\n')
print('PASS: twelve streams, reset/replay, CPU dependency, guest inflate, cold boot Hello T4, GPIO dependency and original nine-clock I2C recovery')
