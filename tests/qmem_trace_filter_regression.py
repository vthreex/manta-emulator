"""Compare genuine dual-CPU boot with QMEM log records on/off.

Checks all reported machine fields, coverage and original PM history. This is
not a full-RAM snapshot or a claim of calibrated CPI/hardware accuracy.
"""
import argparse,hashlib,json,subprocess,sys,tempfile,time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--filter-idle-polls',action='store_true')
p.add_argument('--filter-internal-reads',action='store_true')
for name in ('firmware','emulator','reference-summary','reference-coverage','reference-history','report'):
    p.add_argument('--'+name,required=True)
a=p.parse_args();ref=json.loads(Path(a.reference_summary).read_text())
assert ref['reason']=='instruction budget exhausted' and ref['or1k_instructions']>1000000
launcher=Path(__file__).resolve().parents[1]/'tools/run_pm_boot_checkpoint.py'
with tempfile.TemporaryDirectory(prefix='manta-qmem-logging-') as directory:
    root=Path(directory);summary=root/'quiet.json';cov=root/'quiet.coverage';history=root/'quiet-pm.jsonl'
    command=json.loads(subprocess.check_output([sys.executable,str(launcher),
        '--firmware',a.firmware,'--emulator',a.emulator,'--exploratory-pm-power-good',
        '--candidate-pm51-uart','--explore-pm-nonresume-branch','--copro-probes','--or1k-no-qmem-trace',
        '--max-instructions',str(ref['instruction_dispatches']),'--max-or1k-instructions',str(ref['or1k_budget']),
        '--exploratory-cpi',str(ref.get('exploratory_average_cpi',1)),
        '--summary',str(summary),'--coverage',str(cov),'--pm51-history',str(history),'--print-command'],text=True))
    if a.filter_idle_polls:command+=['--or1k-idle-poll-log-changes']
    if a.filter_internal_reads:command+=['--or1k-register-log-changes']
    start=time.perf_counter();r=subprocess.run(command,capture_output=True,text=True);elapsed=time.perf_counter()-start
    if summary.exists():
        Path(a.report).with_suffix('.actual-summary.json').write_bytes(summary.read_bytes())
        Path(a.report).with_suffix('.actual.coverage').write_bytes(cov.read_bytes())
        Path(a.report).with_suffix('.actual-pm.jsonl').write_bytes(history.read_bytes())
    assert r.returncode==2,(r.returncode,r.stderr)
    got=json.loads(summary.read_text());suppressed=got.pop('or1k_qmem_trace_records_suppressed')
    assert got.pop('or1k_qmem_trace_disabled') and 0<suppressed<=got['or1k_data_writes']
    idle_suppressed=0
    if a.filter_idle_polls:
        assert got.pop('or1k_idle_poll_log_changes_only')
        idle_suppressed=got.pop('or1k_idle_poll_records_suppressed')
        assert 0<idle_suppressed<=got['or1k_data_reads']
    internal_suppressed=0
    if a.filter_internal_reads:
        assert got.pop('or1k_stored_read_log_changes_only')
        internal_suppressed=got.pop('or1k_stored_read_records_suppressed')
        assert 0<internal_suppressed<=got['or1k_data_reads']
    assert got.pop('copro_original_program_match') and got['copro_guest_loaded_sha256']==ref['copro_guest_loaded_sha256']
    # Generic caller inspections became positive resource61 SHA matches. Keep
    # the actual RAM SHA comparison above; exclude only this host-only counter.
    matched_observations=got.pop('copro_loader_inspections');assert matched_observations>=1
    compare=dict(ref);reference_inspections=compare.pop('copro_loader_inspections');compare.pop('copro_original_program_match',None)
    assert got==compare,{k:[compare.get(k),got.get(k)] for k in set(compare)|set(got) if compare.get(k)!=got.get(k)}
    assert cov.read_bytes()==Path(a.reference_coverage).read_bytes(),'original executed PC coverage changed'
    assert history.read_bytes()==Path(a.reference_history).read_bytes(),'original PM execution changed'
    report={'result':'PASS','scope':__doc__.strip(),'MIPS':got['instruction_dispatches'],
        'original_OpenRISC':got['or1k_instructions'],'QMEM_log_records_suppressed':suppressed,
        'duplicate_known_idle_poll_records_suppressed':idle_suppressed,
        'unchanged_internal_value_mask_records_suppressed':internal_suppressed,
        'all_reported_machine_fields_equal':True,'coverage_and_PM_history_equal':True,
        'TSP_core_QMEM_state_sha256':got['or1k_core_QMEM_state_sha256'],
        'resource61_positive_SHA_observations':matched_observations,'reference_generic_observer_count':reference_inspections,
        'elapsed_seconds_under_host_load':elapsed,'timing_hardware_verified':False,
        'emulator_sha256':hashlib.sha256(Path(a.emulator).read_bytes()).hexdigest(),
        'summary':json.loads(summary.read_text())}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: QMEM log filtering preserves original CPU execution, all reported state, coverage and PM history')
