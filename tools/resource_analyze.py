"""Decode a structurally validated MST type-2 string table; reject loose signatures."""
import argparse,json,struct,hashlib
from pathlib import Path
def decode_mst_strings(blob):
 if len(blob)<0x44 or blob[:4]!=b'MST\0':raise ValueError('Not an MST container')
 start,size,kind=struct.unpack_from('>III',blob,4)
 if kind!=2 or start+size!=len(blob):raise ValueError('Unsupported type or inconsistent container length')
 count=struct.unpack_from('>I',blob,0x40)[0]
 if not 1<=count<=128 or 0x44+count*4>start:raise ValueError('Invalid table count')
 tables=struct.unpack_from('>'+str(count)+'I',blob,0x44)
 result=[]
 for language,table in enumerate(tables):
  if table<0x44+count*4 or table+4>start:raise ValueError('Table out of bounds')
  entries=struct.unpack_from('>I',blob,table)[0]
  if entries>10000 or table+4+entries*4>start:raise ValueError('Index array out of bounds')
  offsets=struct.unpack_from('>'+str(entries)+'I',blob,table+4);strings=[]
  for offset in offsets:
   if offset%2 or offset>=size:raise ValueError('Unaligned/out-of-bounds string')
   pos=start+offset;end=pos
   while end+2<=len(blob) and blob[end:end+2]!=b'\0\0':end+=2
   if end+2>len(blob):raise ValueError('Unterminated UTF16 string')
   strings.append(blob[pos:end].decode('utf-16le',errors='strict'))
  result.append(dict(language_index=language,index_table_offset=table,entry_count=entries,strings=strings))
 return dict(magic='MST\\0',kind=kind,data_offset=start,data_bytes=size,languages=result,sha256=hashlib.sha256(blob).hexdigest())
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('payload');p.add_argument('-o','--output',required=True);a=p.parse_args();r=decode_mst_strings(Path(a.payload).read_bytes());Path(a.output).write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8');print(f'Validated {len(r["languages"])} language tables; {r["languages"][0]["entry_count"]} string IDs each');print(r['languages'][0]['strings'][:12])
