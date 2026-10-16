"""Isolated original AVD event-consumer tests; no physical event producer asserted."""
import argparse,concurrent.futures,hashlib,json,re,subprocess,tempfile,zlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for n in ('firmware','probe','emulator','report'):p.add_argument('--'+n,required=True)
a=p.parse_args();stock=Path(a.firmware).read_bytes();sha=hashlib.sha256(stock).hexdigest()
assert sha=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
main=zlib.decompress(stock[0x23880:0x16124e],-15)
blob=main[0x80627a00-0x803b3980:0x8062ae00-0x803b3980]
assert hashlib.sha256(blob).hexdigest()=='48ab58a5689d3135b2cdf78fa77da92fa912776f741c33c7afa97fdcfc4ee4fd'
with tempfile.TemporaryDirectory(prefix='manta-avd-event-consumer-') as directory:
    image=Path(directory)/'original-avd.bin';image.write_bytes(blob)
    def run(case):
        flag,value=case
        result=subprocess.run([a.probe,str(image),'--deferred-shared-prefix','--synthetic-11db-bit7',str(flag),
              '--synthetic-event-status',str(value),'--symbolic-code-tables'],capture_output=True,text=True,timeout=30)
        assert result.returncode==0,(case,result.stderr)
        text=result.stdout;count=2293+31*flag+(3 if value&4 else 0)
        assert 'SYNTHETIC_ISOLATED_EVENT_STATUS' in text and 'not connected to television boot' in text
        assert f'AVD_ORIGINAL_INSTRUCTIONS {count} PC 0x2ad SP 0xb0' in text
        assert 'address 0x11fc' in text and 'XDATA write unsupported' in text
        assert f'AVD_XDATA write 0x11fc value 0x{value^255:x}' in text
        assert ('0996 e4 CLR' in text)==bool(value&4)
        assert 'shared_writes=73 ram_writes=67 interrupts=0' in text
        assert 'avd_shared_register_write address 0x1f206bf8' not in text
        assert 'avd_shared_register_write address 0x1f206bb8' not in text
        assert '0abc ' not in text
        sequence='\n'.join(re.findall(r'^[0-9a-f]{4} [0-9a-f]{2} .*$',text,re.M))
        return {'synthetic_11DB_bit7':flag,'synthetic_11FC_byte':value,'instructions':count,
                'first_visit_sequence_sha256':hashlib.sha256(sequence.encode()).hexdigest(),
                'stdout_sha256':hashlib.sha256(text.encode()).hexdigest()},text
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results=list(pool.map(run,[(flag,value) for flag in (0,1) for value in range(256)]))
    for flag in (0,1):
        for bit2 in (0,4):
            hashes={r['first_visit_sequence_sha256'] for r,_ in results if r['synthetic_11DB_bit7']==flag and r['synthetic_11FC_byte']&4==bit2}
            assert len(hashes)==1,'unconsumed event fields cannot select another instruction path before acknowledgement'
    assert run((0,0))[0]==results[0][0]
    rejected=subprocess.run([a.emulator,'--firmware',a.firmware,'--synthetic-event-status','0'],capture_output=True,text=True)
    assert rejected.returncode==1 and 'Unsupported option/feature' in rejected.stderr
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==sha
report={'result':'PASS','scope':'512 ISOLATED synthetic status cases, original code from reset; NOT connected-TV progress or hardware event semantics',
        'cases':[r for r,_ in results],'sample_clear':results[0][1],'sample_bit2':results[4][1],
        'observed_bit2_role':'0993 -> 0996..0999 clears IRAM62 and IRAM30 when set',
        'attempted_ack':'099B..09A1 -> original MOVX 02AD, native11FC, data=~captured_status',
        'acknowledgement_rejected_before_backing_write':True,'event_or_HTotal_produced':False,
        'connected_AVD_progress_unchanged':863,'firmware_sha256':sha,
        'probe_sha256':hashlib.sha256(Path(a.probe).read_bytes()).hexdigest(),
        'emulator_sha256':hashlib.sha256(Path(a.emulator).read_bytes()).hexdigest()}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: 512 original AVD event-consumer fixtures; unsupported acknowledgement cannot manufacture pending events')
