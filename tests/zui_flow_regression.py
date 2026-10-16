"""Original ZUI queue/dispatcher function tests and read-only strict-boot probes."""
import argparse,hashlib,json,subprocess,tempfile,zlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for n in ('firmware','probe','emulator','report'):p.add_argument('--'+n,required=True)
a=p.parse_args();stock=Path(a.firmware).read_bytes();sha=hashlib.sha256(stock).hexdigest()
assert sha=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
main=zlib.decompress(stock[0x23880:0x16124e],-15)
assert hashlib.sha256(main).hexdigest()=='149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236'
signatures={}
for address,size,digest in ((0x804e0d48,0x1cc,'9da8c036aca630ca87967416a7c79bb6bf2a891d5f570de86be61fabaaba3f92'),
                            (0x8049f040,0x140,'4ce696096aded2ef3fbfea2ab738d4aa276251132213ce7ddd5aebeaa4aa47b1')):
    code=main[address-0x803b3980:address-0x803b3980+size]
    assert hashlib.sha256(code).hexdigest()==digest
    signatures[hex(address)]={'bytes':size,'sha256':digest}
with tempfile.TemporaryDirectory(prefix='manta-original-zui-') as directory:
    d=Path(directory);image=d/'original-main.bin';image.write_bytes(main)
    first=subprocess.check_output([a.probe,str(image)],text=True,timeout=30)
    assert subprocess.check_output([a.probe,str(image)],text=True,timeout=30)==first
    functions=json.loads(first)
    assert functions['result']=='PASS' and functions['queue_cases']==40
    assert functions['standby_default_cases']==4 and functions['active_dispatch_cases']==4
    assert functions['window_resource_cases']==29 and functions['original_window_records']>3000
    assert functions['source_timeout_cases']==1620 and functions['source_timeout_transition_boundaries']==54
    assert functions['elapsed_getter_cases']==30
    assert functions['source_phase_cases']==15 and functions['wrapper_selection_cases']==9
    assert functions['startup_source_cases']==3
    assert functions['resource3_windows_before_surface']==113
    assert functions['OSD_open_selection_cases']==87
    assert functions['OSD_open_original_assert_ids']==3
    assert functions['resource3_creation_routes']==113 and functions['isolated_empty_queue_reaches_paint_boundary']
    assert functions['original_window_timer_cases']==6
    assert functions['paint_window']==4 and functions['paint_primitive_type']==4 and functions['paint_primitive_id']==65
    assert functions['paint_resource_address']==0x80686ad0
    assert functions['paint_clip_synthetic_RAM_cases']==16 and functions['paint_GE_context_cases']==16
    assert functions['paint_GE_clip_writes']==64 and functions['paint_GE_context_executed']
    assert functions['paint_strict_GE_unknown_stops']
    assert not functions['paint_surface_allocated'] and not functions['paint_GE_raster_issued']
    assert not functions['TV_dispatcher_invoked'] and not functions['TV_events_injected'] and not functions['pixels_generated']
    damaged=d/'damaged-main.bin';bad=bytearray(main);bad[0x804e0dc4-0x803b3980]^=1;damaged.write_bytes(bad)
    result=subprocess.run([a.probe,str(damaged)],capture_output=True,text=True)
    assert result.returncode==1 and 'size/hash mismatch' in result.stderr
    states=[]
    for enabled in (False,True):
        summary=d/f'{enabled}.json'
        command=[a.emulator,'--firmware',a.firmware,'--headless','--summary',str(summary)]
        if enabled:command+=['--zui-flow-probes']
        result=subprocess.run(command,capture_output=True,text=True,timeout=30)
        assert result.returncode==2
        states.append(json.loads(summary.read_text()))
    observed=states[1];assert observed.pop('zui_flow_probes_enabled')
    flow=observed.pop('zui_flow')
    assert flow=={'read_only':True,'signature_failures':0,'dropped_observations':0,'entries':{},'snapshots':[]}
    assert states[0]==observed,'enabling observation must preserve every strict CPU/peripheral summary field'
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==sha
report={'result':'PASS','scope':'isolated original ZUI functions with synthetic RAM; strict TV execution unchanged, no UI call/event injected',
        'original_functions':functions,'original_code_signatures':signatures,'corrupted_image_rejected':True,
        'strict_summary_identical_except_empty_observer':True,'firmware_sha256':sha,
        'probe_sha256':hashlib.sha256(Path(a.probe).read_bytes()).hexdigest(),
        'emulator_sha256':hashlib.sha256(Path(a.emulator).read_bytes()).hexdigest()}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: original queue/dispatcher and isolated OSD3 paint through GE clipping; strict execution unchanged; no TV UI invoked or raster output')
