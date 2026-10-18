"""Original clear packets and opt-in plain fills; isolated guest RAM, no connected OSD."""
import argparse,hashlib,json,os,subprocess,tempfile,zlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('firmware','probe','report'):p.add_argument('--'+name,required=True)
a=p.parse_args();stock=Path(a.firmware).read_bytes();sha=hashlib.sha256(stock).hexdigest()
assert sha=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
image=zlib.decompress(stock[0x23880:0x16124e],-15)
with tempfile.TemporaryDirectory(prefix='manta-original-clear-') as directory:
    target=Path(directory)/'original-main.bin';target.write_bytes(image)
    first=subprocess.check_output([a.probe,str(target)],text=True,timeout=30)
    assert subprocess.check_output([a.probe,str(target)],text=True,timeout=30)==first
    result=json.loads(first)
    assert result['result']=='PASS' and result['cases']==5
    assert result['raster_requests_rejected']==3 and result['zero_size_returns']==2
    assert result['raster_command']==0x30 and result['original_instructions']==4352
    assert result['original_format_capability_cases']==4 and result['original_destination_binding_cases']==8
    assert result['original_destination_binding_instructions']>4000
    assert result['isolated_original_GOP_allocations']==3 and result['original_GOP_rejections']==6
    assert result['original_allocated_record_bindings']==2 and result['original_GOP_allocation_instructions']>12000
    assert result['original_allocated_clear_requests_rejected']==2
    assert not result['connected_framebuffer_allocated'] and not result['GOP_window_enabled']
    assert result['pixels_generated'] and not result['legacy_guard_cases_pixels_generated']
    assert not result['framebuffer_allocated'] and not result['connected_pixels_generated']
    assert result['isolated_original_plain_fills']==2 and result['isolated_plain_fill_pixels']==24
    assert result['original_plain_fill_instructions']==3517
    assert result['original_BitBlt_mode_cases']==4 and result['original_BitBlt_stretch_errors']==1
    assert result['original_BitBlt_mode_instructions']==10087
    assert result['original_alpha1555_register_cases']==12 and result['original_alpha1555_instructions']==2292
    assert result['isolated_original_plain_BitBlts']==2 and result['original_plain_BitBlt_instructions']==6515
    assert result['isolated_plain_BitBlt_pixels']==12 and not result['OSD3_format_C_BitBlt_supported']
    assert result['original_source_key_cases']==8 and result['original_source_key_instructions']==3492
    assert result['original_dither_cases']==12 and result['original_dither_instructions']==2368
    assert result['original_nearest_selector_cases']==6 and result['original_nearest_selector_instructions']==176
    for bpp,name in ((2,'RGB565'),(4,'ARGB8888')):
        memory=bytearray([0xa5])*4096
        for i in range(96):memory[0x800+i]=(i*37+11)&255
        for y in range(1,3):
            memory[y*24+2*bpp:y*24+5*bpp]=memory[0x800+y*24+bpp:0x800+y*24+4*bpp]
        assert result[f'plain_BitBlt_{name}_RAM_sha256']==hashlib.sha256(memory).hexdigest()
    assert result['original_scale_cases']==800 and result['original_scale_instructions']==496964
    assert result['original_scale_results_sha256']=='a904ebd994796325a1591ddc960dbe4e57261c300b00a728513f497037f01b0c'
    assert not result['hardware_filter_rounding_verified']
    damaged=bytearray(image);damaged[0x8049e158-0x803b3980]^=1;target.write_bytes(damaged)
    rejected=subprocess.run([a.probe,str(target)],capture_output=True,text=True,timeout=30)
    assert rejected.returncode==1 and 'original main image mismatch' in rejected.stderr
    target.write_bytes(image)
    env=os.environ.copy();env['PATH']=str(Path(os.environ['SystemRoot'])/'System32')
    assert subprocess.check_output([str(Path(a.probe).resolve()),str(target)],text=True,env=env,timeout=30)==first
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==sha
report={'result':'PASS','scope':'isolated original clear instructions with synthetic context; two supported plain raster effects in guest RAM, no connected TV frame',
        'original_functions':result,'corrupted_main_rejected':True,'clean_PATH_probe_pass':True,
        'probe_sha256':hashlib.sha256(Path(a.probe).read_bytes()).hexdigest(),'firmware_sha256':sha}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: original clear30 packets, isolated RGB565/ARGB8888 RAM effects, preserved padding and guarded unsupported OSD raster')
