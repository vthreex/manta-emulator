"""Replay every event in the connected original PM powered/UART boot prefix.

This covers the whole newly reached PM execution, not later TV startup.
"""
import argparse, hashlib, json, subprocess, sys, tempfile
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
for name in ('firmware','emulator','report'):p.add_argument('--'+name,required=True)
a=p.parse_args()
fw=Path(a.firmware).resolve();stock_sha=hashlib.sha256(fw.read_bytes()).hexdigest()
assert stock_sha=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
launcher=Path(__file__).resolve().parents[1]/'tools/run_pm_boot_checkpoint.py'
with tempfile.TemporaryDirectory(prefix='manta-pm-powered-prefix-') as directory:
    root=Path(directory);summaries=[];histories=[];coverage=[]
    trace=root/'prefix.jsonl'
    for replay in (False,True):
        tag='replay' if replay else 'cold'
        summary=root/(tag+'.json');history=root/(tag+'-pm.jsonl');cov=root/(tag+'.coverage')
        command=json.loads(subprocess.check_output([sys.executable,str(launcher),
            '--firmware',str(fw),'--emulator',str(Path(a.emulator).resolve()),
            '--exploratory-pm-power-good','--candidate-pm51-uart','--max-instructions','300000',
            '--summary',str(summary),'--coverage',str(cov),'--pm51-history',str(history),
            '--print-command'],text=True))
        command+=['--replay-trace' if replay else '--record-trace',str(trace)]
        result=subprocess.run(command,capture_output=True,text=True)
        assert result.returncode==2,(result.returncode,result.stderr)
        j=json.loads(summary.read_text())
        assert j['instruction_dispatches']==300000
        assert j['pm51_instructions']==3474 and j['pm51_pc']=='0x574f'
        assert j['pm51_marker_writes']==j['pm51_interrupt_entries']==0
        # 629b0453... was the hash before commit 195ac82 (MCU stack provenance became part of the state bytes). That commit
        # recorded the intentional change in evidence/mcu-stack-validation.json; the expectation is read from there so it cannot
        # drift silently again. PM51 instruction count and PC (asserted above) are unchanged.
        change=json.loads((Path(__file__).resolve().parents[1]/'evidence/mcu-stack-validation.json').read_text())['prior_same_mode_comparison']['changed_summary_fields']['pm51_state_sha256']
        assert change['before']=='629b0453a0d986f1aa838941644d5f8b43030d624453f2dc4e8673f97b3011d5'
        assert j['pm51_state_sha256']==change['after']=='32c9489e015cfc475fa13aa1bad537ba4eb9625cff63102d0b80cb6f738d0b68'
        assert not j['osd_demonstrated']
        summaries.append(j);histories.append(history.read_bytes());coverage.append(cov.read_bytes())
    assert summaries[0]==summaries[1] and histories[0]==histories[1] and coverage[0]==coverage[1]
    events=[json.loads(line) for line in trace.read_text().splitlines()]
    kinds=[e['kind'] for e in events]
    assert kinds.count('pm51_uart_config_write')==kinds.count('pm51_uart_rx_gate_write')==1
    assert kinds.count('pm51_gpio_direction_write')==1
    assert kinds.count('pm51_marker_write')==0
    report={'result':'PASS','scope':'all event replay in connected original MIPS/PM boot prefix; not full powered TV boot replay',
        'MIPS_dispatches':300000,'original_PM_instructions':3474,'PM_pc':'0x574f',
        'trace_events':len(events),'trace_sha256':hashlib.sha256(trace.read_bytes()).hexdigest(),
        'PM_history_sha256':hashlib.sha256(histories[0]).hexdigest(),
        'UART_configuration_writes':2,'GPIO_direction_writes':1,'activity_marker_writes':0,
        'full_summary_coverage_and_history_equal':True,'stock_sha256':stock_sha}
assert hashlib.sha256(fw.read_bytes()).hexdigest()==stock_sha
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: connected original PM powered/UART prefix event replay, complete summaries, histories and coverage match')
