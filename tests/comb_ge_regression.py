"""Full original-guest replay and assertions for COMB/GOP-table/GE checkpoints.
The reference files must come from cold runs of this unchanged firmware.
No successful TV boot or graphics completion is inferred from these checks.
"""
import argparse, hashlib, json, subprocess, tempfile
from pathlib import Path

p=argparse.ArgumentParser()
for option in ('firmware','emulator','reference-summary','reference-trace','bank-guard-summary','report'):
 p.add_argument('--'+option,required=True)
a=p.parse_args()
stock=hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()
assert stock=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
ref=json.loads(Path(a.reference_summary).read_text())
assert ref['mode']=='firmware from reset flash' and ref['runtime_entry_reached']
assert ref['guest_decompressed_sha256']=='149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236'
assert ref['stop_pc']=='0x8052624c' and ref['stop_address']=='0x1f205004'
assert ref['instruction_dispatches']==656427765 and ref['guest_exception_vector_entries']==ref['guest_software_tick']==1813
assert ref['comb_paired_restores']==1 and not ref['comb_paired_restore_pending']
assert ref['semaphore_acquisitions']==4 and ref['semaphore_releases']==3
assert ref['scaler_initializer_returns']==ref['dlc_initializer_returns']==1
assert ref['gamma_ram_commits']==768 and ref['gop_aperture_events']==11
assert ref['ge_candidate_aperture_events']==1 and ref['ge_blocked_aperture_events']==1
assert not ref['gop_initializer_entered'] and not ref['osd_demonstrated']
assert sum(e['origin'] in ('0x1f200a38','0x1f200a3c') for e in ref['invalid_observation_bytes'])==4
guard=json.loads(Path(a.bank_guard_summary).read_text())
assert guard['stop_pc']=='0x803c1f70' and 'unknown preserved register bit' in guard['reason']
assert guard['comb_paired_restores']==1 and guard['exploratory_gop_initial_bank']==-1
assert guard['instruction_dispatches']==654548913 and not guard['gop_initializer_entered']
events=[]
with Path(a.reference_trace).open() as f:
 for line in f:
  e=json.loads(line)
  if e['kind'] in ('exploratory_comb_restore_begin','exploratory_comb_restore_end',
     'deferred_gop_selector_snapshot','deferred_gop_selector_mask','deferred_ge_context_sample',
     'semaphore_read','semaphore_write','gop_initializer','gop_window_descriptor') or (
      e['kind']=='write' and e['address'] in ('0x1f203e65','0x1f203ffc')):
   events.append(e)
kinds=[e['kind'] for e in events]
assert kinds.count('exploratory_comb_restore_begin')==kinds.count('exploratory_comb_restore_end')==1
assert next(e['tick'] for e in events if e['kind']=='exploratory_comb_restore_end')-next(
 e['tick'] for e in events if e['kind']=='exploratory_comb_restore_begin')==20
assert kinds.count('deferred_ge_context_sample')==1 and 'gop_initializer' not in kinds and 'gop_window_descriptor' not in kinds
assert len([e for e in events if e['kind']=='write' and e['address']=='0x1f203e65'])==2
flags=['--firmware',a.firmware,'--headless','--symbolic-init','--candidate-miu','--candidate-bdma',
 '--candidate-uart','--gpio-i2c','--defer-diagnostic-reads','--virtual-rtc','--cp0-timer',
 '--candidate-utmi','--candidate-spi','--exploratory-spi-input','pull-up','--candidate-chip-id',
 '--exploratory-sc-bank','0','--candidate-gamma','--exploratory-gamma-enable','0',
 '--exploratory-display-clock','148500000','--exploratory-subwindow-enable','0',
 '--exploratory-dlc-pending','0','--exploratory-dlc-timeout','--exploratory-comb-bank','0',
 '--exploratory-comb-paired-restore','--exploratory-gop-bank','0',
 '--candidate-semaphore','--exploratory-semaphore-idle','--max-instructions','1200000000']
with tempfile.TemporaryDirectory(prefix='manta-comb-replay-') as tmp:
 tmp=Path(tmp);out=tmp/'summary.json'
 r=subprocess.run([a.emulator]+flags+['--replay-trace',a.reference_trace,'--summary',str(out)],capture_output=True,text=True)
 assert r.returncode==2,(r.returncode,r.stderr)
 replay=json.loads(out.read_text());assert replay==ref,'original boot/IRQ/MMIO sequence must replay identically'
 assert '@@@ Hello T4' in r.stdout and 'USB_Init_Configure done...' in r.stdout
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==stock
Path(a.report).write_text(json.dumps({'result':'PASS','original_sha256':stock,
 'emulator_sha256':hashlib.sha256(Path(a.emulator).read_bytes()).hexdigest(),
 'full_farthest_trace_replay':'PASS','checkpoint_reference_and_guard_assertions':'PASS',
 'assumptions_not_hardware_validation':True,'selected_events':events,'replay':replay},indent=2)+'\n')
print('PASS: original COMB transaction/restore, unknown-bank decision guard, actual GOP table writes, semaphore ownership, GE context provenance and full deterministic replay; no OSD claimed')
