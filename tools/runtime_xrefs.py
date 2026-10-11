"""Reproducible, conservative runtime string/call/MMIO cross references.

Function starts are prologue candidates, not symbols asserted without review.
Original input and decompression hashes are mandatory. No firmware is exported.
"""
import argparse, bisect, hashlib, json, re, struct
from pathlib import Path
from firmware_extract import validate, inflate_at, KNOWN

BASE=0x803b0180
def analyze(path, summary=None):
    flash=validate(Path(path).read_bytes()); _,runtime=inflate_at(flash,KNOWN[0][0])
    if hashlib.sha256(runtime).hexdigest()!=KNOWN[0][3]:raise ValueError('runtime hash mismatch')
    image=flash[0x20080:0x23880]+runtime
    words=list(struct.unpack('<%dI'%(len(image)//4),image))
    def word(pc):return words[(pc-BASE)//4]
    code_end=0x80613000 # reviewed code/data boundary; not an executable-section claim
    prologues=[BASE+4*i for i,w in enumerate(words[:(code_end-BASE)//4])
               if w&0xffff0000==0x27bd0000 and w&0x8000]
    def function(pc):
        i=bisect.bisect_right(prologues,pc)-1
        return prologues[i] if i>=0 else BASE
    strings={BASE+m.start():m.group().decode('ascii') for m in re.finditer(rb'[ -~]{6,}\x00',image)}
    strings={k:v.rstrip('\x00') for k,v in strings.items()
             if any(s in v.lower() for s in ['gop','osd','lvds','ge_','gfx','panel','bmtest','miu','rtc'])}
    refs=[]; calls=[]; mmio=[]; known={0:0}; reset_after=None
    for pc in range(BASE,code_end,4):
        w=word(pc); op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31;fn=w&63; imm=w&65535; signed=imm-(65536 if imm&32768 else 0)
        val=None
        if op==15:val=imm<<16
        elif op==13 and rs in known:val=known[rs]|imm
        elif op==12 and rs in known:val=known[rs]&imm
        elif op==9 and rs in known:val=(known[rs]+signed)&0xffffffff
        elif op==0 and fn in [33,37] and rs in known and rt in known:
            val=(known[rs]+known[rt])&0xffffffff if fn==33 else known[rs]|known[rt];rt=rd
        elif op in [32,33,34,35,36,37,38,40,41,42,43,46] and rs in known:
            addr=(known[rs]+signed)&0xffffffff
            if 0xbf000000<=addr<0xbf400000: mmio.append(dict(pc=hex(pc),address=hex(addr),opcode=hex(w),function_candidate=hex(function(pc))))
            if op==35 and addr==0x806b8d0c:val=0xbf000000 # verified runtime RIU base
        if val is not None:
            known[rt]=val
            if val in strings:refs.append(dict(pc=hex(pc),string_address=hex(val),text=strings[val],function_candidate=hex(function(pc))))
        elif op in [8,9,10,11,12,13,14,15,32,33,34,35,36,37,38]:known.pop(rt,None)
        elif op==0 and fn not in [8,9,12,13,15,24,25,26,27,17,19]:known.pop(rd,None)
        if op==3:calls.append(dict(pc=hex(pc),caller_candidate=hex(function(pc)),target=hex((pc&0xf0000000)|((w&0x3ffffff)<<2))))
        # Never propagate constants across a branch/call or its delay slot.
        if reset_after==pc:known={0:0};reset_after=None
        if op in [1,2,3,4,5,6,7,20,21,22,23] or (op==0 and fn in [8,9]):reset_after=pc+4
        known[0]=0
    from capstone import Cs,CS_ARCH_MIPS,CS_MODE_MIPS32,CS_MODE_LITTLE_ENDIAN
    cs=Cs(CS_ARCH_MIPS,CS_MODE_MIPS32|CS_MODE_LITTLE_ENDIAN)
    ranges=[(0x804697d4,0x804698e4),(0x805482d8,0x80548528),(0x805cb6d4,0x805cb710),(0x805cbbd8,0x805cbc20)]
    report=dict(runtime_sha256=KNOWN[0][3],base=hex(BASE),string_refs=refs,direct_calls=calls,constant_mmio_candidates=mmio,
                disassembly=[dict(start=hex(s),instructions=[dict(pc=hex(i.address),word=hex(word(i.address)),instruction=i.mnemonic+' '+i.op_str)
                      for i in cs.disasm(image[s-BASE:e-BASE],s)]) for s,e in ranges],
                caveat='Straight-line constants and nearest stack prologue are candidates; indirect callers and path-sensitive dataflow require manual validation.')
    if summary:
        s=json.loads(Path(summary).read_text())
        report['recent_execution']=[dict(**i,disassembly=next(cs.disasm(struct.pack('<I',int(i['word'],16)),int(i['pc'],16))).mnemonic+' '+next(cs.disasm(struct.pack('<I',int(i['word'],16)),int(i['pc'],16))).op_str) for i in s['recent_instructions']]
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--firmware',required=True);p.add_argument('--summary');p.add_argument('--report',required=True);a=p.parse_args()
    r=analyze(a.firmware,a.summary);Path(a.report).write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8')
    print(f"{len(r['string_refs'])} reviewed-keyword string references; {len(r['constant_mmio_candidates'])} MMIO candidates")
