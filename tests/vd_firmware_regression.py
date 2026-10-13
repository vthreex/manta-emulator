"""Execute the original MCU blob through actual register aliases and IRQ handlers."""
import argparse,hashlib,json,subprocess,tempfile,zlib
from pathlib import Path
p=argparse.ArgumentParser()
for name in ('firmware','probe','report'):p.add_argument('--'+name,required=True)
a=p.parse_args();stock=Path(a.firmware).read_bytes()
assert len(stock)==0x800010 and hashlib.sha256(stock).hexdigest()=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
main=zlib.decompress(stock[0x23880:0x16124e],-15)
assert hashlib.sha256(main).hexdigest()=='149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236'
blob=main[0x806a4a04-0x803b3980:0x806aa00e-0x803b3980]
with tempfile.TemporaryDirectory(prefix='manta-original-mcu-') as directory:
    image=Path(directory)/'original-mcu.bin';image.write_bytes(blob)
    r=subprocess.run([a.probe,str(image)],text=True,capture_output=True)
    assert r.returncode==0,(r.returncode,r.stdout,r.stderr)
    report=json.loads(r.stdout)
    assert report['guest_generated_FF_replies']==2 and report['original_startup_idle_writes']==1
    assert report['original_instructions']>26000 and report['original_irq_entries_before_failure']>=2
    assert report['unknown_target_stopped_without_ack']
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==hashlib.sha256(stock).hexdigest()
report['probe_sha256']=hashlib.sha256(Path(a.probe).read_bytes()).hexdigest()
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: actual original MCU startup, two original IRQ-generated replies, shared data readback, unsupported transaction has no completion')
