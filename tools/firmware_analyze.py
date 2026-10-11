#!/usr/bin/env python3
"""Sample-specific binary evidence; no inferred symbol is presented as proven."""
import argparse,collections,json,math,re,struct
from pathlib import Path
from firmware_extract import validate,sha,KNOWN,inflate_at
from capstone import Cs,CS_ARCH_MIPS,CS_MODE_MIPS32,CS_MODE_LITTLE_ENDIAN
def disassemble(b,base):
 c=Cs(CS_ARCH_MIPS,CS_MODE_MIPS32|CS_MODE_LITTLE_ENDIAN);c.skipdata=True
 return list(c.disasm(b,base))
def analyze(path,out):
 if Path(path).stat().st_size!=0x800010:raise ValueError('Wrong input size before reading')
 b=Path(path).read_bytes();f=validate(b);out=Path(out);out.mkdir(parents=True,exist_ok=True)
 entropy=[]
 for start in range(0,len(f),0x1000):
  chunk=f[start:start+0x1000];counts=collections.Counter(chunk)
  entropy.append(dict(start=start,end=start+len(chunk),bits_per_byte=-sum((v/len(chunk))*math.log2(v/len(chunk)) for v in counts.values()),ff_fraction=counts[255]/len(chunk)))
 (out/'block-entropy.json').write_text(json.dumps(entropy,indent=2))
 ins=disassemble(f[:0x2c60],0xbfc00000)
 (out/'boot-disassembly.txt').write_text('\n'.join(f'{i.address:08x}: {i.bytes.hex():8} {i.mnemonic} {i.op_str}' for i in ins)+'\n')
 calls=[dict(pc=i.address,target=i.op_str,instruction=i.mnemonic) for i in ins if i.mnemonic in ('jal','bal','jr','jalr')]
 (out/'boot-callgraph.json').write_text(json.dumps(calls,indent=2))
 # Conservative local constant propagation, clearing at control-flow boundaries.
 constants={};refs=[]
 for i in ins:
  w=int.from_bytes(i.bytes,'little');op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;imm=w&65535;simm=imm if imm<32768 else imm-65536
  if op==15:constants[rt]=imm<<16
  elif op in (9,13) and rs in constants:constants[rt]=((constants[rs]+simm) if op==9 else constants[rs]|imm)&0xffffffff
  elif op in (32,33,35,36,37,40,41,43) and rs in constants:
   addr=(constants[rs]+simm)&0xffffffff
   if 0xbf000000<=addr<0xbf400000:refs.append(dict(pc=i.address,address=addr,physical=addr&0x1fffffff,instruction=f'{i.mnemonic} {i.op_str}',meaning='unclassified direct MMIO reference'))
   if op<40:constants.pop(rt,None)
  elif op not in (0,2,3,4,5,6,7):constants.pop(rt,None)
  if i.mnemonic in ('j','jal','bal','jr','jalr'):constants.clear()
 (out/'mmio-references.json').write_text(json.dumps(refs,indent=2))
 strings=[]
 for match in re.finditer(rb'[\x20-\x7e]{6,}',f[:0x23880]):strings.append(dict(offset=match.start(),text=match.group().decode()))
 (out/'boot-strings.json').write_text(json.dumps(strings,indent=2))
 diffs=[dict(offset=i,a=a,b=c) for i,(a,c) in enumerate(zip(f[0x7e0000:0x7f0000],f[0x7f0000:])) if a!=c]
 regions=[dict(start=0,end=0x2c60,kind='reset/initialization/loader code and tables; boundary from stock copy routine'),dict(start=0x2c60,end=0x85c0,kind='second-stage loader; stock copy targets 0x81000000'),dict(start=0x85c0,end=0x20000,kind='unclassified gap'),dict(start=0x20000,end=0x20080,kind='runtime loader header and padding'),dict(start=0x20080,end=0x23880,kind='uncompressed runtime prefix; guest loader copies to 0xA03B0180'),dict(start=0x360d1c,end=0x7b0000,kind='mostly erased; see entropy for exact blocks'),dict(start=0x7b0000,end=0x800000,kind='unclassified populated top sectors')]
 regions += [dict(start=s,end=e+4,kind='DEFLATE + length trailer',decompressed_bytes=n,sha256=h) for s,e,n,h in KNOWN]
 manifest=out/'manifest.json'
 if manifest.exists():
  for r in json.loads(manifest.read_text())['streams'][4:]:
   end,payload=inflate_at(f,r['start'])
   if (end,len(payload),sha(payload))!=(r['end'],r['length'],r['sha256']):raise ValueError('Additional manifest mismatch')
   regions.append(dict(start=r['start'],end=end+4,kind='additional trailer-validated DEFLATE; provenance pending',decompressed_bytes=len(payload),sha256=sha(payload)))
 source,destination,compressed_end,rom_end,entry,crc=struct.unpack_from('<6I',f,0x20000)
 report=dict(original_sha256=sha(b),flash_sha256=sha(f),size=len(b),isa='MIPS32 little-endian',reset_mapping='0xBFC00000 conventional; internally consistent control flow, physical wiring unverified',trailer=b[0x800000:].hex(),regions=regions,nvram_differences=diffs,runtime_header=dict(offset=0x20000,source=hex(source),destination=hex(destination),compressed_ram_end=hex(compressed_end),rom_end=hex(rom_end),entry=hex(entry),crc_msb_seed_zero=hex(crc)),decompressed_main_load_address='0x803B3980 (loader destination + 0x3800 prefix)')
 (out/'flash-map.json').write_text(json.dumps(report,indent=2))
 symbols={0xbfc00000:'reset_candidate',0xbfc0006c:'cp0_initialize',0xbfc00c00:'riu_masked_table_apply',0xbfc00c90:'riu_byte_table_apply',0xbfc01118:'early_board_initialize',0xbfc0271c:'copy_second_stage_to_81000000'}
 (out/'symbols.json').write_text(json.dumps({hex(k):v for k,v in symbols.items()},indent=2))
 (out/'ghidra_import.py').write_text('# Ghidra Jython script. Import RAW MIPS:LE:32:default at BFC00000 first.\n'+''.join(f'createLabel(toAddr(0x{k:x}), "{v}", True)\n' for k,v in symbols.items()))
 print(json.dumps(report,indent=2))
 print('RESET DISASSEMBLY\n'+'\n'.join(f'{i.address:08x}: {i.mnemonic} {i.op_str}' for i in ins[:28]))
 print('STATIC FLOW: reset -> CP0 -> RIU masked table -> MIU/cache setup -> stock copy [BFC02C60,BFC085C0) to A1000000 -> jump 81000000 -> BDMA CRC -> prefix + guest inflate -> 803B0600. Hardware polls are conditional; see traces for demonstrated reachability.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('firmware');p.add_argument('-o','--output',required=True);a=p.parse_args();analyze(a.firmware,a.output)
