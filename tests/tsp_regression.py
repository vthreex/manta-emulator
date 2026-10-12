"""Cold-guest TSP reset/indirect/DMA milestone with full event replay.
No auxiliary CPU, TS demux completion or OSD frame is inferred from these tests.
"""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser()
for name in ('firmware','emulator','reference-summary','reference-trace','report'):
 p.add_argument('--'+name,required=True)
a=p.parse_args();stock=hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()
assert stock=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
ref=json.loads(Path(a.reference_summary).read_text())
assert ref['runtime_entry_reached'] and ref['mode']=='firmware from reset flash'
assert ref['guest_decompressed_sha256']=='149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236'
assert ref['instruction_dispatches']==666276473 and ref['guest_exception_vector_entries']==ref['guest_software_tick']==1844
assert ref['stop_pc']=='0x805d6bac' and ref['stop_address']=='0x1f202be8'
assert 'OpenRISC auxiliary CPU' in ref['reason']
assert ref['tsp_queue_known'] and not ref['tsp_reset_held'] and ref['tsp_queue_count']==0
assert ref['tsp_filein_submissions']==0,'there was no DMA work drained before the original count poll'
assert ref['tsp_reset_assertions']==2 and ref['tsp_reset_releases']==1
assert ref['tsp_indirect_reads']==336 and ref['tsp_indirect_writes']==880
assert ref['tsp_firmware_download_requests']==ref['tsp_firmware_download_completions']==1
assert not ref['tsp_firmware_download_pending'] and ref['tsp_firmware_download_bytes']==12288
assert ref['tsp_firmware_download_source']=='0x72db00'
assert ref['gop_initializer_entries']==2 and ref['gop_palette_commits']==64 and not ref['osd_demonstrated']
assert sum(x['origin'] in ('0x1f200a38','0x1f200a3c') for x in ref['invalid_observation_bytes'])==4
events=[];commands=[];images=[];descriptors=[]
with Path(a.reference_trace).open() as f:
 for line in f:
  e=json.loads(line)
  if e['kind'] in ('exploratory_tsp_reset','tsp_cmdq_reset','tsp_firmware_download_request',
    'tsp_firmware_download_complete','tsp_original_firmware_buffer'):events.append(e)
  if e['kind']=='write' and e['address']=='0x1f202af8':commands.append(e)
  if e['kind']=='gop_window_descriptor':descriptors.append(e)
assert not commands and not descriptors
requests=[e for e in events if e['kind']=='tsp_firmware_download_request']
done=[e for e in events if e['kind']=='tsp_firmware_download_complete']
assert len(requests)==len(done)==1 and done[0]['tick']-requests[0]['tick']>=768
assert requests[0]['address']==done[0]['address']=='0x72db00' and requests[0]['width']==done[0]['width']==12288
buffer=next(e for e in events if e['kind']=='tsp_original_firmware_buffer')
assert buffer['width']==17416 and '69785f9a58c2927ceb9e92cd0fb5d8b39c9978a3c2971a7e9e6fbd9a09df189a' in buffer['detail']
flags=['--firmware',a.firmware,'--headless','--symbolic-init','--candidate-miu','--candidate-bdma',
 '--candidate-uart','--gpio-i2c','--defer-diagnostic-reads','--virtual-rtc','--cp0-timer','--candidate-utmi',
 '--candidate-spi','--exploratory-spi-input','pull-up','--candidate-chip-id','--exploratory-sc-bank','0',
 '--candidate-gamma','--exploratory-gamma-enable','0','--exploratory-display-clock','148500000',
 '--exploratory-subwindow-enable','0','--exploratory-dlc-pending','0','--exploratory-dlc-timeout',
 '--exploratory-comb-bank','0','--exploratory-comb-paired-restore','--exploratory-gop-bank','0',
 '--candidate-semaphore','--exploratory-semaphore-idle','--candidate-ge','--exploratory-ge-reset',
 '--candidate-gop-palette','--candidate-tsp','--exploratory-tsp-reset-low','--max-instructions','1200000000']
with tempfile.TemporaryDirectory(prefix='manta-tsp-replay-') as directory:
 out=Path(directory)/'summary.json';image=Path(directory)/'tsp.bin'
 r=subprocess.run([a.emulator]+flags+['--replay-trace',a.reference_trace,'--summary',str(out),
    '--tsp-firmware-dump',str(image)],capture_output=True,text=True)
 assert r.returncode==2,(r.returncode,r.stderr)
 replay=json.loads(out.read_text());assert replay==ref,'full original CPU/IRQ/MMIO execution must replay identically'
 assert image.stat().st_size==17416 and hashlib.sha256(image.read_bytes()).hexdigest()=='69785f9a58c2927ceb9e92cd0fb5d8b39c9978a3c2971a7e9e6fbd9a09df189a'
 assert '@@@ Hello T4' in r.stdout and 'First GOP driver instance, flush GWIN HW' in r.stdout
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==stock
Path(a.report).write_text(json.dumps({'result':'PASS','original_sha256':stock,
 'emulator_sha256':hashlib.sha256(Path(a.emulator).read_bytes()).hexdigest(),
 'full_trace_replay':'PASS','assumptions_not_hardware_validation':True,'events':events,'replay':replay},indent=2)+'\n')
print('PASS: original TSP reset/count, 336/880 indirect operations, actual 12KiB QMEM transfer, unchanged guest/engine and full deterministic replay; auxiliary CPU and OSD unsupported')
