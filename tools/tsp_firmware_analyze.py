"""Identify the real guest-supplied TSP image; never substitutes reference code.
OpenRISC decoder covers the observed reset prefix only and reports unknown words.
GNU or1k opcode definitions: https://gnu.googlesource.com/binutils-gdb/+/master/opcodes/or1k-opc.c
No auxiliary CPU execution or physical reset address is inferred by this tool.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from firmware_extract import validate,inflate_at

def decode(word):
 op=word>>26;rd=(word>>21)&31;ra=(word>>16)&31;rb=(word>>11)&31
 immediate=word&65535;signed=immediate if immediate<32768 else immediate-65536
 if op==6 and (word&0x1f0000)==0:return f'l.movhi r{rd},0x{immediate:x}'
 if op in (0x27,0x29,0x2a,0x2b):
  name={0x27:'addi',0x29:'andi',0x2a:'ori',0x2b:'xori'}[op]
  return f'l.{name} r{rd},r{ra},{signed if op==0x27 else hex(immediate)}'
 if op==0x2e and word&0x3c0 in (0,0x40,0x80,0xc0):
  return f'l.{["slli","srli","srai","rori"][(word>>6)&3]} r{rd},r{ra},{word&31}'
 if word==0x15000000:return 'l.nop 0'
 if op in (0x11,0x12):return f'l.{"jalr" if op==0x11 else "jr"} r{rb}'
 if op in (0,1,3,4):
  displacement=word&0x3ffffff
  if displacement&0x2000000:displacement-=0x4000000
  return f'l.{ {0:"j",1:"jal",3:"bnf",4:"bf"}[op]} pc{displacement*4:+d}'
 if op in (0x21,0x23,0x25):return f'l.{ {0x21:"lwz",0x23:"lbz",0x25:"lhz"}[op]} r{rd},{signed}(r{ra})'
 if op==0x38:
  fn=word&0x7ff;name={0:'add',2:'sub',3:'and',4:'or',5:'xor',8:'sll',0x48:'srl',0x88:'sra'}.get(fn)
  if name:return f'l.{name} r{rd},r{ra},r{rb}'
 return f'.word 0x{word:08x} ; unsupported by bounded analysis decoder'

def analyze(firmware,buffer,manifest):
 stock=Path(firmware).read_bytes();validate(stock);image=Path(buffer).read_bytes()
 assert len(image)==17416 and hashlib.sha256(image).hexdigest()=='69785f9a58c2927ceb9e92cd0fb5d8b39c9978a3c2971a7e9e6fbd9a09df189a'
 assert struct.unpack_from('<2I',image,0x100)==(0x18000000,0xb8000010)
 assert struct.unpack_from('<31I',image,0x108)==tuple(0x9c000000|((31-i)<<21) for i in range(31))
 locations=[];off=stock[:0x800000].find(image)
 if off>=0:locations.append({'kind':'raw SPI','offset':hex(off)})
 for stream in json.loads(Path(manifest).read_text())['streams']:
  end,data=inflate_at(stock[:0x800000],stream['start'])
  assert end==stream['end'] and hashlib.sha256(data).hexdigest()==stream['sha256']
  off=data.find(image)
  if off>=0:locations.append({'kind':'verified DEFLATE content','stream_offset':hex(stream['start']),
     'decompressed_offset':hex(off),'stream_sha256':stream['sha256']})
 prefix=[{'offset':hex(pc),'word':hex(w:=struct.unpack_from('<I',image,pc)[0]),'asm':decode(w)}
   for pc in range(0x100,0x1c4,4)]
 return {'original_spi_sha256':hashlib.sha256(stock).hexdigest(),'guest_buffer_bytes':len(image),
  'guest_buffer_sha256':hashlib.sha256(image).hexdigest(),'guest_physical_source':'0x72db00',
  'isa_evidence':'little-endian OpenRISC: movhi/shift then exact r31..r1 addi-zero initialization, followed by ori/jalr/nop patterns',
  'candidate_reset_prefix_offset':'0x100','physical_reset_PC_not_validated':True,
  'image_locations':locations,'reset_prefix_disassembly':prefix,
  'guest_DMA_bytes':12288,'DMA_prefix_sha256':hashlib.sha256(image[:12288]).hexdigest(),
  'eight_final_bytes':image[-8:].hex(),'final_bytes_meaning_unknown':True,
  'auxiliary_execution_demonstrated':False,'original_firmware_or_reference_payload_redistributed':False}

if __name__=='__main__':
 p=argparse.ArgumentParser()
 for name in ('firmware','guest-buffer','manifest','output'):p.add_argument('--'+name,required=True)
 a=p.parse_args();result=analyze(a.firmware,a.guest_buffer,a.manifest)
 Path(a.output).write_text(json.dumps(result,indent=2)+'\n')
 print('PASS: unchanged SPI, exact original TSP buffer, verified stream search and OpenRISC reset-prefix signatures; no core execution claimed')
