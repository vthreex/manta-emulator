"""Real dual-CPU cold boot and replay. No injected TSP response or substitute image."""
import argparse,hashlib,json,struct,subprocess,tempfile,collections
from pathlib import Path
p=argparse.ArgumentParser()
for name in ('firmware','emulator','reference-summary','reference-trace','reference-coverage','report'):
 p.add_argument('--'+name,required=True)
a=p.parse_args();stock=Path(a.firmware).read_bytes()
assert len(stock)==0x800010 and hashlib.sha256(stock).hexdigest()=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
image=stock[0x1e7838:0x1ebc40]
assert hashlib.sha256(image).hexdigest()=='69785f9a58c2927ceb9e92cd0fb5d8b39c9978a3c2971a7e9e6fbd9a09df189a'
ref=json.loads(Path(a.reference_summary).read_text());pcs=collections.Counter();replies=[];boundaries=[]
assert ref['exploratory_tsp_cpu'] and ref['or1k_instructions']>459000
assert ref['instruction_dispatches']>666736000 and ref['gop_initializer_entries']>=2
assert ref['guest_decompressed_sha256']=='149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236'
assert ref['tsp_firmware_download_completions']==1 and ref['tsp_firmware_download_bytes']==12288
assert not ref['osd_demonstrated'],'test does not turn a blank unconfigured host window into OSD evidence'
with Path(a.reference_trace).open() as f:
 for line in f:
  e=json.loads(line)
  if e['kind']=='or1k_instruction':
   address=int(e['address'],16);assert int(e['value'],16)==struct.unpack_from('<I',image,address)[0]
   pcs[address]+=1
  if e['kind']=='or1k_register_write':replies.append(e)
  if e['kind'] in ('blocked_read','exploratory_or1k_enable','deferred_platform_identity','exploratory_pm_boot_reason'):boundaries.append(e)
if ref.get('or1k_trace_first_visits'):
 assert len(pcs)==ref['or1k_unique_pcs'] and all(n==1 for n in pcs.values())
else:assert sum(pcs.values())==ref['or1k_instructions']
assert ref['or1k_original_word_fetches']==ref['or1k_instructions'] and ref['or1k_modified_word_fetches']==0
assert pcs[0x100]==pcs[0x1dbc]==pcs[0xd94]==1
assert any(e['address']=='0x21033c' and e['value']=='0x1000000' for e in replies)
assert any(e['address']=='0x210330' and e['value']=='0x0' for e in replies)
coverage=set(Path(a.reference_coverage).read_text().split())
assert '0x805d5f88' in coverage,'original MIPS Alive-success branch must actually execute'
flags=['--firmware',a.firmware,'--headless','--symbolic-init','--candidate-miu','--candidate-bdma',
 '--candidate-uart','--gpio-i2c','--defer-diagnostic-reads','--virtual-rtc','--cp0-timer','--candidate-utmi',
 '--candidate-spi','--exploratory-spi-input','pull-up','--candidate-chip-id','--exploratory-sc-bank','0',
 '--candidate-gamma','--exploratory-gamma-enable','0','--exploratory-display-clock','148500000',
 '--exploratory-subwindow-enable','0','--exploratory-dlc-pending','0','--exploratory-dlc-timeout',
 '--exploratory-comb-bank','0','--exploratory-comb-paired-restore','--exploratory-gop-bank','0',
 '--candidate-semaphore','--exploratory-semaphore-idle','--candidate-ge','--exploratory-ge-reset',
 '--candidate-gop-palette','--candidate-tsp','--exploratory-tsp-reset-low','--exploratory-tsp-cpu',
 '--max-instructions','2000000000']
if ref.get('deferred_platform_identity_enabled'):flags+=['--defer-platform-identity']
if ref.get('exploratory_pm_ac_on'):flags+=['--exploratory-pm-ac-on']
if ref.get('exploratory_sar_fullscale'):flags+=['--exploratory-sar-fullscale']
if ref.get('exploratory_ir_dma_reset'):flags+=['--exploratory-ir-dma-reset']
if ref.get('exploratory_gop_reset_windows'):flags+=['--exploratory-gop-reset-windows']
if ref.get('or1k_trace_first_visits'):flags+=['--or1k-trace-first-visits']
flags+=['--max-or1k-instructions',str(ref['or1k_budget'])]
flags+=['--or1k-dispatch-divider',str(ref['or1k_dispatch_divider'])]
with tempfile.TemporaryDirectory(prefix='manta-or1k-replay-') as directory:
 out=Path(directory)/'summary.json'
 r=subprocess.run([a.emulator]+flags+['--replay-trace',a.reference_trace,'--summary',str(out)],capture_output=True,text=True)
 assert r.returncode==2,(r.returncode,r.stderr)
 replay=json.loads(out.read_text());assert replay==ref,'both CPUs, IRQs and every event must replay exactly'
 assert '@@@ Hello T4' in r.stdout and 'First GOP driver instance, flush GWIN HW' in r.stdout
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==hashlib.sha256(stock).hexdigest()
Path(a.report).write_text(json.dumps({'result':'PASS','emulator_sha256':hashlib.sha256(Path(a.emulator).read_bytes()).hexdigest(),
 'full_dual_CPU_trace_replay':'PASS','original_TSP_SHA256':hashlib.sha256(image).hexdigest(),
 'original_instruction_words_verified':ref['or1k_original_word_fetches'],'original_unique_OR_PCs':len(pcs),
 'MIPS_Alive_success_branch_executed':True,'original_auxiliary_replies':replies,
 'hardware_assumptions_remain_explicit':True,'boundaries':boundaries,'replay':replay},indent=2)+'\n')
print('PASS: original TSP instruction words, startup, real reply and MIPS success branch; full dual-CPU deterministic replay; immutable firmware; no OSD claim')
