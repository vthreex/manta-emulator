"""Original AVD reset execution with conservative CODE uncertainty, no TV inputs."""
import argparse, concurrent.futures, hashlib, json, re, subprocess, tempfile, zlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('firmware','probe','emulator','report'):p.add_argument('--'+name,required=True)
a=p.parse_args();stock=Path(a.firmware).read_bytes();stock_sha=hashlib.sha256(stock).hexdigest()
assert stock_sha=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
main=zlib.decompress(stock[0x23880:0x16124e],-15)
blob=main[0x80627a00-0x803b3980:0x8062ae00-0x803b3980]
assert hashlib.sha256(blob).hexdigest()=='48ab58a5689d3135b2cdf78fa77da92fa912776f741c33c7afa97fdcfc4ee4fd'
with tempfile.TemporaryDirectory(prefix='manta-symbolic-ROM-') as directory:
    image=Path(directory)/'original-avd.bin';image.write_bytes(blob)
    def run(extra):
        result=subprocess.run([a.probe,str(image),'--deferred-shared-prefix']+extra,
                              text=True,capture_output=True,timeout=30)
        assert result.returncode==0,(extra,result.returncode,result.stderr)
        text=result.stdout
        writes=[tuple(int(x,base) for x,base in zip(m,(16,16,10,10))) for m in re.findall(
            r'^avd_shared_register_write address 0x([0-9a-f]+) value 0x([0-9a-f]+).*XDATA=(\d+); known=(\d+)',text,re.M)]
        visits='\n'.join(re.findall(r'^[0-9a-f]{4} [0-9a-f]{2} .*$',text,re.M))
        return text,writes,hashlib.sha256(visits.encode()).hexdigest()
    symbolic,writes,sequence=run(['--symbolic-code-tables'])
    assert 'AVD_ORIGINAL_INSTRUCTIONS 863 PC 0x702 SP 0xb2' in symbolic
    assert 'configurations=7 shared_reads=5 shared_writes=21 ram_writes=25 interrupts=0' in symbolic
    assert 'SYNTHETIC_ISOLATED_STARTUP_SELECTOR' not in symbolic
    assert writes[-1]==(0x1f206b38,0,0x119c,255),'original 1B2B must establish selector zero itself'
    assert all(x[0] not in (0x1f206bb8,0x1f206b8d,0x1f206b90) for x in writes),'no request or HTotal supplied'
    thresholds=[w for w in writes if 0x117c<=w[2]<=0x117f]
    assert [w[3] for w in thresholds]==[0,255,0,0],thresholds
    def differential(value):
        concrete,cw,cs=run(['--synthetic-startup-selector',str(value),'255'])
        uncertain,uw,us=run(['--synthetic-startup-selector',str(value),'0','--symbolic-code-tables'])
        assert cs==sequence==us,'uncertainty cannot choose a different instruction path'
        for candidate in (cw,uw):
            assert len(candidate)==len(writes)
            for abstract,actual in zip(writes,candidate):
                address,data,native,known=abstract
                assert (address,native)==(actual[0],actual[2])
                assert ((data^actual[1])&known)==0,(value,abstract,actual)
        assert [x[3] for x in uw]==[x[3] for x in writes]
        return {'selector_fixture':value,'concrete_sha256':hashlib.sha256(concrete.encode()).hexdigest(),
                'unknown_representative_sha256':hashlib.sha256(uncertain.encode()).hexdigest()}
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        cases=list(pool.map(differential,range(256)))
    assert run(['--symbolic-code-tables'])[0]==symbolic,'exact instruction/write/state replay'
    flag_cases=[]
    for bit,count in ((0,2278),(1,2309)):
        text,fw,fs=run(['--synthetic-11db-bit7',str(bit),'--symbolic-code-tables'])
        assert 'SYNTHETIC_ISOLATED_11DB_BIT7' in text and 'not connected to television boot' in text
        assert f'AVD_ORIGINAL_INSTRUCTIONS {count} PC 0x993 SP 0xae' in text
        assert '0284 d2 SETB  IE.7' in text and '0292 12 LCALL #098Dh' in text
        assert 'AVD_BOUNDARY MCU unknown IRAM bit affects instruction/decision' in text
        assert fw[-1]==(0x1f206bac,0xa5,0x11d6,0),'unknown private timer byte must remain invalid'
        assert all(w[2] not in (0x11dc,0x11c7,0x11c8) for w in fw)
        flag_cases.append({'synthetic_11DB_bit7':bit,'instructions':count,'pc':'0x0993',
                           'main_loop_entered':True,'interrupt_enable_executed':True,
                           'hardware_events_and_HTotal_remain_unknown':True,'stdout':text})
    rejected=subprocess.run([a.emulator,'--firmware',a.firmware,'--diagnostic-avd-symbolic-code-tables'],capture_output=True,text=True)
    assert rejected.returncode==1 and 'requires guarded shared execution' in rejected.stderr
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==stock_sha
report={'result':'PASS','scope':'original AVD from reset, no hardware selector supplied; candidate bus/clock/deferred effects remain hypotheses',
        'symbolic_instructions':863,'pc':'0x0702','native_required_input':'11DB bit7; meaning/producer unknown',
        'original_selector_write':'1B2B via 02AA/02AD -> 119C = 0, known FF',
        'HTotal_publisher_executed':False,'host_request_written':False,'threshold_writes':thresholds,
        'differential_cases':cases,'full_stdout':symbolic,'first_visit_sequence_sha256':sequence,
        'isolated_flag_cases_NOT_TV_progress':flag_cases,
        'firmware_sha256':stock_sha,'program_sha256':hashlib.sha256(blob).hexdigest(),
        'probe_sha256':hashlib.sha256(Path(a.probe).read_bytes()).hexdigest(),
        'emulator_sha256':hashlib.sha256(Path(a.emulator).read_bytes()).hexdigest()}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: original AVD 863 instructions without selector; 256 concrete and 256 unknown-representative differential cases')
