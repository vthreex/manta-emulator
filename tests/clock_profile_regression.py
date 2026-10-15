"""Real reset/PM instruction execution under uncalibrated CPI hypotheses.

Every event, summary, coverage and PM instruction history must replay. This is
not a calibrated performance model, full application boot or hardware proof.
"""
import argparse, hashlib, json, subprocess, sys, tempfile
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
for name in ('firmware','emulator','report'):p.add_argument('--'+name,required=True)
a=p.parse_args()
fw=Path(a.firmware).resolve();emu=Path(a.emulator).resolve()
stock_sha=hashlib.sha256(fw.read_bytes()).hexdigest()
assert stock_sha=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
launcher=Path(__file__).resolve().parents[1]/'tools/run_pm_boot_checkpoint.py'
results=[]
with tempfile.TemporaryDirectory(prefix='manta-cycle-profiles-') as directory:
    root=Path(directory)
    for cpi in (1,2,4,8,16):
        trace=root/f'{cpi}.jsonl';observations=[]
        for replay in (False,True):
            tag=f'{cpi}-{replay}';summary=root/(tag+'.json');cov=root/(tag+'.coverage');history=root/(tag+'-pm.jsonl')
            command=json.loads(subprocess.check_output([sys.executable,str(launcher),
                '--firmware',str(fw),'--emulator',str(emu),'--max-instructions','300000',
                '--exploratory-pm-power-good','--candidate-pm51-uart','--exploratory-cpi',str(cpi),
                '--summary',str(summary),'--coverage',str(cov),'--pm51-history',str(history),'--print-command'],text=True))
            r=subprocess.run(command+['--replay-trace' if replay else '--record-trace',str(trace)],capture_output=True,text=True)
            assert r.returncode==2,(cpi,replay,r.returncode,r.stderr)
            s=json.loads(summary.read_text())
            assert s['instruction_dispatches']==300000 and s['pm51_instructions']==3474 and s['pm51_pc']=='0x574f'
            assert s['pm51_marker_writes']==s['pm51_interrupt_entries']==0 and not s['osd_demonstrated']
            if cpi!=1:
                assert s['exploratory_average_cpi']==cpi and s['virtual_cpu_cycles']==300000*cpi
                assert not s['cpu_cycle_model_hardware_verified']
            else:assert 'exploratory_average_cpi' not in s
            observations.append((s,cov.read_bytes(),history.read_bytes()))
        assert observations[0]==observations[1]
        events=[json.loads(line) for line in trace.read_text().splitlines()]
        for e in events:
            if cpi==1:assert 'virtual_cpu_cycles' not in e
            else:assert e['virtual_cpu_cycles']==e['tick']*cpi
        results.append({'cpi':cpi,'events':len(events),'MIPS':300000,'PM':3474,
                        'full_prefix_replay':'PASS','trace_sha256':hashlib.sha256(trace.read_bytes()).hexdigest()})
    for bad in (3,4):
        r=subprocess.run([str(emu),'--firmware',str(fw),'--headless','--exploratory-cpi',str(bad)],capture_output=True,text=True)
        assert r.returncode==1 and ('CPI' in r.stderr or 'CPI' in r.stdout)
    for incomplete in ([],['--exploratory-display-clock','148500000'],['--symbolic-init','--defer-diagnostic-reads']):
        r=subprocess.run([str(emu),'--firmware',str(fw),'--headless','--exploratory-gop-vsync-latch']+incomplete,
                         capture_output=True,text=True)
        assert r.returncode==1 and 'GOP sync-latch hypothesis requires' in r.stderr
    for invalid_policy in (
        ['--opaque-read-policy','missing.tsv'],
        ['--opaque-read-policy','missing.tsv','--symbolic-init'],
        ['--opaque-policy-wait-ms','1'],
        ['--opaque-read-policy','missing.tsv','--symbolic-init','--defer-diagnostic-reads','--opaque-policy-wait-ms','4294967296']):
        r=subprocess.run([str(emu),'--firmware',str(fw),'--headless']+invalid_policy,capture_output=True,text=True)
        assert r.returncode==1 and 'opaque observation policy requires' in r.stderr
    for incomplete in ([],['--symbolic-init','--defer-diagnostic-reads']):
        r=subprocess.run([str(emu),'--firmware',str(fw),'--headless','--exploratory-avd-no-hsync']+incomplete,capture_output=True,text=True)
        assert r.returncode==1 and 'AVD absent-HSYNC hypothesis requires' in r.stderr
    for token in ('0','256','4294967296','1junk','-1'):
        r=subprocess.run([str(emu),'--firmware',str(fw),'--headless','--exploratory-vif-chip-revision-byte',token],capture_output=True,text=True)
        assert r.returncode==1 and ('VIF chip revision' in r.stderr), (token,r.returncode,r.stderr)
    for incomplete in ([],['--symbolic-init','--defer-diagnostic-reads']):
        r=subprocess.run([str(emu),'--firmware',str(fw),'--headless','--exploratory-ir-capture-present']+incomplete,capture_output=True,text=True)
        assert r.returncode==1 and 'IR capture activity hypothesis requires' in r.stderr
assert hashlib.sha256(fw.read_bytes()).hexdigest()==stock_sha
Path(a.report).write_text(json.dumps({'result':'PASS','hardware_timing_verified':False,
    'scope':'original reset/PM prefix under explicit clock/input hypotheses; no verified cold boot or fabricated completion',
    'profiles':results,'strict_and_invalid_profile_rejections':'PASS','unqualified_GOP_hypothesis_rejections':'PASS',
    'strict_and_invalid_opaque_policy_rejections':'PASS',
    'strict_and_invalid_VIF_revision_rejections':'PASS',
    'unqualified_IR_capture_activity_rejections':'PASS',
    'emulator_sha256':hashlib.sha256(emu.read_bytes()).hexdigest(),'stock_sha256':stock_sha},indent=2)+'\n')
print('PASS: five cycle profiles replay every prefix event; strict/invalid configurations reject; no hardware timing claim')
