"""Recover only the original MIPS's observed IDMA words from a native trace.

The 24-bit packing is proven by 804FF690/804FF49C. It is not an ISA
identification. Output contains private original firmware; do not redistribute.
"""
import argparse, hashlib, json, struct, zlib
from pathlib import Path

def extract(stock, trace, directory):
    assert len(stock)==0x800010 and hashlib.sha256(stock).hexdigest()=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
    runtime=zlib.decompress(stock[0x23880:0x16124e],-15)
    assert hashlib.sha256(runtime).hexdigest()=='149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236'
    ports=[{'address':None,'phase':[],'words':{},'segments':[]} for _ in range(2)]
    for line in trace:
        if b'"kind":"write"' not in line or not any(x in line for x in (b'"address":"0x1f225a',b'"address":"0x1f225c')):continue
        e=json.loads(line);a=int(e['address'],16);v=int(e['value'],16)
        base=0x1f225c00 if a>=0x1f225c00 else 0x1f225a00;p=ports[base==0x1f225c00];off=a-base
        if off==8 and e['width']==2:
            assert not p['phase'],'new IDMA address inside an unfinished word'
            assert e['pc']=='0x804f9150','unexpected address producer; investigate original caller'
            p['address']=v;p['segments'].append({'start':v,'tick':e['tick'],'pc':e['pc'],'bytes':bytearray()})
        if off in (4,5) and e['width']==1:
            assert e['pc']=='0x804f91d4','unexpected data producer; investigate original caller'
            assert p['address'] is not None and off==(4,5,4,5)[len(p['phase'])]
            p['phase'].append(v)
            if len(p['phase'])==4:
                middle,high,low,padding=p['phase'];assert padding==0
                word=bytes((low,middle,high));p['words'][p['address']]=word
                p['segments'][-1]['bytes'].extend(word);p['address']=(p['address']+1)&65535;p['phase']=[]
    directory.mkdir(parents=True,exist_ok=True);manifest=[]
    for i,p in enumerate(ports):
        assert not p['phase'],'trace stops inside an unfinished port word'
        state=b''.join(struct.pack('<H',a)+v for a,v in sorted(p['words'].items()))
        path=directory/f'port-{i}-addressed-words.bin';path.write_bytes(state)
        segments=[]
        for n,s in enumerate(p['segments']):
            b=bytes(s.pop('bytes'));f=directory/f'port-{i}-segment-{n}-{s["start"]:04x}.bin';f.write_bytes(b)
            segments.append({**s,'bytes':len(b),'words':len(b)//3,'sha256':hashlib.sha256(b).hexdigest(),'path':f.name,
                             'first_words_le24':[f'{int.from_bytes(b[k:k+3],"little"):06x}' for k in range(0,min(len(b),48),3)]})
        manifest.append({'port':i,'initialized_words':len(p['words']),'addressed_memory_sha256':hashlib.sha256(state).hexdigest(),'segments':segments})
    result={'scope':'original guest-uploaded addressed 24-bit words; auxiliary ISA unconfirmed','ports':manifest}
    (directory/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser()
    for n in ('firmware','trace','out'):p.add_argument('--'+n,required=True)
    a=p.parse_args()
    with Path(a.trace).open('rb') as trace:r=extract(Path(a.firmware).read_bytes(),trace,Path(a.out))
    print(json.dumps(r,indent=2))
