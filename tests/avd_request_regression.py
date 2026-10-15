"""Original MIPS host-assisted AVD transactions, confined to synthetic isolated fixtures."""
import argparse, hashlib, json, subprocess, tempfile, zlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('firmware','probe','report'):p.add_argument('--'+name,required=True)
a=p.parse_args();stock=Path(a.firmware).read_bytes()
digest=hashlib.sha256(stock).hexdigest()
assert digest=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
with tempfile.TemporaryDirectory(prefix='manta-avd-host-request-') as directory:
    path=Path(directory)/'original-main.bin';path.write_bytes(zlib.decompress(stock[0x23880:0x16124e],-15))
    result=subprocess.run([str(Path(a.probe).resolve()),str(path)],capture_output=True,text=True)
    assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
    report=json.loads(result.stdout)
    assert report['result']=='PASS' and report['request_cases']==1152 and report['unknown_request_stops']==4
    assert report['partial_command_field_cases']==4 and report['maximum_original_instructions']==37
    assert report['TV_request_supplied'] is False
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==digest
report['probe_sha256']=hashlib.sha256(Path(a.probe).read_bytes()).hexdigest()
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: 1152 original AVD host-service transactions; unknown command/bank fields reject; no TV reply injected')
