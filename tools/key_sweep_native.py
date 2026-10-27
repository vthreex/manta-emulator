"""Single-key sweep from a saved machine state with the NATIVE engine (about 10 s per key instead of ~15 minutes).

For every raw IR byte: load SNAPSHOT, deliver the byte through the emulated PM IR FIFO shortly after the snapshot's dispatch count, run WINDOW more dispatches, keep
every distinct GOP preview frame and the original OSD-show calls (804C3684). Output: OUT/rNN/{summary.json,hist/*.bmp} and OUT/index.json; with --sheet a contact sheet PNG per
group of keys (needs PySide6) so the screens can be read by eye.

usage: key_sweep_native.py --snapshot S.snap --out DIR [--raws 0x0a,0x0b|all] [--window 70000000] [--parallel 6] [--sheet]
"""
import argparse, json, os, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'frontend'))
from emulator_session import REPO, Settings, build_command, emulator_candidates  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('--snapshot', required=True)
ap.add_argument('--out', required=True)
ap.add_argument('--raws', default='all')
ap.add_argument('--window', type=int, default=70000000)
ap.add_argument('--lead', type=int, default=20000000)
ap.add_argument('--parallel', type=int, default=6)
ap.add_argument('--emulator', default='')
ap.add_argument('--sheet', action='store_true')
ap.add_argument('--connected', action='store_true', help='use the connected-menu base configuration + the DTV hypotheses (interpreter) instead of the desktop profile (native)')
ap.add_argument('--watch', default='804c3684,8048710c,803ffe40')
a = ap.parse_args()

if a.connected:
    prog = a.emulator or str(REPO / 'work' / 'build' / 'manta.exe')
    args = json.load(open(HERE.parent / 'configs' / 'checkpoints' / 'connected-menu-base.json'))['argv'] + ['--load-state', str(Path(a.snapshot).resolve()), '--load-state-allow-extra-options',
            '--exploratory-provenance-extensions', '--exploratory-mmio-bits', '1f203fe1:01:01+rmw']
    for h in ('1f225dd8:ff:00+init', '1f225cbc:ff:00+init', '1f206728:ff:00+ack', '1f206729:ff:00+ack', '1f004074:ff:00+init', '1f004075:ff:00+init', '1f203600:ff:00+init',
              '1f203601:ff:00+init', '1f225cb0:ff:00+init', '1f205e78:ff:00+init@1', '1f205e79:f4:00+init@1'):
        args += ['--exploratory-mmio-bits', h]
    args += ['--max-instructions', '1']
else:
    st = Settings()
    st.emulator = a.emulator or emulator_candidates()[0]
    st.snapshot = str(Path(a.snapshot).resolve())
    prog, args = build_command(st, 'sweep')
    for flag in ('--frontend-shm', '--preview-interval-log2', '--preview-max-fps'):
        i = args.index(flag)
        del args[i:i + 2]
i = args.index('--max-instructions')
out = Path(a.out).resolve()
out.mkdir(parents=True, exist_ok=True)


def run_cmd(extra, budget):
    a2 = list(args)
    a2[a2.index('--max-instructions') + 1] = str(budget)
    return [prog] + a2 + extra


# dispatch count the snapshot resumes at
probe = out / 'probe.json'
subprocess.run(run_cmd(['--headless', '--summary', str(probe)], 1), cwd=str(REPO), capture_output=True)
base = json.load(open(probe))['instruction_dispatches']
print('snapshot resumes at', base, flush=True)

if a.raws == 'all':
    rows = [l.split('\t') for l in (HERE.parent / 'docs' / 'ir-raw-to-app-key-table.tsv').read_text().splitlines() if l and not l.startswith('#')]
    raws = [int(r[0], 0) for r in rows if len(r) > 1 and r[1] != '' and r[2] != '0x8046aa64']
else:
    raws = [int(x, 0) for x in a.raws.split(',')]


def one(raw):
    d = out / ('r%02x' % raw)
    if (d / 'summary.json').exists():
        return raw, 'cached'
    (d / 'hist').mkdir(parents=True, exist_ok=True)
    extra = ['--headless', '--summary', str(d / 'summary.json'), '--gop-frame-history', str(d / 'hist'), '--gop-history-interval-log2', '17',
             '--ir-script', '%d:raw=0x%02x' % (base + a.lead, raw), '--watch-pc-regs', a.watch]
    t = time.time()
    subprocess.run(run_cmd(extra, base + a.lead + a.window), cwd=str(REPO), capture_output=True)
    return raw, round(time.time() - t, 1)


with ThreadPoolExecutor(a.parallel) as ex:
    for raw, r in ex.map(one, raws):
        print('0x%02x' % raw, r, flush=True)

index = {}
for raw in raws:
    d = out / ('r%02x' % raw)
    try:
        s = json.load(open(d / 'summary.json'))
        frames = [f for f in json.load(open(d / 'hist' / 'index.json'))['frames'] if f['dispatches'] >= base]
    except Exception as e:  # noqa: BLE001
        index['0x%02x' % raw] = {'error': str(e)}
        continue
    hits = s.get('watched_pc_hits', {})
    osd = [h['a0'] for h in hits.get('0x804c3684', {}).get('hits', [])]
    index['0x%02x' % raw] = {'reason': s.get('reason'), 'frames': [(f['dispatches'] - base, f['covered_pixels'], f['checksum'], f['file']) for f in frames],
                             'osd': osd, 'src_switch': [h['a0'] for h in hits.get('0x8048710c', {}).get('hits', [])], 'zui_key': hits.get('0x803ffe40', {}).get('count', 0)}
json.dump(index, open(out / 'index.json', 'w'), indent=1)

if a.sheet:
    from PySide6.QtGui import QColor, QFont, QGuiApplication, QImage, QPainter
    app = QGuiApplication(sys.argv)
    cell_w, cell_h, cols = 340, 192, 4
    entries = []
    for raw in raws:
        info = index['0x%02x' % raw]
        if 'frames' not in info or not info['frames']:
            continue
        base_ck = info['frames'][0][2]
        # last frame that differs from the first (the first is the state before the key); else the last one
        diff = [f for f in info['frames'] if f[2] != base_ck]
        pick = diff[-1] if diff else None
        entries.append((raw, pick, info))
    for page in range(0, len(entries), 16):
        chunk = entries[page:page + 16]
        rows = (len(chunk) + cols - 1) // cols
        img = QImage(cols * cell_w, rows * (cell_h + 18), QImage.Format_RGB32)
        img.fill(QColor('#101010'))
        p = QPainter(img)
        p.setFont(QFont('Segoe UI', 9))
        for k, (raw, pick, info) in enumerate(chunk):
            x, y = (k % cols) * cell_w, (k // cols) * (cell_h + 18)
            p.setPen(QColor('#ffd060'))
            p.drawText(x + 4, y + 13, '0x%02x  osd=%s  frames=%d' % (raw, ','.join(sorted(set(info['osd']))) or '-', len(info['frames'])))
            if pick:
                im = QImage(str(out / ('r%02x' % raw) / 'hist' / pick[3]))
                if not im.isNull():
                    p.drawImage(x, y + 18, im.scaled(cell_w - 2, cell_h - 2))
        p.end()
        img.save(str(out / ('sheet_%02d.png' % (page // 16))))
    print('sheets written')
