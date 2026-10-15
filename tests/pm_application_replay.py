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
p.add_argument('--opaque-read-policy',help='Exact final invalid-observation policy used by a policy-enabled reference; replay never waits for edits')
p.add_argument('--max-instructions',type=int,default=2000000000,
               help='Original recorded dispatch budget; may exceed a hardware-stop instruction count')
a=p.parse_args()
stock=Path(a.firmware).read_bytes()
stock_sha=hashlib.sha256(stock).hexdigest()
assert stock_sha=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
ref=json.loads(Path(a.reference_summary).read_text())
assert ref['diagnostic_pm_nonresume_branch'] and not ref['PM_cold_boot_hardware_verified']
assert ref['pm_activity_invalid_samples']==ref['pm_activity_constrained_predicates']==ref['pm_activity_original_branches']==1
assert ref['pm_activity_byte_known']==ref['pm51_marker_writes']==0
assert ref['pm51_instructions']==3474 and ref['pm51_pc']=='0x574f'
assert ref['runtime_entry_reached'] and ref['instruction_dispatches']*ref.get('exploratory_average_cpi',1)>1789567273
resource=stock[0x2d89d8:0x2e3c58]
assert len(resource)==45696 and resource[:2]==b'\xff\xd8'
assert hashlib.sha256(resource).hexdigest()=='92b1ccd98f3a02f7dd1a7e5d9a6b0cd73e18e07bdeee9531bf3fbaaf66d2b700'
events=[];trace_sha=hashlib.sha256();count=0
selected=(b'diagnostic_pm_activity_',b'diagnostic_pm_nonresume_',b'bdma_flash_copy',b'pm51_marker_write',
          b'exploratory_gop_update_request',b'exploratory_gop_register_latch')
with Path(a.reference_trace).open('rb') as f:
    for line in f:
        trace_sha.update(line);count+=1
        if any(tag in line for tag in selected):events.append(json.loads(line))
kinds=[e['kind'] for e in events]
assert 'pm51_marker_write' not in kinds
for kind in ('diagnostic_pm_activity_invalid_sample','diagnostic_pm_nonresume_predicate','diagnostic_pm_nonresume_branch'):
    assert kinds.count(kind)==1,(kind,kinds)
copy=next(e for e in events if e['kind']=='bdma_flash_copy' and int(e['address'],16)==0x2d89d8)
done=next(e for e in events if e['kind']=='bdma_flash_copy_complete' and int(e['address'],16)==0x03230000)
assert copy['pc']=='0x80547350' and int(copy['address'],16)==0x2d89d8 and copy['width']==45696
assert int(copy['value'],16)==0x03230000
assert int(done['address'],16)==0x03230000 and done['width']==45696
assert done.get('virtual_cpu_cycles',done['tick'])>=copy.get('virtual_cpu_cycles',copy['tick'])+64
if ref.get('exploratory_gop_sync_latch'):
    assert ref['gop_update_requests']>0 and ref['gop_register_latches']>0
    assert not ref['gop_latch_clock_domain_hardware_verified']
    assert kinds.count('exploratory_gop_update_request')==ref['gop_update_requests']
    assert kinds.count('exploratory_gop_register_latch')==ref['gop_register_latches']
    coverage=set(Path(a.reference_coverage).read_text().splitlines())
    assert {'0x80469e38','0x804e0d48','0x804e0dbc'}<=coverage,'original application must enter AND return from ZUI'
    assert ref['osd_routine_entries'].get('0x804e0d48')==1
    assert all(e['address']=='0x1f203ffc' for e in events if e['kind'].startswith('exploratory_gop_'))
replay_result='NOT RUN (recorded evidence only)'
added_gop_diagnostics=None
if not a.validate_only:
    launcher=Path(__file__).resolve().parents[1]/'tools/run_pm_boot_checkpoint.py'
    with tempfile.TemporaryDirectory(prefix='manta-pm-application-replay-') as directory:
        root=Path(directory);summary=root/'replay.json';cov=root/'replay.coverage';history=root/'replay-pm.jsonl'
        command=json.loads(subprocess.check_output([sys.executable,str(launcher),
            '--firmware',str(Path(a.firmware).resolve()),'--emulator',str(Path(a.emulator).resolve()),
            '--exploratory-pm-power-good','--candidate-pm51-uart','--explore-pm-nonresume-branch',
            '--max-instructions',str(a.max_instructions),'--max-or1k-instructions',str(ref['or1k_budget']),
            '--exploratory-cpi',str(ref.get('exploratory_average_cpi',1)),
            '--summary',str(summary),'--coverage',str(cov),'--pm51-history',str(history),'--print-command'],text=True))
        if 'copro_loader_inspections' in ref:command+=['--copro-probes']
        if ref.get('or1k_qmem_trace_disabled'):command+=['--or1k-no-qmem-trace']
        if ref.get('or1k_idle_poll_log_changes_only'):command+=['--or1k-idle-poll-log-changes']
        if ref.get('or1k_stored_read_log_changes_only'):command+=['--or1k-register-log-changes']
        if ref.get('exploratory_gop_sync_latch'):command+=['--exploratory-gop-vsync-latch']
        if ref.get('exploratory_avd_no_hsync'):command+=['--exploratory-avd-no-hsync']
        if ref.get('exploratory_ir_capture_activity'):command+=['--exploratory-ir-capture-present']
        if ref.get('exploratory_scart_id_grounded'):command+=['--exploratory-scart-id-grounded']
        if ref.get('candidate_pm_ir_fifo'):command+=['--candidate-pm-ir-fifo']
        if ref.get('exploratory_pm_ir_reset_empty'):command+=['--exploratory-pm-ir-reset-empty']
        if 'exploratory_vif_chip_revision_byte' in ref:command+=['--exploratory-vif-chip-revision-byte',str(ref['exploratory_vif_chip_revision_byte'])]
        if ref.get('opaque_policy_enabled'):
            assert a.opaque_read_policy and Path(a.opaque_read_policy).exists(),'policy-enabled replay needs its final rules'
            command+=['--opaque-read-policy',str(Path(a.opaque_read_policy).resolve())]
        result=subprocess.run(command+['--replay-trace',str(Path(a.reference_trace).resolve())],capture_output=True,text=True)
        if summary.exists():
            Path(a.report).with_suffix('.actual-summary.json').write_bytes(summary.read_bytes())
            Path(a.report).with_suffix('.actual.coverage').write_bytes(cov.read_bytes())
            Path(a.report).with_suffix('.actual-pm.jsonl').write_bytes(history.read_bytes())
        assert result.returncode==2,(result.returncode,result.stderr)
        got=json.loads(summary.read_text())
        if 'gop_configured_descriptors' in got and 'gop_configured_descriptors' not in ref:
            # Decoder-only extension for legacy references. Every old summary
            # field and every recorded event must still match. These exact
            # disabled dimensions/address are independently present in original
            #748M GWIN writes, not a fabricated host framebuffer.
            added_gop_diagnostics=got.pop('gop_configured_descriptors')
            tiny={'enabled':False,'bank':4,'window':0,'miu_relative_address':'0xddd800',
                  'pitch':16,'width':8,'height':8,'format':6,'bpp':16}
            expected=[]
            if ref.get('graphics_probes',{}).get('window_program_entries',0)>=7:
                expected=[dict(active_latch=False,**tiny),dict(active_latch=True,**tiny)]
            assert added_gop_diagnostics==expected,'legacy guest register writes must explain new disabled descriptors exactly'
        assert got==ref,'all original CPUs/peripherals and invalid-state observations must replay'
        assert cov.read_bytes()==Path(a.reference_coverage).read_bytes(),'original MIPS coverage must match'
        assert history.read_bytes()==Path(a.reference_history).read_bytes(),'original PM history must match'
        replay_result='PASS: every event, full summary, coverage and PM history identical' if added_gop_diagnostics is None else \
            'PASS: every event, all pre-existing summary fields, coverage and PM history identical; new disabled-GOP diagnostics verified'
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==stock_sha
report={'result':'PASS','scope':'diagnostic non-resume predicate assumption; NOT verified cold boot or display',
        'full_new_PM_application_replay':replay_result,'trace_events':count,'trace_sha256':trace_sha.hexdigest(),
        'emulator_sha256':hashlib.sha256(Path(a.emulator).read_bytes()).hexdigest(),
        'stock_sha256':stock_sha,'original_JPEG_transfer_sha256':hashlib.sha256(resource).hexdigest(),
        'original_JPEG_encoded_dimensions':[1940,1080],'guest_transfer_destination':'0x03230000',
        'observed_events':events,'instruction_dispatches':ref['instruction_dispatches'],
        'OpenRISC_instructions':ref['or1k_instructions'],'PM_marker_generated':False,
        'exploratory_average_cpi':ref.get('exploratory_average_cpi',1),
        'physical_timing_verified':False,'added_disabled_GOP_diagnostics':added_gop_diagnostics,
        'ZUI_entries':ref.get('osd_routine_entries',{}).get('0x804e0d48',0),
        'authentic_display_demonstrated':ref['osd_demonstrated']}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: original diagnostic application branch and flash-to-MIU0 JPEG transfer; '+replay_result+'; no invented PM state/display')
