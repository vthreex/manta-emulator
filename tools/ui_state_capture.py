"""Capture the guest-controlled UI states (screenshots + framebuffer checksums + GOP/Blue Screen state) from the NO SIGNAL checkpoint.

usage: ui_state_capture.py --emulator EXE --out DIR
Each state is a fresh process loading the same snapshot and receiving real virtual-remote bytes. Records: active GOP windows, guest Blue Screen byte
(0x806FBD85 bit 4), SHA-256 of both guest framebuffers (0xDDD880 / 0xFDB880), the native-preview checksum, the exact exploratory options, and a PNG.
"""
import argparse, hashlib, json, os, struct, subprocess, sys, threading, zlib
from pathlib import Path

root = Path(__file__).resolve().parents[1]
repo = root.parents[1]
p = argparse.ArgumentParser()
p.add_argument('--emulator', required=True)
p.add_argument('--out', required=True)
a = p.parse_args()
out = Path(a.out).resolve(); out.mkdir(parents=True, exist_ok=True)
base = json.load(open(root / 'configs/checkpoints/connected-menu-base.json'))['argv']
exe = str(Path(a.emulator).resolve())
snap = repo / 'work/checkpoints/no-signal-ready-v2.snap'
INJECT = 1300000000
nav = [(0, 0xfc), (8000000, 0x11), (16000000, 0x11)]
toggle = [(26000000, 0x0e), (36000000, 0x0e), (48000000, 0x0d)]
OPTS = ['--exploratory-video-plane', 'setting', '--exploratory-gop-black-colorkey']
states = {
    '1_no_signal_blue_on': ([], 6000000),
    '2_no_signal_blue_off': (nav + toggle + [(62000000, 0x20)], 110000000),
    '3_info_blue_on': ([(0, 0x16)], 40000000),
    '4_info_blue_off': (nav + toggle + [(62000000, 0x16)], 85000000),
    '5_menu_setup_blue_on': (nav, 30000000),
    '6_menu_setup_blue_off': (nav + toggle, 65000000),
    '7_language_popup': ([(0, 0xfc), (8000000, 0x11), (16000000, 0x11), (28000000, 0x0d)], 45000000),
}


def png(path, w, h, rows):
    def ch(t, c): return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    raw = b''.join(b'\0' + r for r in rows)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw, 6)) + ch(b'IEND', b''))


def bmp_to_png(src, dst, scale=2):
    d = Path(src).read_bytes(); off = struct.unpack_from('<I', d, 10)[0]; w = struct.unpack_from('<i', d, 18)[0]; h = abs(struct.unpack_from('<i', d, 22)[0])
    rows = []
    for y in range(0, h, scale):
        r = bytearray()
        for x in range(0, w, scale):
            i = off + (y * w + x) * 4; r += bytes((d[i + 2], d[i + 1], d[i]))
        rows.append(bytes(r))
    png(dst, w // scale, len(rows), rows)


report = {}


def run(name, keys, window, extra=(), switch=False):
    d = out / name; (d / 'hist').mkdir(parents=True, exist_ok=True)
    cmd = [exe] + base + ['--headless', '--summary', str(d / 's.json'), '--load-state', str(snap), '--load-state-allow-extra-options', '--max-instructions', str(INJECT + window),
                          '--gop-frame-history', str(d / 'hist'), '--dump-guest-range', 'ddd880+1fe000', '--dump-guest-range', 'fdb880+1fe000',
                          '--dump-guest-range', '6fbd80+8', '--dump-prefix', str(d / 'ram')] + OPTS + list(extra)
    if keys: cmd += ['--ir-script', ','.join('%d:raw=0x%02x' % (INJECT + t, k) for t, k in keys)]
    subprocess.run(cmd, cwd=repo, capture_output=True)
    s = json.load(open(d / 's.json')); idx = json.load(open(d / 'hist/index.json'))['frames']; last = idx[-1]
    sha = lambda f: hashlib.sha256((d / f).read_bytes()).hexdigest()
    settings = (d / 'ram_0x6fbd80.bin').read_bytes()
    bmp_to_png(d / 'hist' / last['file'], out / (name + '.png'))
    report[name] = {'dispatches': s['instruction_dispatches'] - INJECT, 'stop': s['reason'], 'active_gop_windows': s['gop_active_descriptors'],
                    'blue_screen_bit4_of_806FBD85': bool(settings[5] & 0x10), 'fb_0xDDD880_sha256': sha('ram_0xddd880.bin'), 'fb_0xFDB880_sha256': sha('ram_0xfdb880.bin'),
                    'native_preview_checksum': last['checksum'], 'preview_covered_pixels': last['covered_pixels'], 'png': name + '.png',
                    'exploratory_options': OPTS + list(extra), 'keys': ['0x%02x@+%d' % (k, t) for t, k in keys]}


ths = [threading.Thread(target=run, args=(n, k, w)) for n, (k, w) in states.items()]
ths.append(threading.Thread(target=run, args=('8_source_av1_banner', [(0, 0x4e)], 40000000,
           ['--exploratory-provenance-extensions', '--exploratory-mmio-bits', '1f203fe1:01:01+rmw'])))
[t.start() for t in ths]; [t.join() for t in ths]
(out / 'ui-states.json').write_text(json.dumps(report, indent=1, sort_keys=True))
import shutil
for n in list(report):
    shutil.rmtree(out / n, ignore_errors=True)   # keep only the PNGs and the JSON (framebuffer dumps are large; their SHA-256 are recorded)
for k in sorted(report): print(k, report[k]['blue_screen_bit4_of_806FBD85'], report[k]['preview_covered_pixels'], report[k]['native_preview_checksum'])
