#!/usr/bin/env python3
"""Extract only end-marked raw DEFLATE streams with matching length trailers."""
import argparse, hashlib, json, struct, zlib
from pathlib import Path

EXPECTED = 'd60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
KNOWN = [(0x23880,0x16124e,3167072,'149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236'),
 (0x161258,0x1bd538,731981,'57d6a9f0b909616105e99b8b447325621b0e8dfc63a7fe27aab64d2d10e65e49'),
 (0x1bd548,0x1e65b7,357005,'95dad91784af417a38223dc19a0a33f0793b616933acf72782f955a0ca131343'),
 (0x1e65c0,0x1e782b,8448,'f6766d55f073349026047373d17a2b6ca520a128557e2afed0a938169728ca84')]
def sha(b): return hashlib.sha256(b).hexdigest()
def validate(b):
 if len(b)!=0x800010 or sha(b)!=EXPECTED: raise ValueError('Wrong original firmware size or SHA256; refusing sample-specific interpretation')
 return b[:0x800000]
def inflate_at(b,start,limit=32*1024*1024):
 d=zlib.decompressobj(-15)
 out=d.decompress(b[start:],limit+1)
 if len(out)>limit or not d.eof or d.unconsumed_tail: raise ValueError('Unterminated or oversized DEFLATE')
 end=len(b)-len(d.unused_data)
 if end+4>len(b) or struct.unpack_from('<I',b,end)[0]!=len(out): raise ValueError('Missing/mismatching length trailer')
 return end,out
def extract(path,outdir,scan=False):
 original=Path(path).read_bytes(); b=validate(original); outdir=Path(outdir);outdir.mkdir(parents=True,exist_ok=True)
 (outdir/'spi_8mib_readonly_copy.bin').write_bytes(b)
 (outdir/'programmer-trailer.bin').write_bytes(original[0x800000:])
 records=[]
 for start,end,length,digest in KNOWN:
  found,payload=inflate_at(b,start)
  if (found,len(payload),sha(payload))!=(end,length,digest):raise ValueError('Known stream mismatch')
  name=f'stream-{start:06x}.bin';(outdir/name).write_bytes(payload)
  records.append(dict(start=start,end=end,length=len(payload),sha256=sha(payload),file=name,classification='MIPS code' if start==0x23880 else 'unclassified payload'))
 if scan:
  # Scan every byte in the remaining populated range, skipping known streams.
  # Matching a trailer is mandatory; tiny false streams are excluded.
  pos=0x1e7830;stop=0x360d1c
  while pos<stop:
   if ((b[pos]>>1)&3) not in (1,2):pos+=1;continue
   try:end,payload=inflate_at(b,pos,16*1024*1024)
   except (ValueError,zlib.error):pos+=1;continue
   if len(payload)<64:pos+=1;continue
   name=f'stream-{pos:06x}.bin';(outdir/name).write_bytes(payload)
   records.append(dict(start=pos,end=end,length=len(payload),sha256=sha(payload),file=name,classification='trailer-validated payload; loader provenance pending'))
   print(f'ADDITIONAL {pos:#x}..{end:#x} -> {len(payload)}',flush=True);pos=end+4
 report=dict(original_sha256=sha(original),original_bytes=len(original),flash_sha256=sha(b),flash_bytes=len(b),metadata_hex=original[0x800000:].hex(),streams=records)
 (outdir/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
 for r in records: print(f'{r["start"]:#08x}..{r["end"]:#08x} -> {r["length"]} sha256={r["sha256"]}')
 return report
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('firmware');p.add_argument('-o','--output',required=True);p.add_argument('--scan',action='store_true');a=p.parse_args();extract(a.firmware,a.output,a.scan)
