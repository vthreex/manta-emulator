"""Blue Screen setting: guest state and the opt-in presenter plane, driven only by virtual remote input from the NO SIGNAL checkpoint.

Needs work/checkpoints/no-signal-ready-v2.snap (see docs/save-state.md); SKIPS (exit 0, result SKIP) when it is absent.
Original menu path: MENU 0xFC, RIGHT 0x11 x2 (Setup), DOWN 0x0E x2 (Blue Screen), OK 0x0D toggles. INFO 0x16 leaves a region with no GOP window.
Checks (all from guest memory / frames, nothing host-forced):
  * the setting is bit 4 of 0x806FBD85 (word 0x806FBD84 bit 12): 1 initially (On), 0 after one OK (Off)
  * 0x806FBD50 is a CHECKSUM halfword rewritten by 0x8047424C, not the setting
  * --exploratory-video-plane setting paints the uncovered INFO region blue when On and black when Off
"""
import argparse, json, os, struct, subprocess, sys, tempfile, threading
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--firmware', required=True)
p.add_argument('--emulator', required=True)
p.add_argument('--report', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
repo = root.parents[1]
snap = repo / 'work/checkpoints/no-signal-ready-v2.snap'
if not snap.exists():
    Path(a.report).write_text(json.dumps({'result': 'SKIP', 'reason': 'checkpoint absent'}))
    print('SKIP: checkpoint absent')
    sys.exit(0)
base = json.load(open(root / 'configs/checkpoints/connected-menu-base.json'))['argv']
exe = str(Path(a.emulator).resolve())
INJECT = 1300000000
nav = [(0, 0xfc), (8000000, 0x11), (16000000, 0x11), (26000000, 0x0e), (36000000, 0x0e)]
results = {}


def run(tag, keys, tmp, window, extra):
    d = Path(tmp) / tag
    (d / 'hist').mkdir(parents=True)
    cmd = [exe] + base + ['--headless', '--summary', str(d / 's.json'), '--load-state', str(snap), '--max-instructions', str(INJECT + window),
                          '--ir-script', ','.join('%d:raw=0x%02x' % (INJECT + t, k) for t, k in keys), '--gop-frame-history', str(d / 'hist'),
                          '--dump-guest-range', '6fbd40+60', '--dump-prefix', str(d / 'ram')] + extra
    subprocess.run(cmd, cwd=repo, capture_output=True)
    ram = (d / 'ram_0x6fbd40.bin').read_bytes()
    frames = json.load(open(d / 'hist/index.json'))['frames']
    bmp = (d / 'hist' / frames[-1]['file']).read_bytes()
    w = struct.unpack_from('<i', bmp, 18)[0]
    off = struct.unpack_from('<I', bmp, 10)[0]
    px = bmp[off:off + 4]                    # top-left pixel, BGRA (top-down 32-bit BMP)
    results[tag] = {'fbd85': ram[0x45], 'fbd50': ram[0x10], 'topleft_bgra': list(px), 'frames': len(frames), 'covered_last': frames[-1]['covered_pixels']}


with tempfile.TemporaryDirectory(prefix='manta-bs-') as tmp:
    plane = ['--exploratory-video-plane', 'setting']
    ths = [threading.Thread(target=run, args=('on_info', [(0, 0x16)], tmp, 16000000, plane)),
           threading.Thread(target=run, args=('off_info', nav + [(48000000, 0x0d), (62000000, 0x16)], tmp, 85000000, plane)),
           threading.Thread(target=run, args=('menu_only', nav, tmp, 45000000, []))]
    [t.start() for t in ths]; [t.join() for t in ths]
on, off, menu = results['on_info'], results['off_info'], results['menu_only']
assert menu['fbd85'] & 0x10, 'Blue Screen must start On (bit 4 of 0x806FBD85): %r' % menu
assert not (off['fbd85'] & 0x10), 'one OK on Blue Screen must clear bit 4: %r' % off
assert on['fbd85'] & 0x10
assert off['fbd50'] != on['fbd50'], 'the checksum byte must change with the setting (it is not the setting itself)'
assert on['topleft_bgra'][:3] == [255, 0, 0] and on['topleft_bgra'][3] == 255, 'uncovered INFO region must be the blue plane when On: %r' % on
assert off['topleft_bgra'][:3] == [0, 0, 0] and off['topleft_bgra'][3] == 255, 'uncovered INFO region must be the black plane when Off: %r' % off
assert on['covered_last'] < 400000 and off['covered_last'] < 400000
Path(a.report).write_text(json.dumps({'result': 'PASS', 'runs': results}, indent=1))
print('PASS: Blue Screen is bit 4 of 0x806FBD85 (0x806FBD50 is the checksum); the exploratory plane follows it (blue On, black Off)')
