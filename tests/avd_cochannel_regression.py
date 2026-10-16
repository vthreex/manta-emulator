"""Original AVD co-channel output fragments plus original MIPS getter; isolated only."""
import argparse,hashlib,json,re,subprocess,tempfile,zlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for n in ('firmware','probe','startup-probe','report'):p.add_argument('--'+n,required=True)
a=p.parse_args();stock=Path(a.firmware).read_bytes();sha=hashlib.sha256(stock).hexdigest()
assert sha=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
main=zlib.decompress(stock[0x23880:0x16124e],-15)
with tempfile.TemporaryDirectory(prefix='manta-cochannel-fragments-') as directory:
    path=Path(directory)/'original-main.bin';path.write_bytes(main)
    first=subprocess.check_output([a.probe,str(path)],text=True,timeout=30)
    assert subprocess.check_output([a.probe,str(path)],text=True,timeout=30)==first
    report=json.loads(first)
    assert report['result']=='PASS' and report['latch_cases']==2 and report['unknown_latch_cases']==2
    assert report['original_MIPS_getters']==6 and report['original_AVD_fragment_instructions']==157
    assert report['initial_status_invalid'] and not report['TV_detector_status_supplied']
    assert not report['TV_AVD_advanced'] and not report['pixels_generated']
    assert path.read_bytes()==main
    avd=main[0x80627a00-0x803b3980:0x8062ae00-0x803b3980]
    program=Path(directory)/'original-avd.bin';program.write_bytes(avd);startup=[]
    for bit,count in ((0,2278),(1,2309)):
        text=subprocess.check_output([a.startup_probe,str(program),'--deferred-shared-prefix',
            '--synthetic-11db-bit7',str(bit),'--symbolic-code-tables'],text=True,timeout=30)
        assert f'AVD_ORIGINAL_INSTRUCTIONS {count} PC 0x993 SP 0xae' in text
        assert '0245 51 ACALL 02AAh' in text and '0247 e4 CLR' in text
        writes=re.findall(r'^avd_shared_register_write address 0x([0-9a-f]+) value 0x([0-9a-f]+).*XDATA=(\d+); known=(\d+)',text,re.M)
        cochannel=[row for row in writes if int(row[2])==0x11ca]
        assert cochannel==[('1f206b94','0',str(0x11ca),'255')],cochannel
        assert 'SYNTHETIC_ISOLATED_11DB_BIT7' in text and 'not connected to television boot' in text
        startup.append({'synthetic_coefficient_selector_bit7':bit,'original_startup_instructions':count,
            'stop':'0x0993 / unknown event bit','original_11CA_zero_write_observed':True,
            'stdout_sha256':hashlib.sha256(text.encode()).hexdigest(),'scope':'isolated conditional full original reset; NOT connected progress'})
    assert program.read_bytes()==avd
    report['isolated_conditional_original_reset_cases']=startup
    damaged=bytearray(main);damaged[0x80627a00-0x803b3980+0x1183]^=1;path.write_bytes(damaged)
    result=subprocess.run([a.probe,str(path)],capture_output=True,text=True)
    assert result.returncode==1 and 'original main image mismatch' in result.stderr
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==sha
report['firmware_sha256']=sha;report['probe_sha256']=hashlib.sha256(Path(a.probe).read_bytes()).hexdigest()
report['startup_probe_sha256']=hashlib.sha256(Path(a.startup_probe).read_bytes()).hexdigest()
report['scope_note']='Separate synthetic program concatenates unchanged startup-zero, detector SETB/CLR, shared-byte read and flag-publication instructions with original absolute helpers. It does not execute original reset-to-detector control flow. Linked MIPS context RAM is an explicit fixture; original instructions/resources and input files stay unchanged.'
report['unknown_note']='An INVALID byte is copied into the private latch using original MOVX/MOV instructions. Default mode stops before storing unknown RAM; symbolic-data mode preserves it and stops at JNB. Neither produces a second publication.'
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: original startup/flag publications and MIPS getter, with strict/symbolic unknown guards; no TV detector status supplied')
