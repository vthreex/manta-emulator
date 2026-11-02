"""fnstrings.py BLOB BASE ADDR [SPAN]: strings referenced (lui+addiu/ori) in [ADDR, ADDR+SPAN) of a flat MIPS blob."""
import sys, re, struct
d = open(sys.argv[1], 'rb').read(); base = int(sys.argv[2], 0); a0 = int(sys.argv[3], 0); span = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x300
n = len(d) // 4; w = struct.unpack('<%dI' % n, d[:n * 4])
S = {m.start() + base: m.group() for m in re.finditer(rb'[\x20-\x7e]{5,}', d)}
for i in range((a0 - base) // 4, min(n, (a0 + span - base) // 4)):
    a = w[i]
    if a >> 26 != 0x0f: continue
    rt = (a >> 16) & 31; h = (a & 0xffff) << 16
    for j in range(i + 1, min(i + 6, n)):
        b = w[j]; op = b >> 26
        if op in (9, 13) and ((b >> 21) & 31) == rt:
            imm = b & 0xffff; imm -= 0x10000 if imm & 0x8000 else 0
            t = (h + imm) & 0xffffffff
            if t in S: print('%08x -> %08x %s' % (base + i * 4, t, S[t][:80].decode()))
            break
