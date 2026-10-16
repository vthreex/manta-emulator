"""Isolated original timing-copy/getter/stability instructions; no TV sample supplied."""
import argparse,hashlib,json,subprocess,tempfile,zlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('firmware','probe','report'):p.add_argument('--'+name,required=True)
a=p.parse_args();firmware=Path(a.firmware).read_bytes();digest=hashlib.sha256(firmware).hexdigest()
assert digest=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
main=zlib.decompress(firmware[0x23880:0x16124e],-15)
with tempfile.TemporaryDirectory(prefix='manta-timing-fragments-') as directory:
    image=Path(directory)/'original-main.bin';image.write_bytes(main)
    output=subprocess.check_output([a.probe,str(image)],text=True,timeout=60)
    assert subprocess.check_output([a.probe,str(image)],text=True,timeout=60)==output
    report=json.loads(output)
    assert report['result']=='PASS' and report['sample_copy_cases']==2348
    assert report['invalid_sample_cases']==276 and report['fragment_instructions']==117400
    assert report['MIPS_getter_cases']==512 and report['MIPS_stability_cases']==320
    assert report['interleaved_copy_cases']==64 and report['interleaved_original_getters']==128
    assert report['mixed_generation_samples']==34 and report['additional_fragment_instructions']==6400
    assert report['publication_between_getter_reads_cases']==64 and report['between_read_mixed_samples']==34
    assert report['interleaved_original_stability_cases']==64
    assert report['post_stability_cochannel_boundary_cases']==9
    assert not report['TV_sample_supplied'] and not report['TV_publisher_executed'] and not report['pixels_generated']
    damaged=bytearray(main);damaged[0x80627a00-0x803b3980+0xaa9]^=1;image.write_bytes(damaged)
    rejected=subprocess.run([a.probe,str(image)],capture_output=True,text=True)
    assert rejected.returncode==1 and 'original main image mismatch' in rejected.stderr
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==digest
report['firmware_sha256']=digest
report['probe_sha256']=hashlib.sha256(Path(a.probe).read_bytes()).hexdigest()
report['scope_note']='8051 synthetic harness copies unchanged original AA9..ABE and 75E..78D fragments; not original reset-to-publisher execution. MIPS isolated functions/blocks use the unchanged original image. Additional cases execute both processors with publication between stores or getter reads, then original history/stability instructions with isolated mode8/history RAM.'
report['mapping_note']='Verifies software lane/width/order under the candidate shared bus; physical MSD309 wiring remains unverified.'
report['post_stability_scope_note']='Nine isolated original getter/history/continuation cases use mode0, patchflags0 and synthetic samples; observe actual COMB/gain stores and stop before co-channel getter. Not connected configuration or a supplied co-channel result.'
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: original timing/getter/stability cases plus 128 two-processor publication interleavings; no TV measurement or event generated')
