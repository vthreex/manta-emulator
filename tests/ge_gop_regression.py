"""Verify real stock GE/GOP execution and replay every recorded bus event.
Requires a local immutable original dump and a cold-run reference. A palette
clear is not a framebuffer, and experimental reset assumptions are not validated
hardware behavior. This test deliberately requires OSD to remain unclaimed.
"""
import argparse, hashlib, json, subprocess, tempfile
from pathlib import Path

p=argparse.ArgumentParser()
for name in ('firmware','emulator','reference-summary','reference-trace','report'):
 p.add_argument('--'+name,required=True)
a=p.parse_args()
stock=hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()
assert stock=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
ref=json.loads(Path(a.reference_summary).read_text())
assert ref['mode']=='firmware from reset flash' and ref['runtime_entry_reached']
assert ref['guest_decompressed_sha256']=='149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236'
assert ref['candidate_ge'] and ref['exploratory_ge_reset'] and ref['candidate_gop_palette']
assert ref['instruction_dispatches']>=666140674
assert ref['guest_exception_vector_entries']==ref['guest_software_tick']>=1844
assert ref['ge_parameter_submissions']==ref['ge_parameter_transfers']==ref['ge_parameter_retirements']==626
assert ref['gop_initializer_entered'] and ref['gop_initializer_entries']==2
assert ref['gop_palette_commits']==64 and ref['gop_palette_defined_entries']==[0,64]
assert ref['scaler_initializer_returns']==ref['dlc_initializer_returns']==1
assert ref['gamma_ram_commits']==768 and ref['semaphore_acquisitions']==ref['semaphore_releases']==4
assert not ref['osd_demonstrated']
assert sum(x['origin'] in ('0x1f200a38','0x1f200a3c') for x in ref['invalid_observation_bytes'])==4
initializers=[];palettes=[];descriptors=[];commands=[];opaque_masks=0;opaque_routes=0
with Path(a.reference_trace).open() as f:
 for line in f:
  e=json.loads(line)
  if e['kind']=='gop_initializer':initializers.append(e)
  if e['kind']=='gop_palette_commit':palettes.append(e)
  if e['kind']=='gop_window_descriptor':descriptors.append(e)
  if e['kind']=='write' and e['address']=='0x1f205180':commands.append(e)
  if e['kind']=='deferred_tsp_irq_mask':opaque_masks+=1
  if e['kind']=='deferred_tsp_route_configuration':opaque_routes+=1
assert len(initializers)==2 and all(x['pc']=='0x80539ce4' and x['ra']=='0x80533678' for x in initializers)
assert initializers[0]['tick']==656564909
assert len(palettes)==64 and [int(x['address'],16) for x in palettes]==list(range(768,832))
assert all(x['value']=='0x0' for x in palettes), 'exact original startup clear; no invented colors'
assert not descriptors and not commands, 'checkpoint must not claim unobserved raster/framebuffer activity'
assert opaque_masks>=4 and opaque_routes>0
flags=['--firmware',a.firmware,'--headless','--symbolic-init','--candidate-miu','--candidate-bdma',
 '--candidate-uart','--gpio-i2c','--defer-diagnostic-reads','--virtual-rtc','--cp0-timer',
 '--candidate-utmi','--candidate-spi','--exploratory-spi-input','pull-up','--candidate-chip-id',
 '--exploratory-sc-bank','0','--candidate-gamma','--exploratory-gamma-enable','0',
 '--exploratory-display-clock','148500000','--exploratory-subwindow-enable','0',
 '--exploratory-dlc-pending','0','--exploratory-dlc-timeout','--exploratory-comb-bank','0',
 '--exploratory-comb-paired-restore','--exploratory-gop-bank','0','--candidate-semaphore',
 '--exploratory-semaphore-idle','--candidate-ge','--exploratory-ge-reset','--candidate-gop-palette',
 '--max-instructions','1200000000']
with tempfile.TemporaryDirectory(prefix='manta-ge-replay-') as directory:
 out=Path(directory)/'summary.json'
 r=subprocess.run([a.emulator]+flags+['--replay-trace',a.reference_trace,'--summary',str(out)],capture_output=True,text=True)
 assert r.returncode==2,(r.returncode,r.stderr)
 replay=json.loads(out.read_text())
 extra={'candidate_tsp':False,'exploratory_tsp_reset_low':False,'tsp_queue_known':False,
   'tsp_reset_held':True,'tsp_reset_assertions':0,'tsp_reset_releases':0,
   'tsp_filein_submissions':0,'tsp_queue_count':None,'tsp_indirect_reads':0,'tsp_indirect_writes':0,
   'tsp_firmware_download_requests':0,'tsp_firmware_download_pending':False,
   'tsp_firmware_download_completions':0,'tsp_firmware_download_bytes':0,'tsp_firmware_download_source':'0x0'}
 assert set(replay)-set(ref)<=set(extra),'unexpected golden-summary schema change'
 assert all(replay[k]==extra[k] for k in set(replay)-set(ref))
 assert {k:replay[k] for k in ref}==ref,'every original CPU/IRQ/MMIO checkpoint and replayed event must agree'
 assert '@@@ Hello T4' in r.stdout and 'USB_Init_Configure done...' in r.stdout
 assert 'First GOP driver instance, flush GWIN HW' in r.stdout
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==stock
Path(a.report).write_text(json.dumps({'result':'PASS','original_sha256':stock,
 'emulator_sha256':hashlib.sha256(Path(a.emulator).read_bytes()).hexdigest(),
 'full_trace_replay':'PASS','assumptions_not_hardware_validation':True,
 'initializers':initializers,'palette_pulses':palettes,'opaque_tsp_mask_reads':opaque_masks,
 'opaque_tsp_route_reads':opaque_routes,'replay':replay},indent=2)+'\n')
print('PASS: original GE parameter queue, two original GOP entries, 64 guest palette clears, invalid TSP provenance and complete deterministic replay; no OSD pixels claimed')
