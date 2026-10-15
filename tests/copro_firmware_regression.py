"""Original resource lookup and explicitly conditional CP startup, not TV boot."""
import argparse, hashlib, importlib.util, json, subprocess, sys, tempfile, zlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('firmware','probe','report'):p.add_argument('--'+name,required=True)
a=p.parse_args()
tool=Path(__file__).resolve().parents[1]/'tools/firmware_resources.py'
spec=importlib.util.spec_from_file_location('resources',tool);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
stock=Path(a.firmware).read_bytes();manifest,payloads=module.recover(stock)
cp=next(r for r in manifest['resources'] if r['id']=='0x61')
assert cp['source_flash']=='0x242910' and cp['decoded_bytes']==1253416
assert cp['decoded_sha256']=='bc00e532ab47b35e2f3a8be0b59a40b7ae80111dd366b6ceb741cdb74ca95ae1'
osd=next(r for r in manifest['resources'] if r['id']=='0x826')
assert osd['decoded_sha256']=='769bb8b6fc9eda743de6cefc8b3bc10c60d9ddb867b4cc37fef1ef3badf54f4f'
osd_literals={'NO SIGNAL':0x21356,'No Signal':0x23e42,'SCART':0x210dc,'HDMI1':0x2119c}
for text,offset in osd_literals.items():
    encoded=text.encode('utf-16-le')
    assert offset%2==0 and payloads[0x826][offset:offset+len(encoded)]==encoded
results=[]
with tempfile.TemporaryDirectory(prefix='manta-original-copro-') as directory:
    root=Path(directory);runtime=root/'runtime.bin';program=root/'program.bin'
    runtime.write_bytes(stock[0x20080:0x23880]+zlib.decompress(stock[0x23880:0x16124e],-15))
    program.write_bytes(payloads[0x61])
    for flags,instructions,pc,address,pending in [([],1,0x104,0,0),
        (['--assume-reset-r0-zero'],5,0x114,0xa0201fd4,0),
        (['--assume-reset-r0-zero','--defer-unmodeled-startup-writes'],9,0x124,0xa0202400,2)]:
        command=[a.probe,a.firmware,str(runtime),str(program),'--entry-100-hypothesis']+flags
        r=subprocess.run(command,capture_output=True,text=True);assert r.returncode==0,(r.returncode,r.stderr)
        result=json.loads(r.stdout)
        assert result['original_MIPS_mailbox_framing_verified'] and result['mailbox_class']==43
        assert result['cleanup_command_index']==18 and result['mailbox_parameter_count']==0 and result['original_busy_rejection']==9
        assert result['original_MIPS_resource_lookup'] and result['original_CP_instructions']==instructions
        assert result['stop_pc']==pc and result['blocked_address']==address
        assert result['pending_unimplemented_writes']==pending and result['peripheral_effects_applied']==result['ready_replies_generated']==0
        replay=subprocess.run(command,capture_output=True,text=True)
        assert replay.returncode==0 and replay.stdout==r.stdout,'isolated CPU instruction/bus history must replay exactly'
        results.append(result)
    changed=bytearray(program.read_bytes());changed[0x100]^=1;program.write_bytes(changed)
    bad=subprocess.run(command,capture_output=True,text=True)
    assert bad.returncode==1 and 'hash mismatch' in bad.stderr,'crippled original code must fail'
    import os
    private_stock=root/'stock.bin';private_stock.write_bytes(stock)
    alias=root/'alias.bin';os.link(private_stock,alias)
    for target in (private_stock,alias):
        r=subprocess.run([sys.executable,str(tool),'--firmware',str(private_stock),'--manifest',str(target)],capture_output=True,text=True)
        assert r.returncode!=0 and 'output alias' in r.stderr
        assert private_stock.read_bytes()==stock,'resource manifest cannot truncate the immutable input'
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==module.STOCK
Path(a.report).write_text(json.dumps({'result':'PASS','scope':'isolated original lookup/startup; CP not integrated; no JPEG reply/display',
    'resource':cp,'OSD_literal_resource':osd,'OSD_literal_offsets':osd_literals,
    'OSD_literals_are_frame_evidence':False,'experiments':results,'changed_guest_code_rejected':True,
    'probe_sha256':hashlib.sha256(Path(a.probe).read_bytes()).hexdigest()},indent=2)+'\n')
print('PASS: original MIPS directory lookup, original CP startup prefix and replay; unimplemented hardware never acknowledges')
