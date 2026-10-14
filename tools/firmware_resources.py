"""Recover the original 11-byte resource directory used by MIPS 804F0AF4.

Read-only stock input; optional private extraction. Resource IDs are firmware
metadata, not an inference that each payload is code or a recognizable file.
"""
import argparse, hashlib, json, os, struct, zlib
from pathlib import Path

STOCK='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
def sha(b):return hashlib.sha256(b).hexdigest()
def protect_output(source,target):
    if Path(source).resolve()==Path(target).resolve() or (Path(target).exists() and os.path.samefile(source,target)):
        raise ValueError('refusing to overwrite original firmware through an output alias')
def recover(stock):
    if len(stock)!=0x800010 or sha(stock)!=STOCK:raise ValueError('requires immutable original Manta dump')
    table=0x20080+(0x803b2000-0x803b0180)
    count=int.from_bytes(stock[table+12:table+14],'big')
    if count!=13:raise ValueError('unexpected original resource directory extent')
    manifest=[];payloads={}
    for index in range(count):
        rid,source,extent,kind=struct.unpack_from('>HIIB',stock,table+14+11*index)
        # Original guest rounds resource extents up to eight bytes.
        rounded=(extent+7)&~7
        if source+rounded>0x800000:raise ValueError('resource extends past physical NOR capacity')
        block=stock[source:source+extent]
        row={'id':hex(rid),'table_entry_flash':hex(table+14+11*index),'source_flash':hex(source),
             'stored_extent':extent,'guest_rounded_extent':rounded,'type':kind,'stored_sha256':sha(block)}
        if kind==4:
            decoder=zlib.decompressobj(-15)
            decoded=decoder.decompress(block,32*1024*1024)
            if not decoder.eof or decoder.unconsumed_tail:raise ValueError('incomplete or oversized DEFLATE resource')
            consumed=len(block)-len(decoder.unused_data)
            if consumed+4>len(block):raise ValueError('missing original uncompressed-length trailer')
            declared=struct.unpack_from('<I',block,consumed)[0]
            if declared!=len(decoded):raise ValueError('resource DEFLATE trailer length mismatch')
            row.update(deflate_bytes=consumed,trailer_flash=hex(source+consumed),
                       decoded_bytes=len(decoded),decoded_sha256=sha(decoded),
                       trailing_padding_bytes=len(block)-consumed-4)
        elif kind==0:decoded=block
        else:raise ValueError('unidentified original storage type')
        manifest.append(row);payloads[rid]=decoded
    return {'stock_sha256':STOCK,'directory_flash':hex(table),'original_MIPS_lookup':'0x804F0AF4',
            'entry_bytes':11,'count':count,'resources':manifest},payloads

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--firmware',required=True);p.add_argument('--manifest',required=True)
    p.add_argument('--extract-id',action='append',type=lambda n:int(n,0),default=[])
    p.add_argument('--private-output',type=Path)
    a=p.parse_args()
    protect_output(a.firmware,a.manifest)
    manifest,payloads=recover(Path(a.firmware).read_bytes())
    if a.extract_id and not a.private_output:p.error('--extract-id requires explicit private-output directory')
    targets=[]
    for rid in a.extract_id:
        if rid not in payloads:p.error('resource ID absent from original directory')
        target=a.private_output/f'resource-{rid:04x}.bin'
        protect_output(a.firmware,target)
        protect_output(a.manifest,target) if Path(a.manifest).exists() else None
        if target.resolve()==Path(a.manifest).resolve():raise ValueError('manifest and extracted payload must be separate')
        if target.exists() and target.read_bytes()!=payloads[rid]:raise ValueError('refusing to replace different extraction')
        targets.append((rid,target))
    for rid,target in targets:
        a.private_output.mkdir(parents=True,exist_ok=True)
        target.write_bytes(payloads[rid])
    Path(a.manifest).write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'PASS: {manifest["count"]} original resources, DEFLATE endpoints/trailers and physical bounds verified')
if __name__=='__main__':main()
