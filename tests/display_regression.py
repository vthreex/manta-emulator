"""Original firmware display continuation. Every experimental input is explicit."""
import argparse, hashlib, json, subprocess, tempfile
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--firmware', required=True)
p.add_argument('--emulator', required=True)
p.add_argument('--report', required=True)
a = p.parse_args()
stock = hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()
assert stock == 'd60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
flags = ['--firmware', a.firmware, '--headless', '--symbolic-init', '--candidate-miu',
 '--candidate-bdma', '--candidate-uart', '--gpio-i2c', '--defer-diagnostic-reads',
 '--virtual-rtc', '--cp0-timer', '--candidate-utmi', '--candidate-spi',
 '--exploratory-spi-input', 'pull-up', '--candidate-chip-id', '--exploratory-sc-bank', '0',
 '--candidate-gamma', '--exploratory-gamma-enable', '0', '--max-instructions', '400000000']
results = {}
with tempfile.TemporaryDirectory(prefix='manta-display-') as directory:
 directory = Path(directory)
 def run(name, extra, pc=None, address=None):
  path = directory / (name+'.json')
  r = subprocess.run([a.emulator]+flags+extra+['--summary', str(path)],
      capture_output=True, text=True, check=False)
  assert r.returncode == 2, (name, r.returncode, r.stderr)
  j = json.loads(path.read_text())
  assert j['runtime_entry_reached'] and j['guest_decompressed_sha256'] == '149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236'
  assert '@@@ Hello T4' in r.stdout and 'USB_Init_Configure done...' in r.stdout
  assert j['gamma_ram_commits'] == 768 and not j['gop_initializer_entered'] and j['gop_aperture_events'] == 0 and not j['osd_demonstrated']
  assert sum(e['origin'] in ('0x1f200a38','0x1f200a3c') for e in j['invalid_observation_bytes']) == 4
  if pc: assert j['stop_pc'] == pc, (name, j['stop_pc'], j['reason'])
  if address: assert j['stop_address'] == address, (name, j['stop_address'])
  results[name] = j
  print(name, j['stop_pc'], j['instruction_dispatches'], j['guest_exception_vector_entries'], flush=True)
  return j
 clock = ['--exploratory-display-clock','148500000']
 gate = run('unknown-subwindow', clock, '0x805fe070','0x1f205e40')
 enabled = run('subwindow-enabled', clock+['--exploratory-subwindow-enable','1'])
 assert 'unknown preserved register bit' in enabled['reason'], 'unknown subwindow geometry must stop before affecting a comparison'
 disabled = clock+['--exploratory-subwindow-enable','0']
 dlc = run('unknown-dlc-request', disabled, '0x805e39f0','0x1f205e10')
 assert dlc['scaler_initializer_returns'] == 1 and dlc['output_sync_observed_transitions'] == 12
 pending = run('dlc-request-pending', disabled+['--exploratory-dlc-pending','1'], '0x805e3a00','0x1f205e10')
 active = disabled+['--exploratory-dlc-pending','0']
 completion = run('unknown-dlc-completion', active, '0x805e3d14','0x1f205e10')
 assert completion['dlc_initializer_returns'] == 0
 timed = active+['--exploratory-dlc-timeout']
 comb = run('unknown-comb-selector', timed, '0x805f87dc','0x1f206c00')
 assert comb['dlc_initializer_returns'] == 1 and comb['preserved_bit_unknown_ram_bytes'] >= 3
 trace = directory/'display.jsonl'
 full = timed+['--exploratory-comb-bank','0']
 # The conservative RMW proof now rejects the fresh read before combining
 # it with a separately invalid saved field. No policy is enabled here:
 # unknown state must stop at the original LHU, before any SH/device use.
 final = run('comb-high-byte-guard',full+['--record-trace',str(trace)],'0x805f8824','0x1f206d1c')
 assert final['reason'] == 'unmodeled MMIO read', final['reason']
 assert final['registers'][4] == '0xbf206d1c' and final['recent_instructions'][-1]['word'] == '0x94820000'
 assert final['scaler_initializer_returns'] == 1 and final['dlc_initializer_returns'] == 1
 assert final['guest_exception_vector_entries'] == final['guest_software_tick'] == 606
 returns=[]; edges=0; requests=0
 with trace.open() as f:
  for line in f:
   e=json.loads(line)
   if e['kind']=='guest_output_vsync_wait_return': returns.append(e['value'])
   if e['kind']=='exploratory_output_vsync_edge': edges+=1
   if e['kind']=='dlc_capture_request': requests+=1
 assert returns == ['0x0','0x0','0x0'], returns
 assert edges == 12 and requests >= 2
 replay = run('full-display-replay',full+['--replay-trace',str(trace)])
 assert replay == final, 'entire original boot/IRQ/MMIO trace must replay identically'
 slower = run('half-output-clock',['--exploratory-display-clock','74250000'],'0x805fe070','0x1f205e40')
 assert slower['instruction_dispatches'] > gate['instruction_dispatches'] and slower['guest_exception_vector_entries'] > gate['guest_exception_vector_entries']
 assert slower['output_sync_observed_transitions'] == gate['output_sync_observed_transitions'], 'same pulses require more virtual time at half clock'
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest() == stock
Path(a.report).write_text(json.dumps({'result':'PASS','original_sha256':stock,'runs':len(results),
 'assumptions_not_hardware_validation':True,'results':results},indent=2)+'\n')
print('PASS: original output waits, feature-gate guard, DLC completion guard/timeouts, banked COMB path and deterministic replay; no authentic frame claimed')
