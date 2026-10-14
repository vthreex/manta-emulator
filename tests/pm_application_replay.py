"""Validate/replay the explicitly diagnostic non-resume application path.

No PM reset byte, ready response or guest PC is supplied. Requires a private
original firmware and complete recorded event trace; does not establish cold
boot hardware accuracy or JPEG/GOP display output.
"""
import argparse, hashlib, json, subprocess, sys, tempfile
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
for name in ('firmware','emulator','reference-summary','reference-trace','reference-coverage','reference-history','report'):
    p.add_argument('--'+name,required=True)
p.add_argument('--validate-only',action='store_true')
a=p.parse_args()
stock=Path(a.firmware).read_bytes()
stock_sha=hashlib.sha256(stock).hexdigest()
assert stock_sha=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
ref=json.loads(Path(a.reference_summary).read_text())
assert ref['diagnostic_pm_nonresume_branch'] and not ref['PM_cold_boot_hardware_verified']
assert ref['pm_activity_invalid_samples']==ref['pm_activity_constrained_predicates']==ref['pm_activity_original_branches']==1
assert ref['pm_activity_byte_known']==ref['pm51_marker_writes']==0
assert ref['pm51_instructions']==3474 and ref['pm51_pc']=='0x574f'
assert ref['runtime_entry_reached'] and ref['instruction_dispatches']>1789567273
resource=stock[0x2d89d8:0x2e3c58]
assert len(resource)==45696 and resource[:2]==b'\xff\xd8'
assert hashlib.sha256(resource).hexdigest()=='92b1ccd98f3a02f7dd1a7e5d9a6b0cd73e18e07bdeee9531bf3fbaaf66d2b700'
events=[];trace_sha=hashlib.sha256();count=0
selected=(b'diagnostic_pm_activity_',b'diagnostic_pm_nonresume_',b'bdma_flash_copy',b'pm51_marker_write')
with Path(a.reference_trace).open('rb') as f:
    for line in f:
        trace_sha.update(line);count+=1
        if any(tag in line for tag in selected):events.append(json.loads(line))
kinds=[e['kind'] for e in events]
assert 'pm51_marker_write' not in kinds
for kind in ('diagnostic_pm_activity_invalid_sample','diagnostic_pm_nonresume_predicate','diagnostic_pm_nonresume_branch',
             'bdma_flash_copy','bdma_flash_copy_complete'):
    assert kinds.count(kind)==1,(kind,kinds)
copy=next(e for e in events if e['kind']=='bdma_flash_copy')
done=next(e for e in events if e['kind']=='bdma_flash_copy_complete')
assert copy['pc']=='0x80547350' and int(copy['address'],16)==0x2d89d8 and copy['width']==45696
assert int(copy['value'],16)==0x03230000
assert int(done['address'],16)==0x03230000 and done['width']==45696 and done['tick']>=copy['tick']+64
replay_result='NOT RUN (recorded evidence only)'
if not a.validate_only:
    launcher=Path(__file__).resolve().parents[1]/'tools/run_pm_boot_checkpoint.py'
    with tempfile.TemporaryDirectory(prefix='manta-pm-application-replay-') as directory:
        root=Path(directory);summary=root/'replay.json';cov=root/'replay.coverage';history=root/'replay-pm.jsonl'
        command=json.loads(subprocess.check_output([sys.executable,str(launcher),
            '--firmware',str(Path(a.firmware).resolve()),'--emulator',str(Path(a.emulator).resolve()),
            '--exploratory-pm-power-good','--candidate-pm51-uart','--explore-pm-nonresume-branch',
            '--summary',str(summary),'--coverage',str(cov),'--pm51-history',str(history),'--print-command'],text=True))
        result=subprocess.run(command+['--replay-trace',str(Path(a.reference_trace).resolve())],capture_output=True,text=True)
        assert result.returncode==2,(result.returncode,result.stderr)
        assert json.loads(summary.read_text())==ref,'all original CPUs/peripherals and invalid-state observations must replay'
        assert cov.read_bytes()==Path(a.reference_coverage).read_bytes(),'original MIPS coverage must match'
        assert history.read_bytes()==Path(a.reference_history).read_bytes(),'original PM history must match'
        replay_result='PASS: every event, full summary, coverage and PM history identical'
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==stock_sha
report={'result':'PASS','scope':'diagnostic non-resume predicate assumption; NOT verified cold boot or display',
        'full_new_PM_application_replay':replay_result,'trace_events':count,'trace_sha256':trace_sha.hexdigest(),
        'emulator_sha256':hashlib.sha256(Path(a.emulator).read_bytes()).hexdigest(),
        'stock_sha256':stock_sha,'original_JPEG_transfer_sha256':hashlib.sha256(resource).hexdigest(),
        'original_JPEG_encoded_dimensions':[1940,1080],'guest_transfer_destination':'0x03230000',
        'observed_events':events,'instruction_dispatches':ref['instruction_dispatches'],
        'OpenRISC_instructions':ref['or1k_instructions'],'PM_marker_generated':False,
        'ZUI_entries':ref.get('osd_routine_entries',{}).get('0x804e0d48',0),
        'authentic_display_demonstrated':ref['osd_demonstrated']}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: original diagnostic application branch and flash-to-MIU0 JPEG transfer; '+replay_result+'; no invented PM state/display')
