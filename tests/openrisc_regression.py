"""Real dual-CPU cold boot and replay. No injected TSP response or substitute image."""
import argparse,hashlib,json,struct,subprocess,tempfile,collections
from pathlib import Path
p=argparse.ArgumentParser()
for name in ('firmware','emulator','reference-summary','reference-trace','reference-coverage','report'):
 p.add_argument('--'+name,required=True)
p.add_argument('--validate-only',action='store_true',help='check original payloads and recorded producer events without claiming a new CPU replay')
a=p.parse_args();stock=Path(a.firmware).read_bytes()
assert len(stock)==0x800010 and hashlib.sha256(stock).hexdigest()=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
image=stock[0x1e7838:0x1ebc40]
assert hashlib.sha256(image).hexdigest()=='69785f9a58c2927ceb9e92cd0fb5d8b39c9978a3c2971a7e9e6fbd9a09df189a'
ref=json.loads(Path(a.reference_summary).read_text());pcs=collections.Counter();replies=[];boundaries=[];scart=[];scmi=[];windows=[];euro=[];rmw=[];rmw_reads=[];pq=[];pq_sources=[];pq_commits=[];conditional_reads=[];conditional_writes=[];dummy=[]
revision_reads=[];fetch_decisions=[];fetch_parameters=[];vd_written=bytearray();vd_readback=bytearray();vd_attempts=[];vd_xdata=[];osd_entries=collections.Counter()
mailbox_doorbells=[];mailbox_reads=[];mailbox_command=None;ci_samples=[];pcmcia_dummy=[];deferred_keys=[];vd_starts=[]
shared=ref.get('exploratory_vd_shared_run',False);shared_writes=[];shared_alias_reads=[];unused_decoder=[]
avd_written=bytearray();avd_readback=bytearray();avd_starts=[];avd_cached=[];shared_status=None
audio_reset_writes=[];audio_status_writes=[];audio_status_blocks=[]
audio_ack_samples=[];audio_ack_predicates=[];audio_ack_timeouts=[];audio_ack_returns=[];audio_recovery_writes=[];audio_mode_preservation=[]
audio_word_selectors=[];sif_command_samples=[];sif_command_restores=[]
sif_command_producer=None;sif_command_writes=[];sif_command_reads=[]
avd_instructions=[];avd_external=[];avd_suspensions=[]
idma=[{'memory':{},'phase':[],'address':None,'read_address':None,'readback':None,'writes':0,'reads':0} for _ in range(2)]
idma_addresses=[{} for _ in range(2)]
idma_byte_addresses=[]
idma_readbacks=0
assert ref['exploratory_tsp_cpu'] and ref['or1k_instructions']>459000
assert ref['instruction_dispatches']>666736000 and ref['gop_initializer_entries']>=2
assert ref['guest_decompressed_sha256']=='149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236'
assert ref['tsp_firmware_download_completions']==1 and ref['tsp_firmware_download_bytes']==12288
assert not ref['osd_demonstrated'],'test does not turn a blank unconfigured host window into OSD evidence'
with Path(a.reference_trace).open() as f:
 for line in f:
  # Full native replay still checks every event. Static payload validation
  # needs only these two auxiliary event types, avoiding JSON decoding of
  # millions of unchanged original polling-loop memory events.
  if '"kind":"or1k_' in line and '"kind":"or1k_instruction"' not in line and '"kind":"or1k_register_write"' not in line:continue
  e=json.loads(line)
  if e['kind']=='avd_mcu_instruction':avd_instructions.append(e)
  if e['kind']=='audio_idma_address_byte':idma_byte_addresses.append(e)
  if e['kind']=='avd_mcu_external_unresolved':avd_external.append(e)
  if e['kind']=='exploratory_avd_boundary_suspension':avd_suspensions.append(e)
  if ref.get('candidate_audio_idma'):
   address=int(e['address'],16)
   if e['kind']=='write' and (0x1f225a00<=address<0x1f225b00 or 0x1f225c00<=address<0x1f225d00):
    base=0x1f225c00 if address>=0x1f225c00 else 0x1f225a00;port=idma[base==0x1f225c00];offset=address-base;value=int(e['value'],16)
    latch=idma_addresses[base==0x1f225c00]
    if offset in (8,0x10) and e['width']==2:
     latch[offset]=value&255;latch[offset+1]=value>>8
     if offset==8:assert not port['phase'];port['address']=value
     else:port['read_address']=value
    if offset in (8,9,0x10,0x11) and e['width']==1:
     latch[offset]=value;start=offset&~1
     assembled=latch[start]|latch[start+1]<<8 if start in latch and start+1 in latch else None
     if start==8:assert not port['phase'];port['address']=assembled
     else:port['read_address']=assembled
    if offset in (4,5) and e['width']==1:
     assert offset==(4,5,4,5)[len(port['phase'])] and port['address'] is not None
     port['phase'].append(value)
     if len(port['phase'])==4:
      middle,high,low,padding=port['phase'];assert padding==0
      port['memory'][port['address']]=bytes((low,middle,high));port['address']=(port['address']+1)&65535
      port['phase']=[];port['writes']+=1
    if offset==0 and e['width']==1 and value&8:
     assert port['read_address'] is not None
     port['readback']=port['memory'].get(port['read_address']);port['read_address']=(port['read_address']+1)&65535;port['reads']+=1
   if e['kind']=='audio_idma_readback':
    base=0x1f225c00 if address>=0x1f225c00 else 0x1f225a00;port=idma[base==0x1f225c00];offset=address-base
    assert port['readback'] is not None and int(e['value'],16)==port['readback'][{0x18:1,0x19:2,0x1c:0}[offset]],'readback must equal the actual addressed original upload'
    idma_readbacks+=1
  if e['kind']=='mcu_mailbox_doorbell':mailbox_doorbells.append(e)
  if e['kind']=='exploratory_ci_detect_sample':ci_samples.append(e)
  if e['kind']=='deferred_pcmcia_initial_observation':pcmcia_dummy.append(e)
  if e['kind']=='deferred_vd_reset_key_write':deferred_keys.append(e)
  if e['kind']=='vd_mcu_start_request':vd_starts.append(e)
  if e['kind']=='deferred_decoder_unused_sample':unused_decoder.append(e)
  if e['kind']=='avd_mcu_start_request':avd_starts.append(e)
  if e['kind']=='deferred_avd_config_cache':avd_cached.append(e)
  if e['kind']=='write' and e['address']=='0x1f225800':audio_reset_writes.append(e)
  if e['kind']=='write' and e['address'] in ('0x1f225ab4','0x1f225cb8'):audio_status_writes.append(e)
  if e['kind']=='blocked_read' and e['address'] in ('0x1f225ab4','0x1f225cb8'):audio_status_blocks.append(e)
  if e['kind']=='exploratory_audio_ack_invalid_sample':audio_ack_samples.append(e)
  if e['kind']=='exploratory_audio_no_ack_predicate':audio_ack_predicates.append(e)
  if e['kind']=='guest_audio_ack_timeout':audio_ack_timeouts.append(e)
  if e['kind']=='guest_audio_ack_failure_return':audio_ack_returns.append(e)
  if e['kind']=='deferred_audio_mode_preservation':audio_mode_preservation.append(e)
  if e['kind']=='deferred_audio_word_selector':audio_word_selectors.append(e)
  if e['kind']=='deferred_sif_standard_command':sif_command_samples.append(e)
  if e['kind']=='preserved_control_write' and e['address']=='0x1f225c40':sif_command_restores.append(e)
  if e['kind']=='write' and e['address']=='0x1f225c40' and e['width']==1:
   sif_command_producer=int(e['value'],16);sif_command_writes.append(e)
  if e['kind']=='read' and e['pc']=='0x804f90e8' and e['address']=='0x1f225c40':
   if sif_command_producer is not None:assert int(e['value'],16)&255==sif_command_producer,'standard-command low byte must reflect the original MIPS producer, not a fabricated DSP value'
   sif_command_reads.append(e)
  if e['kind']=='write' and e['address'] in ('0x1f225a70','0x1f225c79') and e['value']=='0xf3':audio_recovery_writes.append(e)
  if e['kind'] in ('avd_mcu_program_write','avd_mcu_program_read'):
   import re
   output=avd_written if e['kind']=='avd_mcu_program_write' else avd_readback
   assert int(re.search(r'address=(\d+)',e['detail'])[1])==len(output)
   output.append(int(e['value'],16));assert e['width']==1
  if shared and e['kind']=='vd_shared_mailbox_write':
   shared_writes.append(e)
   if e['address']=='0x1f224c00':mailbox_command=int(e['value'],16)
  if shared and e['kind'] in ('write','vd_shared_mailbox_write') and e['address']=='0x1f224c38':shared_status=int(e['value'],16)
  if shared and e['kind']=='read' and e['address']=='0x1f224c38':
   assert shared_status is not None and int(e['value'],16)==shared_status,'status read must reflect the last actual producer store, including outstanding guest request bits'
   shared_alias_reads.append(e)
  if ref.get('exploratory_vd_suspended_boundary') and e['address']=='0x1f224c00':
   if e['kind']=='write':mailbox_command=int(e['value'],16)
   if e['kind']=='read':
    assert mailbox_command is not None and int(e['value'],16)==mailbox_command,'stalled DVB-T MCU must never invent an FF acknowledgment'
    mailbox_reads.append(e)
  if e['kind']=='exploratory_fetch_revision_read':revision_reads.append(e)
  if e['kind']=='guest_fetch_patch_decision':fetch_decisions.append(e)
  if e['kind']=='guest_fetch_parameter':fetch_parameters.append(e)
  if e['kind'] in ('vd_mcu_program_write','vd_mcu_program_read'):
   import re
   output=vd_written if e['kind']=='vd_mcu_program_write' else vd_readback
   index=int(re.search(r'address=(\d+)',e['detail'])[1]);assert index==len(output),'VD address cursor must advance through the actual guest upload/readback'
   output.append(int(e['value'],16));assert e['width']==1
  if e['kind']=='vd_mcu_instruction':vd_attempts.append(e)
  if e['kind'] in ('vd_mcu_blocked_xdata','vd_mcu_blocked_sfr'):vd_xdata.append(e)
  if e['kind']=='guest_osd_routine_entry':osd_entries[e['address']]+=1
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
if ref.get('exploratory_fetch_revision_byte') is not None:
 assert revision_reads and all(int(e['value'],16)==ref['exploratory_fetch_revision_byte'] for e in revision_reads)
 assert len(fetch_decisions)==len(revision_reads)==len(fetch_parameters)
 assert all(int(e['value'],16)==int(ref['exploratory_fetch_revision_byte']<2) for e in fetch_decisions),'original predicate must execute, not a host-returned workaround decision'
 assert '0x806006dc' in coverage and '0x805f4b44' in coverage
if ref.get('candidate_vd_mcu_program'):
 import zlib
 main=zlib.decompress(stock[0x23880:0x16124e],-15)
 vd=main[0x806a4a04-0x803b3980:0x806aa00e-0x803b3980]
 assert len(vd)==22026 and hashlib.sha256(vd).hexdigest()=='2bdb82e863899ea863f63bb4596235615c8f5aa5f77bdc465bec2d465bde22a5'
 assert vd_written==vd_readback==vd,'original MIPS writes and reads every actual MCU byte; a fake successful verification cannot satisfy this check'
 assert len(vd_written)==ref['vd_mcu_program_writes']==ref['vd_mcu_program_reads']==ref['vd_mcu_contiguous_program_bytes']
 assert ref['vd_mcu_program_sha256']==hashlib.sha256(vd).hexdigest()
 assert len(vd_starts)==ref['vd_mcu_start_requests']>=1 and vd_starts[0]['pc']=='0x804f57cc'
 if len(vd_starts)==2:assert vd_starts[1]['pc']=='0x804f48d8','the second original hold/release must restart the uploaded MCU image'
 assert '0x804f5784' in coverage and '0x804f57cc' in coverage
 if ref.get('exploratory_vd_mcu_startup'):
  if shared:
   assert len({e['address'] for e in vd_attempts})==len(vd_attempts),'scheduled MCU trace records original first visits, not a fabricated instruction count'
   assert ref['vd_mcu_total_instructions']>26000 and ref['vd_mcu_interrupt_entries']>=2
   assert len(shared_writes)==ref['vd_mailbox_writes']
   producers=[e for e in shared_writes if e['address']=='0x1f224c38']
   assert producers and len(producers)<=len(vd_starts) and all(e['value']=='0x0' for e in producers),'only original MCU startup initializes host status byte; a held early startup need not reach it'
   assert shared_alias_reads and all(e['tick']>=producers[0]['tick'] for e in shared_alias_reads)
   assert '0x804f496c' in coverage and '0x804f4970' in coverage
  else:
   per_start=ref['vd_mcu_instructions']+1
   assert len(vd_attempts)==per_start*len(vd_starts) and all(vd_attempts[i*per_start]['address']=='0x0' for i in range(len(vd_starts)))
  assert all(int(e['value'],16)==vd[int(e['address'],16)] for e in vd_attempts)
  if not shared:assert vd_attempts[-1]['address']==ref['vd_mcu_pc'] and len(vd_xdata)==len(vd_starts),'each original auxiliary reset must reach an unresolved hardware operation'
if ref.get('osd_probes_enabled'):assert dict(osd_entries)==ref['osd_routine_entries']
if ref['stop_address']=='0x1f001cd1':
 assert ref['stop_pc']=='0x80404bf4' and ref['audio_ack_failure_returns']==2
 assert '0x804868e8' in coverage and '0x80469b94' in coverage,'original audio and platform initializers must return to the application'
 assert '0x80404c04' not in coverage and '0x804e0d48' not in coverage,'unknown PM activity must stop before its predicate or ZUI is fabricated'
 assert idma[1]['memory'][0]==bytes.fromhex('3f0818') and idma[1]['memory'][1]==bytes.fromhex('1f000a'),'later encoded data-bank coefficient writes must preserve original decoder reset/IRQ vectors'
 assert idma_byte_addresses and all(e['pc']=='0x804f9230' for e in idma_byte_addresses),'later scalar writes must update the actual original byte-address latch'
if ref.get('candidate_avd_mcu_program'):
 import zlib
 main=zlib.decompress(stock[0x23880:0x16124e],-15)
 avd=main[0x80627a00-0x803b3980:0x8062ae00-0x803b3980]
 assert len(avd)==13312 and hashlib.sha256(avd).hexdigest()=='48ab58a5689d3135b2cdf78fa77da92fa912776f741c33c7afa97fdcfc4ee4fd'
 assert avd_written==avd_readback==avd,'original MIPS must upload and verify the actual AVD program, not a host-returned success'
 assert len(avd_written)==ref['avd_mcu_program_writes']==ref['avd_mcu_program_reads']==ref['avd_mcu_program_bytes']
 assert len(avd_starts)==ref['avd_mcu_start_requests']==1
 if ref.get('exploratory_avd_mcu_startup'):
  assert [int(e['address'],16) for e in avd_instructions]==[0,3,6,0x219,0x7fd,0x800,0x802]
  assert all(int(e['value'],16)==avd[int(e['address'],16)] for e in avd_instructions),'AVD fetched opcodes must belong to the original uploaded image'
  assert ref['avd_mcu_instructions']==6 and ref['avd_mcu_pc']=='0x802' and ref['avd_mcu_suspended']
  assert len(avd_external)==len(avd_suspensions)==1 and avd_external[0]['address']=='0xc008' and avd_external[0]['value']=='0x55'
 else:assert ref['avd_mcu_instructions']==0,'release alone does not demonstrate auxiliary execution'
 assert unused_decoder and '0x805048a4' in coverage and '0x8050491c' in coverage
if ref['stop_address']=='0x1f225ab4':
 assert [(e['pc'],e['value']) for e in audio_reset_writes[:2]]==[('0x804fa418','0x3'),('0x804fa434','0x0')]
 assert audio_reset_writes[1]['tick']>audio_reset_writes[0]['tick'],'the original reset delay must execute'
 assert not audio_status_writes and len(audio_status_blocks)==1,'unknown DSP2 response must not be fabricated by a host ready write'
 assert avd_cached and ref['preserved_bit_unknown_ram_bytes']>=9,'AVD cache provenance must survive the independent audio initialization'
if ref.get('candidate_audio_idma'):
 for port,observed in zip(idma,ref['audio_idma_ports']):
  state=b''.join(struct.pack('<H',a)+v for a,v in sorted(port['memory'].items()))
  assert observed['word_uploads']==port['writes'] and observed['word_reads']==port['reads']
  assert observed['initialized_words']==len(port['memory']) and observed['addressed_memory_sha256']==hashlib.sha256(state).hexdigest()
 assert idma_readbacks,'original addressed memory verification must execute'
if ref.get('exploratory_audio_no_ack'):
 assert not audio_status_writes,'no host ready write or valid DSP reply may be produced'
 assert len(audio_ack_samples)==len(audio_ack_predicates)==ref['audio_ack_unknown_samples']==ref['audio_ack_constrained_comparisons']
 assert len(audio_ack_timeouts)==ref['audio_ack_timeouts']==len(audio_ack_returns)==ref['audio_ack_failure_returns']
 phases=[('0x1f225ab4','0x804f9098','0x804f7a28','0x804f7a60','0x804f7a84','0x1f225a70','0x804f927c'),
         ('0x1f225cb8','0x804f9050','0x804f7b10','0x804f7b58','0x804f7b8c','0x1f225c79','0x804f9230')]
 for source,reader,branch,timeout,returned,command,writer in phases:
  samples=[e for e in audio_ack_samples if e['address']==source]
  if not samples:continue
  assert len(samples)==100 and all(e['pc']==reader for e in samples)
  assert not any(e['address']==source for e in audio_status_blocks),'a constrained observation must remain invalid and scoped, not become a fabricated valid register'
  predicates=[e for e in audio_ack_predicates if e['pc']==branch]
  assert len(predicates)==100 and all(e['value']=='0x1452fff7' for e in predicates),'only the exact original BNE predicates may be constrained'
  failures=[e for e in audio_ack_timeouts if e['pc']==timeout];returns=[e for e in audio_ack_returns if e['pc']==returned]
  assert len(failures)==len(returns)==1 and failures[0]['value']=='0x65' and returns[0]['value']=='0x1'
  assert samples[0]['tick']<predicates[-1]['tick']<failures[0]['tick']<returns[0]['tick']
  assert any(failures[0]['tick']<e['tick']<returns[0]['tick'] and e['address']==command and e['pc']==writer for e in audio_recovery_writes),'original timeout must issue its own F3 recovery store before returning'
 assert len(audio_ack_samples)==100*len(audio_ack_timeouts) and len(audio_ack_timeouts) in (1,2)
 assert '0x804f7398' in coverage,'the original audio caller must resume normally'
 assert avd_cached and ref['preserved_bit_unknown_ram_bytes']>=9,'independent invalid AVD cache must remain guarded'
 if sif_command_samples:
  assert all(e['pc']=='0x804f90e8' and e['address']=='0x1f225c40' for e in sif_command_samples)
  if any(int(e['value'],16)&15!=15 for e in sif_command_samples):
   assert sif_command_restores and any(e['pc']=='0x804f9230' and int(e['value'],16)&15==15 for e in sif_command_restores),'unknown command nibble must return through the original same-byte store, not become a valid response'
  else:assert sif_command_writes and sif_command_reads,'known command byte requires a genuine original producer and consumed read'
if ref.get('exploratory_vd_suspended_boundary'):
 assert ref['vd_mcu_suspended'] and ref['vd_mcu_boundary_reason'] and ref['mcu_mailbox_interrupt_pending']
 assert len(mailbox_doorbells)==ref['mcu_mailbox_interrupt_requests']>0 and all(e['value']=='0x2' for e in mailbox_doorbells)
 assert mailbox_reads and '0x804f2f10' in coverage,'original bounded mailbox timeout must return failure, not a host-generated success'
if ref.get('exploratory_ci_detect_high'):
 assert len(ci_samples)==ref['ci_detect_reads']>0 and all(e['value']=='0x4' for e in ci_samples)
 assert pcmcia_dummy and all(e['value']=='0x0' for e in pcmcia_dummy),'discarded PCMCIA pre-read remains an invalid observation'
 assert '0x8051e980' in coverage,'original firmware must execute its card-detect bit mask'
if ref.get('deferred_vd_reset_key_writes_enabled'):
 assert [(int(e['address'],16),int(e['value'],16)) for e in deferred_keys]==[
  (0x1080,0xaa),(0x1081,0x55),(0x1082,0xaa),(0x1083,0x55)]*(len(deferred_keys)//4),'only complete original reset-key sequences may be deferred'
 assert len(deferred_keys)==4*len(vd_starts) if not shared else len(deferred_keys)>=8,'full shared startup reaches its repeated original reset-key sequence; early reset may interrupt the first attempt'
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
if ref.get('exploratory_fetch_revision_byte') is not None:flags+=['--exploratory-fetch-revision-byte',str(ref['exploratory_fetch_revision_byte'])]
if ref.get('candidate_vd_mcu_program'):flags+=['--candidate-vd-mcu-program']
if ref.get('candidate_avd_mcu_program'):flags+=['--candidate-avd-mcu-program']
if ref.get('candidate_audio_idma'):flags+=['--candidate-audio-idma']
if ref.get('exploratory_audio_no_ack'):flags+=['--exploratory-audio-no-ack']
if ref.get('exploratory_avd_mcu_startup'):flags+=['--exploratory-avd-mcu-startup','--exploratory-avd-suspended-boundary']
if ref.get('exploratory_vd_mcu_startup'):flags+=['--exploratory-vd-mcu-startup']
if ref.get('deferred_vd_sfr8e_write_enabled'):flags+=['--defer-vd-sfr8e-write']
if ref.get('deferred_vd_reset_key_writes_enabled'):flags+=['--defer-vd-reset-key-writes']
if ref.get('exploratory_vd_suspended_boundary'):flags+=['--exploratory-vd-suspended-boundary']
if shared:flags+=['--exploratory-vd-shared-run']
if ref.get('exploratory_ci_detect_high'):flags+=['--exploratory-ci-detect-high']
if ref.get('osd_probes_enabled'):flags+=['--osd-probes']
if ref.get('graphics_probes'):flags+=['--graphics-probes']
if ref.get('exploratory_analog_disconnected'):flags+=['--exploratory-analog-disconnected']
if ref.get('exploratory_pq_sram_idle'):flags+=['--exploratory-pq-sram-idle']
if ref.get('exploratory_pq_disabled_ports'):flags+=['--exploratory-pq-disabled-ports']
if ref.get('or1k_trace_first_visits'):flags+=['--or1k-trace-first-visits']
flags+=['--max-or1k-instructions',str(ref['or1k_budget'])]
flags+=['--or1k-dispatch-divider',str(ref['or1k_dispatch_divider'])]
replay=None
if not a.validate_only:
 with tempfile.TemporaryDirectory(prefix='manta-or1k-replay-') as directory:
  out=Path(directory)/'summary.json'
  r=subprocess.run([a.emulator]+flags+['--replay-trace',a.reference_trace,'--summary',str(out)],capture_output=True,text=True)
  assert r.returncode==2,(r.returncode,r.stderr)
  replay=json.loads(out.read_text());assert replay==ref,'both CPUs, IRQs and every event must replay exactly'
  assert '@@@ Hello T4' in r.stdout and 'First GOP driver instance, flush GWIN HW' in r.stdout
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==hashlib.sha256(stock).hexdigest()
Path(a.report).write_text(json.dumps({'result':'PASS','emulator_sha256':hashlib.sha256(Path(a.emulator).read_bytes()).hexdigest(),
 'scope':'recorded evidence validation only' if a.validate_only else 'full original CPU replay',
 'full_dual_CPU_trace_replay':'NOT RUN' if a.validate_only else 'PASS','original_TSP_SHA256':hashlib.sha256(image).hexdigest(),
 'original_instruction_words_verified':ref['or1k_original_word_fetches'],'original_unique_OR_PCs':len(pcs),
 'MIPS_Alive_success_branch_executed':True,'original_auxiliary_replies':replies,
 'original_fetch_revision_observations':revision_reads,'original_fetch_decisions':fetch_decisions,'original_fetch_parameters':fetch_parameters,
 'VD_MCU_actual_uploaded_and_verified_bytes':len(vd_written),'VD_MCU_original_instruction_attempts':len(vd_attempts),'VD_MCU_blocked_hardware':vd_xdata,
 'full_three_CPU_trace_replay':('NOT RUN' if a.validate_only else 'PASS') if shared else None,'original_MCU_shared_writes':shared_writes,
 'original_MCU_status_alias_reads':shared_alias_reads,'guarded_unused_decoder_reads':unused_decoder,
 'AVD_actual_uploaded_and_verified_bytes':len(avd_written),'AVD_original_release_requests':avd_starts,
 'AVD_original_instruction_attempts':avd_instructions,'AVD_unresolved_XDATA':avd_external,
 'full_four_CPU_trace_replay':('NOT RUN' if a.validate_only else 'PASS') if ref.get('exploratory_avd_mcu_startup') else None,
 'audio_IDMA_original_readback_bytes':idma_readbacks,'audio_IDMA_addressed_storage':ref.get('audio_idma_ports'),
 'audio_IDMA_original_byte_address_writes':idma_byte_addresses,
 'guarded_AVD_unknown_cache_observations':avd_cached,
 'original_audio_reset_writes':audio_reset_writes,'unsupported_DSP2_status_observations':audio_status_blocks,
 'exploratory_audio_ack_invalid_samples':audio_ack_samples,'exploratory_audio_no_ack_predicates':audio_ack_predicates,
 'original_audio_timeout_entries':audio_ack_timeouts,'original_audio_failure_returns':audio_ack_returns,
 'original_audio_recovery_writes':audio_recovery_writes,'unknown_audio_mode_preservation':audio_mode_preservation,
 'unknown_audio_halfword_selector_samples':audio_word_selectors,'unknown_SIF_command_samples':sif_command_samples,
 'original_SIF_same_origin_command_restores':sif_command_restores,
 'original_SIF_command_byte_producers':sif_command_writes,'original_SIF_command_byte_reads':sif_command_reads,
 'original_OSD_routine_entries':dict(osd_entries),
 'original_DVB_MCU_mailbox_doorbells':mailbox_doorbells,'original_DVB_mailbox_reads_without_fabricated_ack':len(mailbox_reads),
 'exploratory_CI_detect_samples':ci_samples,'discarded_PCMCIA_observations':pcmcia_dummy,
 'original_MCU_deferred_reset_key_writes':deferred_keys,
 'SCART_original_predicate_samples':scart,'SCMI_partial_known_readback':scmi,'original_GOP_window_programming_entries':len(windows),
 'disconnected_analog_detection_observations':euro,
 'original_masked_bit0_stores':rmw,'original_PQ_table_sources':pq_sources,
 'original_PQ_payload_submissions':len(pq),'original_PQ_payload_commits':len(pq_commits),
 'original_known_branch_RMW_stores':conditional_writes,'unused_PQ_SAR_observations':dummy,
 'hardware_assumptions_remain_explicit':True,'boundaries':boundaries,'replay':replay},indent=2)+'\n')
print('PASS: original payloads, producer events and immutable firmware; '+('recorded evidence only, no new CPU replay' if a.validate_only else 'full original CPU deterministic replay')+'; no OSD claim')
