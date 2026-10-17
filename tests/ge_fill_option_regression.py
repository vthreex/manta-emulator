"""Plain GE fill opt-in must neither resolve strict reset inputs nor draw a TV UI."""
import argparse,hashlib,json,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for n in ('firmware','emulator','report'):p.add_argument('--'+n,required=True)
a=p.parse_args();firmware=Path(a.firmware).resolve();emulator=Path(a.emulator).resolve()
sha=hashlib.sha256(firmware.read_bytes()).hexdigest()
assert sha=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
with tempfile.TemporaryDirectory(prefix='manta-ge-fill-option-') as directory:
    states=[]
    for enabled in (False,True):
        target=Path(directory)/f'{enabled}.json'
        command=[str(emulator),'--firmware',str(firmware),'--headless','--candidate-ge','--summary',str(target)]
        if enabled:command+=['--candidate-ge-rect-fill']
        result=subprocess.run(command,capture_output=True,text=True,timeout=30)
        assert result.returncode==2,(enabled,result.returncode,result.stderr)
        state=json.loads(target.read_text())
        if enabled:
            assert state.pop('candidate_ge_rect_fill') is True
            assert state.pop('ge_rectangles_retired')==state.pop('ge_raster_pixels_written')==0
        states.append(state)
    assert states[0]==states[1],'plain raster support must not invent a POR value or change strict execution'
    bad=subprocess.run([str(emulator),'--firmware',str(firmware),'--headless','--candidate-ge-rect-fill'],
                       capture_output=True,text=True,timeout=30)
    assert bad.returncode==1 and 'requires candidate GE queues' in bad.stderr
    env=os.environ.copy();env['PATH']=str(Path(os.environ['SystemRoot'])/'System32')
    help_text=subprocess.check_output([str(emulator),'--help'],text=True,env=env,timeout=15)
    assert '--candidate-ge-rect-fill' in help_text and 'format C/blending/BitBlt unsupported' in help_text
assert hashlib.sha256(firmware.read_bytes()).hexdigest()==sha
report={'result':'PASS','scope':'strict reset state/unknowns unchanged by opt-in fill support; no connected raster or UI output',
        'strict_summary_identical_except_explicit_extension':True,'missing_GE_queue_model_rejected':True,
        'clean_PATH_help_pass':True,'emulator_sha256':hashlib.sha256(emulator.read_bytes()).hexdigest(),
        'firmware_sha256':sha}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: GE fill option visibility, prerequisite guard and strict-state/unknown parity')
