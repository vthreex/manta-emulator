"""Isolated synthetic AVD startup-field study. No selector is supplied to TV boot."""
import argparse, concurrent.futures, hashlib, json, re, subprocess, tempfile, zlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('firmware','probe','emulator','report'):p.add_argument('--'+name,required=True)
a=p.parse_args();stock=Path(a.firmware).read_bytes()
stock_sha=hashlib.sha256(stock).hexdigest()
assert stock_sha=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
main=zlib.decompress(stock[0x23880:0x16124e],-15)
blob=main[0x80627a00-0x803b3980:0x8062ae00-0x803b3980]
assert len(blob)==13312 and hashlib.sha256(blob).hexdigest()=='48ab58a5689d3135b2cdf78fa77da92fa912776f741c33c7afa97fdcfc4ee4fd'
with tempfile.TemporaryDirectory(prefix='manta-isolated-selector-') as directory:
    image=Path(directory)/'original-avd.bin';image.write_bytes(blob)
    def run(value,known):
        result=subprocess.run([a.probe,str(image),'--deferred-shared-prefix','--synthetic-startup-selector',
                               str(value),str(known)],text=True,capture_output=True,timeout=15)
        assert result.returncode==0,(value,known,result.returncode,result.stderr)
        text=result.stdout
        assert 'SYNTHETIC_ISOLATED_STARTUP_SELECTOR' in text and 'not connected to television boot' in text
        assert 'avd_shared_register_write address 0x1f206bb8' not in text,'no host request/idle command may be invented'
        assert 'AVD_BOUNDARY AVD MCU unknown XDATA bits affect instruction/decision/address' in text
        count,pc=re.search(r'AVD_ORIGINAL_INSTRUCTIONS (\d+) PC (0x[0-9a-f]+)',text).groups()
        visits='\n'.join(re.findall(r'^[0-9a-f]{4} [0-9a-f]{2} .*$',text,re.M))
        return {'value':value,'known_mask':known,'instructions':int(count),'pc':pc,
                'stdout_sha256':hashlib.sha256(text.encode()).hexdigest(),
                'first_visit_sequence_sha256':hashlib.sha256(visits.encode()).hexdigest()},text
    def full(value):
        report,text=run(value,255)
        assert report['instructions']==863 and report['pc']=='0x702'
        assert 'shared_reads=5 shared_writes=21 ram_writes=25 interrupts=0' in text
        assert 'avd_shared_register_write address 0x1f206a98 value 0x25' in text and 'known=224' in text
        assert 'avd_shared_register_read address 0x1f206bb5 value 0xa5' in text
        return report
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:full_cases=list(pool.map(full,range(256)))
    assert len({r['first_visit_sequence_sha256'] for r in full_cases})==1
    partial=[]
    for known,count,pc in [(0,379,'0x1c26'),(0x16,404,'0x1c3c'),(0xfe,863,'0x702')]+[
            (0xfe&~(1<<bit),863 if bit==3 else 379 if bit in (1,2,4) else 404,
             '0x702' if bit==3 else '0x1c26' if bit in (1,2,4) else '0x1c3c') for bit in range(1,8)]:
        r,_=run(0,known);assert (r['instructions'],r['pc'])==(count,pc),(known,r)
        partial.append(r)
    assert full(0)==full_cases[0] and full(255)==full_cases[255],'isolated output/state replay must match exactly'
    rejected=[]
    for value,known in [('256','255'),('0','256'),('-1','255'),('bad','255')]:
        result=subprocess.run([a.probe,str(image),'--deferred-shared-prefix','--synthetic-startup-selector',value,known],text=True,capture_output=True)
        assert result.returncode==1 and 'AVD_ORIGINAL_INSTRUCTIONS' not in result.stdout
        rejected.append([value,known])
    rejected_tv=subprocess.run([a.emulator,'--firmware',a.firmware,'--synthetic-startup-selector','0','255'],text=True,capture_output=True)
    assert rejected_tv.returncode==1 and 'Unsupported option/feature: --synthetic-startup-selector' in rejected_tv.stderr,rejected_tv.stderr
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==stock_sha
report={'result':'PASS','scope':'ISOLATED SYNTHETIC fixtures only; NOT connected TV execution, reset values or graphics',
        'full_byte_cases':full_cases,'partial_bit_cases':partial,'invalid_cases':rejected,
        'fixture_option_rejected_by_TV_emulator':True,'TV_selector_supplied':False,
        'connected_AVD_progress_unchanged':379,'next_isolated_bit_consumer':'0702 JNB ACC.7; native 11DB / candidate BF206BB5',
        'original_IRAM_mask':'1CB1 ANL 07,#1F; unknown low five bits retained through actual same-origin store',
        'program_sha256':hashlib.sha256(blob).hexdigest(),'firmware_sha256':stock_sha,
        'probe_sha256':hashlib.sha256(Path(a.probe).read_bytes()).hexdigest(),
        'emulator_sha256':hashlib.sha256(Path(a.emulator).read_bytes()).hexdigest()}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: 256 isolated original AVD runs and ten partial-field cases; no selector or measurements supplied to TV')
