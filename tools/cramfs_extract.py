# Minimal cramfs extractor for the nBox/ADB dump (little-endian, handles >16 MiB files whose 24-bit size wrapped). Usage: cramfs_extract.py IMAGE BASE_OFFSET DEST_DIR
import struct, sys, os, zlib, json

# Minimal cramfs extractor (little-endian). Names are sanitised; nothing from the image is executed.
src, base, dest = sys.argv[1], int(sys.argv[2], 0), sys.argv[3]
raw = open(src, 'rb').read()
img = raw[base:]
magic, size, flags, future = struct.unpack_from('<IIII', img, 0)
assert magic == 0x28cd3d45, hex(magic)
sig = img[16:32]
crc, edition, blocks, files = struct.unpack_from('<IIII', img, 32)
name = img[48:64].split(b'\0')[0]
print('size', size, 'flags', hex(flags), 'sig', sig, 'edition', edition, 'blocks', blocks, 'files', files, 'name', name)
os.makedirs(dest, exist_ok=True)
stats = {'files': 0, 'dirs': 0, 'links': 0, 'bytes': 0, 'bad': 0}
index = []


def inode(off):
    mode_uid, size_gid, nameoff = struct.unpack_from('<HHI', img, off)[0], None, None
    mode, uid = struct.unpack_from('<HH', img, off)
    w = struct.unpack_from('<I', img, off + 4)[0]
    fsize = w & 0xffffff
    gid = w >> 24
    w2 = struct.unpack_from('<I', img, off + 8)[0]
    namelen = (w2 & 0x3f) * 4
    offset = (w2 >> 6) * 4
    return mode, uid, fsize, gid, namelen, offset


def read_file(offset, fsize):
    # the size field is 24 bits: a file over 16 MiB wraps. Pick the wrap count whose block table puts block 0 inside [0, 4400] bytes.
    for k in range(0, 4):
        fs2 = fsize + k * 0x1000000
        nb = (fs2 + 4095) // 4096
        if not nb:
            break
        first = struct.unpack_from('<I', img, offset)[0] & 0x3fffffff
        gap = first - (offset + 4 * nb)
        if 0 <= gap <= 4400:
            fsize = fs2
            break
    nblk = (fsize + 4095) // 4096
    out = bytearray()
    ptrs = struct.unpack_from('<%dI' % nblk, img, offset) if nblk else ()
    start = offset + 4 * nblk
    for p in ptrs:
        end = p & 0x3fffffff
        chunk = img[start:end]
        start = end
        if not chunk:
            out += b'\0' * 4096
            continue
        try:
            out += chunk if (p & 0x80000000) else zlib.decompress(chunk)
        except Exception:
            stats['bad'] += 1
            out += b'\0' * 4096
    return bytes(out[:fsize])


def walk(off, size, path):
    pos = off
    end = off + size
    while pos < end:
        mode, uid, fsize, gid, namelen, offset = inode(pos)
        nm = img[pos + 12:pos + 12 + namelen].split(b'\0')[0].decode('latin1')
        nm = nm.replace('/', '_').replace('\\', '_')
        pos += 12 + namelen
        full = os.path.join(path, nm)
        fmt = mode & 0o170000
        if fmt == 0o040000:
            stats['dirs'] += 1
            os.makedirs(full, exist_ok=True)
            index.append(('d', full[len(dest):], 0, mode))
            if fsize:
                walk(offset, fsize, full)
        elif fmt == 0o100000:
            data = read_file(offset, fsize) if fsize else b''
            open(full, 'wb').write(data)
            stats['files'] += 1
            stats['bytes'] += len(data)
            index.append(('f', full[len(dest):], fsize, mode))
        elif fmt == 0o120000:
            data = read_file(offset, fsize)
            open(full + '.symlink', 'wb').write(data)
            stats['links'] += 1
            index.append(('l', full[len(dest):], data.decode('latin1'), mode))
        else:
            index.append(('o', full[len(dest):], fsize, mode))


rmode, ruid, rsize, rgid, rnl, roff = inode(64)
print('root mode', oct(rmode), 'size', rsize, 'offset', roff)
walk(roff, rsize, dest)
print(stats)
json.dump(index, open(dest + '.index.json', 'w'))
