"""Plain BitBlt must preserve strict unknowns and never enable scaled OSD rendering."""
import argparse,hashlib,json,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for n in ('firmware','emulator','report'):p.add_argument('--'+n,required=True)
a=p.parse_args();firmware=Path(a.firmware).resolve();exe=Path(a.emulator).resolve()
sha=hashlib.sha256(firmware.read_bytes()).hexdigest()
assert sha=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
with tempfile.TemporaryDirectory(prefix='manta-ge-copy-option-') as directory:
    states=[]
    for enabled in (False,True):
        summary=Path(directory)/f'{enabled}.json'
        command=[str(exe),'--firmware',str(firmware),'--headless','--candidate-ge','--summary',str(summary)]
        if enabled:command+=['--candidate-ge-bitblt']
        result=subprocess.run(command,capture_output=True,text=True,timeout=30)
        assert result.returncode==2,(result.returncode,result.stderr)
        state=json.loads(summary.read_text())
        if enabled:
            assert state.pop('candidate_ge_bitblt') is True
            assert state.pop('ge_bitblts_retired')==state.pop('ge_bitblt_pixels_written')==0
        states.append(state)
    assert states[0]==states[1],'plain copy must not establish reset, power or analog input state'
    bad=subprocess.run([str(exe),'--firmware',str(firmware),'--headless','--candidate-ge-bitblt'],capture_output=True,text=True,timeout=30)
    assert bad.returncode==1 and 'requires candidate GE queues' in bad.stderr
    env=os.environ.copy();env['PATH']=str(Path(os.environ['SystemRoot'])/'System32')
    help_text=subprocess.check_output([str(exe),'--help'],text=True,env=env,timeout=15)
    assert '--candidate-ge-bitblt' in help_text and 'stretch/format C/alpha remain guarded' in help_text
assert hashlib.sha256(firmware.read_bytes()).hexdigest()==sha
Path(a.report).write_text(json.dumps({'result':'PASS','strict_state_and_unknown_parity':True,
    'missing_queue_rejected':True,'clean_PATH_help_pass':True,'connected_pixels':False,
    'firmware_sha256':sha,'emulator_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()},indent=2)+'\n')
print('PASS: plain BitBlt option, queue prerequisite, strict unknown parity and explicit unsupported OSD modes')
