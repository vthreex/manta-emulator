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
 diagnosticFlags=coldFlags+['--gpio-i2c','--defer-diagnostic-reads']
 deferred=run('deferred',diagnosticFlags)
 assert deferred['stop_pc']=='0x8054843c' and deferred['stop_address']=='0x1f002420'
 assert deferred['deferred_diagnostic_reads']==2 and len(deferred['invalid_observation_bytes'])==4
 assert len(deferred['recent_instructions'])==4096 and deferred['gop_aperture_events']==0
 timerTrace=temp/'timer.jsonl'
 timer=run('timer',diagnosticFlags+['--virtual-rtc','--cp0-timer','--record-trace',str(timerTrace)])
 timerEvents=[json.loads(line) for line in timerTrace.read_text().splitlines()]
 assert timer['stop_pc']=='0x804f1094' and timer['stop_address']=='0x1f207578'
 assert timer['guest_exception_vector_entries']==10 and timer['guest_software_tick']==10
 assert any(e['kind']=='rtc_snapshot' for e in timerEvents)
 assert sum(e['kind']=='exception_vector' for e in timerEvents)==10
 assert 'USB_Init_Configure' in (temp/'timer.console').read_text()
 utmi=run('utmi',diagnosticFlags+['--virtual-rtc','--cp0-timer','--candidate-utmi'])
 assert utmi['stop_pc']=='0x8053c1c4' and utmi['stop_address']=='0x1f00105c'
 assert utmi['guest_software_tick']==46 and 'USB_Init_Configure done...' in (temp/'utmi.console').read_text()
 spiTrace=temp/'spi.jsonl'
 spi=run('spi',diagnosticFlags+['--virtual-rtc','--cp0-timer','--candidate-utmi','--candidate-spi','--record-trace',str(spiTrace)])
 spiEvents=[json.loads(line) for line in spiTrace.read_text().splitlines()]
 assert spi['stop_pc']=='0x8053c6ec' and spi['stop_address']=='0x1f001030' and spi['reason']=='undocumented SPI opcode 0xcf for w25q64bv; input electrical state unknown'
 assert spi['instruction_dispatches']>104000000 and spi['guest_software_tick']==46
 assert spi['gop_aperture_events']==0 and not spi['gop_initializer_entered'] and not spi['osd_demonstrated']
 assert any(e['kind']=='spi_command' and e['address']=='0x1f001004' and e['value']=='0x5' for e in spiEvents)
 assert any(e['kind']=='read' and e['address']=='0x1f001014' and (int(e['value'],16)&1)==0 for e in spiEvents)
 assert len(spi['invalid_observation_bytes'])==4
 spiFlags=diagnosticFlags+['--virtual-rtc','--cp0-timer','--candidate-utmi','--candidate-spi']
 probeTrace=temp/'deferred-spi.jsonl'
 probe=run('deferred-spi',spiFlags+['--defer-spi-probe','--record-trace',str(probeTrace)])
 probeEvents=[json.loads(line) for line in probeTrace.read_text().splitlines()]
 assert probe['stop_pc']=='0x8053dd14' and probe['reason']=='unknown diagnostic observation consumed from guest RAM'
 assert probe['spi_jedec_read_returned'] and probe['spi_descriptor_selected'] and probe['spi_jedec']==[239,64,23]
 assert len(probe['invalid_observation_bytes'])==6 and sum(e['origin']=='0x1f001014' for e in probe['invalid_observation_bytes'])==2
 assert sum(e['kind']=='deferred_spi_input' for e in probeEvents)==2
 packets=[];packet=[]
 for e in probeEvents:
  if e['kind']=='spi_cs' and e['value']=='0x0':packet=[]
  if e['kind']=='spi_tx':packet.append(int(e['value'],16))
  if e['kind']=='spi_cs' and e['value']=='0x1':packets.append(packet)
 assert [0xcf,255,255,0] in packets and [0x9f] in packets
 assert any(e['kind']=='spi_jedec_return' and e['value']=='0xef4017' for e in probeEvents)
 miuTrace=temp/'miu-alias.jsonl'
 miu=run('miu-alias',spiFlags+['--exploratory-spi-input','pull-up','--record-trace',str(miuTrace)])
 miuEvents=[json.loads(line) for line in miuTrace.read_text().splitlines()]
 assert miu['stop_pc']=='0x8054cdcc' and miu['stop_address']=='0x1f003d98' and miu['instruction_dispatches']>173000000
 assert any(e['kind']=='guest_tlb_write' and e['pc']=='0x805cb7e0' for e in miuEvents)
 assert any(e['kind']=='cp0_mmu_write' and e['address']=='0x2' and e['value']=='0x1000007' for e in miuEvents)
 assert 'sar input test...' in (temp/'miu-alias.console').read_text()
 gammaFlags=spiFlags+['--exploratory-spi-input','pull-up','--candidate-chip-id','--exploratory-sc-bank','0','--candidate-gamma','--exploratory-gamma-enable','0']
 gammaTrace=temp/'gamma.jsonl'
 gamma=run('gamma',gammaFlags+['--break-pc','0x805fd41c','--record-trace',str(gammaTrace)])
 gammaEvents=[json.loads(line) for line in gammaTrace.read_text().splitlines()]
 assert gamma['stop_pc']=='0x805fd41c' and gamma['reason']=='PC breakpoint'
 assert gamma['guest_exception_vector_entries']==269 and gamma['guest_software_tick']==269 and gamma['gamma_ram_commits']==768
 assert gamma['instruction_dispatches']>174000000 and not gamma['gop_initializer_entered'] and not gamma['osd_demonstrated']
 assert len(gamma['invalid_observation_bytes'])==4
 writes=[e for e in gammaEvents if e['kind']=='gamma_write']
 assert len(writes)==768 and sum(e['kind']=='gamma_commit' for e in gammaEvents)==768
 assert set((e['width'],int(e['address'],16)) for e in writes)==set((channel,index) for channel in range(3) for index in range(256))
 gammaReplay=run('gamma-replay',gammaFlags+['--break-pc','0x805fd41c','--replay-trace',str(gammaTrace)])
 assert gamma==gammaReplay, 'full original boot/peripheral/IRQ trace must replay exactly'
 gamma1=run('gamma-enable-1',gammaFlags[:-1]+['1','--break-pc','0x805fd41c'])
 assert gamma1['stop_pc']==gamma['stop_pc'] and gamma1['gamma_ram_commits']==768 and gamma1['guest_software_tick']==269
 continuedTrace=temp/'continuation.jsonl'
 continued=run('continuation',gammaFlags+['--record-trace',str(continuedTrace)])
 continuedEvents=[json.loads(line) for line in continuedTrace.read_text().splitlines()]
 assert continued['stop_pc']=='0x805fbc8c' and continued['stop_address']=='0x1f205e78'
 assert continued['instruction_dispatches']==175471842 and continued['guest_software_tick']==273 and continued['guest_exception_vector_entries']==273
 assert continued['gamma_ram_commits']==768 and not continued['gop_initializer_entered'] and continued['gop_aperture_events']==0
 assert sum(e['origin'] in ('0x1f200a38','0x1f200a3c') for e in continued['invalid_observation_bytes'])==4
 assert sum(e['kind']=='unused_clock_snapshot' for e in continuedEvents)==2
 continuedReplay=run('continuation-replay',gammaFlags+['--replay-trace',str(continuedTrace)])
 assert continued==continuedReplay
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==hashlib.sha256(b).hexdigest(),'original SPI image changed'
Path(a.report).write_text(json.dumps({'result':'PASS','streams_verified':len(streams),'strict_reset':reset,'strict_replay':replay,'cpu_disabled_checkpoint':crippled,'diagnostic_stage2':stage,'without_gpio':noGpio,'firmware_from_reset':cold,'stuck_sda_fixture':fault,'invalid_diagnostic_cache':deferred,'guest_timer':timer,'utmi_init':utmi,'spi_probe':spi,'guarded_spi_identification':probe,'miu1_alias':miu,'exploratory_gamma':gamma,'exploratory_gamma_replay':gammaReplay,'exploratory_gamma_enable1':gamma1,'clock_table_continuation':continued,'clock_table_continuation_replay':continuedReplay,'guest_inflate_sha256':KNOWN[0][3]},indent=2)+'\n')
print('PASS: original reset/inflate, POR guard, CF packet/unknown cache, Winbond JEDEC/table selection, XIU/guest TLB/MIU1 alias, 269 original IRQs, 768 original gamma samples, both enable states and full deterministic replay; no authentic GOP frame claimed')
