import sys, re, struct, numpy as np
d = open(sys.argv[1], 'rb').read()
strs = np.array(sorted(m.start() for m in re.finditer(rb'(?<=\x00)[\x20-\x7e]{6,}\x00', d)), dtype=np.int64)
n = len(d) // 4; w = struct.unpack('<%dI' % n, d[:n * 4]); tg = set()
for i in range(n - 1):
    a = w[i]
    if a >> 26 != 0x0f: continue
    rt = (a >> 16) & 31; h = (a & 0xffff) << 16
    for j in range(i + 1, min(i + 6, n)):
        b = w[j]; op = b >> 26
        if op in (0x09, 0x0d) and ((b >> 21) & 31) == rt:
            imm = b & 0xffff
            if op == 0x09 and imm & 0x8000: imm -= 0x10000
            tg.add((h + imm) & 0xffffffff); break
tg = np.array(sorted(t for t in tg if 0x80000000 <= t < 0xA0000000), dtype=np.int64)
SPAN = 0x01000000; LO = 0x80000000
acc = np.zeros(SPAN, dtype=np.int32)
for k in range(0, len(tg), 64):
    dl = (tg[k:k+64, None] - strs[None, :]).ravel() - LO
    dl = dl[(dl >= 0) & (dl < SPAN)]
    acc += np.bincount(dl, minlength=SPAN).astype(np.int32)
top = np.argsort(acc)[::-1][:8]
print('targets', len(tg), [(hex(LO + int(t)), int(acc[t])) for t in top])
