"""Unpack every compressed stream (LZMA-alone, zlib/gzip) out of a Sunplus STV flash image and list it.
Usage: python unpack.py STV-24LED14.BIN OUTDIR"""
import sys, os, lzma, zlib, json
img = open(sys.argv[1], 'rb').read()
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
found = []
n = len(img)
i = 0
# LZMA-alone: props byte 0x5d, dict size, 8-byte uncompressed size (here a real size)
while i < n - 13:
    if img[i] == 0x5d and img[i+1:i+3] == b'\x00\x00' and img[i+8:i+13] == b'\x00\x00\x00\x00\x00':
        d = lzma.LZMADecompressor(lzma.FORMAT_ALONE)
        try:
            r = d.decompress(img[i:i + 0x400000])
        except Exception:
            r = b''
        if d.eof and len(r) > 1024:
            used = len(img[i:i + 0x400000]) - len(d.unused_data)
            name = 'lzma_%06x.bin' % i
            open(os.path.join(out, name), 'wb').write(r)
            found.append(dict(kind='lzma', offset=i, packed=used, size=len(r), file=name))
            i += used
            continue
    i += 1
# zlib streams (78 9c / 78 da / 78 01) and gzip
i = 0
while i < n - 6:
    if img[i] == 0x78 and img[i+1] in (0x9c, 0xda, 0x01, 0x5e) and (img[i] * 256 + img[i+1]) % 31 == 0:
        d = zlib.decompressobj()
        try:
            r = d.decompress(img[i:i + 0x200000])
        except Exception:
            r = b''
        if d.eof and len(r) > 256:
            used = len(img[i:i + 0x200000]) - len(d.unused_data)
            name = 'zlib_%06x.bin' % i
            open(os.path.join(out, name), 'wb').write(r)
            found.append(dict(kind='zlib', offset=i, packed=used, size=len(r), file=name))
            i += used
            continue
    i += 1
found.sort(key=lambda x: x['offset'])
json.dump(found, open(os.path.join(out, 'index.json'), 'w'), indent=1)
for f in found:
    print('%-5s %06x packed %7d -> %9d  %s' % (f['kind'], f['offset'], f['packed'], f['size'], f['file']))
