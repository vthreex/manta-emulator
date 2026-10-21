"""Virtual USB pendrive (EXPLORATORY, --exploratory-usb-pendrive): the original firmware enumerates a FAT32 stick and its media player browses it.

Needs work/checkpoints/no-signal-ready-v2.snap; SKIPS when absent. Builds a small FAT32 image on the fly (tools/make_pendrive_image.py), then sends
real remote keys: SOURCE 0x41, DOWN x5 (USB), OK, then OK on PHOTO and OK on drive C (spaced widely: the original browser polls keys slowly).
Checks (all from the original firmware's own behaviour, no patches): USB enumeration requests (GET_DESCRIPTOR device/config, SET_ADDRESS,
SET_CONFIGURATION, GET_MAX_LUN), BOT/SCSI traffic (INQUIRY, READ CAPACITY, READ(10) of the MBR, boot sector, FAT and root directory), the
run reaches its budget without a guarded stop, and a full-screen frame is produced after the drive page.
Hypotheses used (labelled, docs/usb-pendrive.md): controller/port model, chip-id mirror BF203D98 = 0x18 / BF203D9D = 0, GOP wait, audio indirect
registers, GE line model.
"""
import argparse, json, os, subprocess, sys, tempfile
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
keys = [(0, 0x41)] + [(14000000 * (i + 1), 0x0e) for i in range(5)] + [(14000000 * 6, 0x0d), (250000000, 0x0d), (330000000, 0x0d)]
with tempfile.TemporaryDirectory(prefix='manta-pen-', ignore_cleanup_errors=True) as tmp:
    d = Path(tmp); (d / 'hist').mkdir(); (d / 'content/PHOTO').mkdir(parents=True)
    for name in ('Gradient.jpg', 'Test_Pattern.jpg'):   # deterministic stand-in bytes (never decoded: the browser only lists them)
        (d / 'content/PHOTO' / name).write_bytes(bytes((i * 31 + len(name)) & 255 for i in range(3000)))
    subprocess.run([sys.executable, '-I', str(root / 'tools/make_pendrive_image.py'), str(d / 'pen.img'), str(d / 'content'), '--size-mib', '96'], check=True, capture_output=True)
    cmd = [exe] + base + ['--headless', '--summary', str(d / 's.json'), '--load-state', str(snap), '--load-state-allow-extra-options',
                          '--exploratory-provenance-extensions', '--exploratory-ge-line',
                          '--exploratory-mmio-bits', '1f203fe1:01:01+rmw', '--exploratory-mmio-bits', '1f225dd8:ff:00+rmw', '--exploratory-mmio-bits', '1f225cbc:ff:00+rmw',
                          '--exploratory-mmio-bits', '1f203d98:ff:18', '--exploratory-mmio-bits', '1f203d9d:ff:00',
                          '--exploratory-usb-pendrive', (d / 'pen.img').as_posix(),   # forward slashes: the summary echoes option text unescaped
                          
                          '--max-instructions', str(INJECT + 430000000), '--ir-script', ','.join('%d:raw=0x%02x' % (INJECT + t, k) for t, k in keys),
                          '--gop-frame-history', str(d / 'hist')]
    subprocess.run(cmd, cwd=repo, capture_output=True)
    s = json.load(open(d / 's.json'))
    frames = json.load(open(d / 'hist/index.json'))['frames']
assert s['reason'] == 'instruction budget exhausted', (s['reason'], s.get('stop_pc'), s.get('stop_address'))
u = s['exploratory_usb_pendrive']
trace = u['trace']
need = ['usb_pendrive_attach', 'usb_port_reset_done', 'SETUP 80 06 v=0100', 'SETUP 00 05', 'SETUP 80 06 v=0200', 'SETUP 00 09 v=0001', 'SETUP a1 fe', 'scsi 12 ', 'scsi 25 ', 'scsi 28 lba=']
for n in need:
    assert any(n in t for t in trace), ('missing in USB trace', n, trace[:12])
assert u['scsi_commands'] >= 20 and u['blocks_read'] >= 20, u
late = [f for f in frames if f['dispatches'] - INJECT > 300000000 and f['covered_pixels'] == 1044480]
assert late, [(f['dispatches'] - INJECT, f['covered_pixels']) for f in frames][-5:]
Path(a.report).write_text(json.dumps({'result': 'PASS', 'transactions': u['transactions'], 'scsi_commands': u['scsi_commands'], 'blocks_read': u['blocks_read'],
                                       'last_checksum': late[-1]['checksum'], 'frames_after_drive_page': len(late)}, indent=1))
print('PASS: the original firmware enumerates the virtual FAT32 pendrive, mounts it and browses it with real remote keys')
