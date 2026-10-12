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
ref=json.loads(Path(a.reference_summary).read_text());pcs=collections.Counter();replies=[];boundaries=[];scart=[];scmi=[];windows=[];euro=[];rmw=[];rmw_reads=[];pq=[];pq_sources=[];pq_commits=[];conditional_reads=[];conditional_writes=[];dummy=[]
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
  if e['kind']=='exploratory_scart_grounded_sample':scart.append(e)
  if e['kind']=='guarded_scmi_format_read':scmi.append(e)
  if e['kind']=='guest_gop_window_program':windows.append(e)
  if e['kind']=='analog_input_detection':euro.append(e)
  if e['kind']=='write' and e['pc']=='0x806001e4':rmw.append(e)
  if e['kind']=='read' and e['pc']=='0x806001d0':rmw_reads.append(e)
  if e['kind']=='pq_sram_submit':pq.append(e)
  if e['kind']=='pq_sram_commit':pq_commits.append(e)
  if e['kind']=='guest_pq_table_upload':pq_sources.append(e)
  if e['kind']=='read' and e['pc'] in ('0x805fdc3c','0x805fdd24'):conditional_reads.append(e)
  if e['kind']=='write' and e['pc'] in ('0x805fdd1c','0x805fdd3c'):conditional_writes.append(e)
  if e['kind']=='deferred_pq_dummy_observation':dummy.append(e)
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
if ref.get('exploratory_scart_grounded'):
 assert len(scart)==ref['scart_input_reads'] and scart
 assert all(e['pc']=='0x804f0c48' and int(e['value'],16)&15==0 for e in scart)
 assert '0x804f0c4c' in coverage,'original SCART predicate must execute beyond the read'
if ref.get('exploratory_scmi_format_byte') is not None:
 assert scmi and all(int(e['value'],16)&255==255 for e in scmi),'configured format low byte must be known; upper validity is preserved independently'
if ref.get('graphics_probes'):
 assert len(windows)==ref['graphics_probes']['window_program_entries']
if ref.get('exploratory_analog_disconnected'):
 assert euro and all(e['value']=='0x0' for e in euro),'disconnected input must not fabricate positive Euro/Australia detection'
 assert '0x805f1cb4' in coverage,'original detection predicate must execute'
if ref.get('exploratory_pq_sram_idle'):
 assert rmw and len(rmw)==len(rmw_reads)
 assert all(w['address']==r['address']=='0x1f205e4c' and int(w['value'],16)==int(r['value'],16)&0xfffe for r,w in zip(rmw_reads,rmw)),'original independent SLTU and masked bit0 setter must preserve the other fifteen sampled bits'
 assert len(pq)==ref['pq_sram_submissions'] and len(pq_commits)==ref['pq_sram_commits']
 # Original table source and real destination writes must agree. Removing
 # the guest coefficient loop or payload effects cannot satisfy this check.
 import re
 cursor=0;pq_state=bytearray(6144)
 for source in pq_sources:
  count=source['width']//5;batch=pq[cursor:cursor+count];cursor+=count
  assert len(batch)==count
  kind=int(source['value'],16)
  assert kind in (0,1,4,5,6,7)
  # Original Manta C banks are 00/40/80/C0. The related Marlon driver
  # uses 180/1C0 for its last two banks and must not supply these constants.
  start={0:0,1:0x80,4:0,5:0x40,6:0x80,7:0xc0}[kind]
  assert [int(e['address'],16) for e in batch]==list(range(start,start+count)),'original coefficient address loop must execute'
  blob=bytearray()
  for e in batch:
   plane=int(re.search(r'plane=(\d+)',e['detail'])[1]);assert plane==(0 if kind<2 else 1)
   payload=struct.pack('<I',int(e['value'],16))+bytes([int(re.search(r'fifth payload byte=(\d+)',e['detail'])[1])])
   blob.extend(payload)
   offset=(plane*512+int(e['address'],16))*6;pq_state[offset:offset+6]=bytes([1])+payload
  expected=re.search(r'SHA256=([0-9a-f]{64})',source['detail'])[1]
  assert hashlib.sha256(blob).hexdigest()==expected,'guest SRAM uploads must match the original RAM table bytes'
 assert pq_sources and cursor==len(pq) and len(pq_commits)==len(pq),'all guest table bytes must be submitted and actually committed'
 expected_pq_state=hashlib.sha256(pq_state).hexdigest()
 if ref.get('pq_sram_state_sha256'):
  assert ref['pq_sram_state_sha256']==expected_pq_state,'actual coefficient SRAM storage must match original guest writes, including validity and plane addressing'
if conditional_writes:
 assert len(conditional_reads)==len(conditional_writes)
 for r,w in zip(conditional_reads,conditional_writes):
  mask=0xffef if w['pc']=='0x805fdd1c' else 0xffdf
  assert r['address']==w['address']=='0x1f205e0c' and int(w['value'],16)==int(r['value'],16)&mask,'known enable branch and SEH must preserve all other original halfword bits'
 assert dummy and all(e['value']=='0x0' for e in dummy),'unused SAR data must remain invalid rather than becoming a hardware status'
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
if ref.get('exploratory_scart_grounded'):flags+=['--exploratory-scart-grounded']
if ref.get('exploratory_scmi_format_byte') is not None:flags+=['--exploratory-scmi-format-byte',str(ref['exploratory_scmi_format_byte'])]
if ref.get('graphics_probes'):flags+=['--graphics-probes']
if ref.get('exploratory_analog_disconnected'):flags+=['--exploratory-analog-disconnected']
if ref.get('exploratory_pq_sram_idle'):flags+=['--exploratory-pq-sram-idle']
if ref.get('exploratory_pq_disabled_ports'):flags+=['--exploratory-pq-disabled-ports']
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
 'SCART_original_predicate_samples':scart,'SCMI_partial_known_readback':scmi,'original_GOP_window_programming_entries':len(windows),
 'disconnected_analog_detection_observations':euro,
 'original_masked_bit0_stores':rmw,'original_PQ_table_sources':pq_sources,
 'original_PQ_payload_submissions':len(pq),'original_PQ_payload_commits':len(pq_commits),
 'original_known_branch_RMW_stores':conditional_writes,'unused_PQ_SAR_observations':dummy,
 'hardware_assumptions_remain_explicit':True,'boundaries':boundaries,'replay':replay},indent=2)+'\n')
print('PASS: original TSP instruction words, startup, real reply and MIPS success branch; full dual-CPU deterministic replay; immutable firmware; no OSD claim')
