"""xref.py BLOB BASE 'regex' : list strings matching regex with their addresses and the code locations that build that address (lui+addiu/ori)."""
import sys, re, struct
d = open(sys.argv[1], 'rb').read(); base = int(sys.argv[2], 0); pat = sys.argv[3].encode()
n = len(d) // 4; w = struct.unpack('<%dI' % n, d[:n * 4]); ref = {}
for i in range(n - 1):
    a = w[i]
    if a >> 26 != 0x0f: continue
    rt = (a >> 16) & 31; h = (a & 0xffff) << 16
    for j in range(i + 1, min(i + 8, n)):
        b = w[j]; op = b >> 26
        if op in (0x09, 0x0d) and ((b >> 21) & 31) == rt:
            imm = b & 0xffff
            if op == 0x09 and imm & 0x8000: imm -= 0x10000
            ref.setdefault((h + imm) & 0xffffffff, []).append(base + 4 * i); break
for m in re.finditer(rb'[\x20-\x7e]{4,}', d):
    if re.search(pat, m.group()):
        a = base + m.start()
        print('%08x  %r  refs: %s' % (a, m.group()[:70].decode(), ' '.join('%08x' % r for r in ref.get(a, [])[:6])))
