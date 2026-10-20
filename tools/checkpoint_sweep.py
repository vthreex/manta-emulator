"""Run one short experiment per raw IR byte from a saved machine state (no cold boot).

Every run: load SNAPSHOT in a fresh process, deliver ONE raw IR byte through the emulated PM IR FIFO at guest dispatch INJECT, run WINDOW more
dispatches, keep the summary (watch hits of the key reader, OSD show/hide, source switch ...). Runs are independent; the snapshot is never modified.

usage: checkpoint_sweep.py --emulator EXE --snapshot FILE --out DIR --raws 0x0a,0x0b,... [--inject N] [--window N] [--parallel N] [--watch PCS] [--history]
The base command is configs/checkpoints/connected-menu-base.json (must equal the configuration the snapshot was written with).
"""
import argparse, json, os, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

root = Path(__file__).resolve().parents[1]
repo = root.parents[1]
p = argparse.ArgumentParser()
p.add_argument('--emulator', required=True)
p.add_argument('--snapshot', required=True)
p.add_argument('--out', required=True)
p.add_argument('--raws', required=True)
p.add_argument('--inject', type=int, default=1300000000)
p.add_argument('--window', type=int, default=60000000)
p.add_argument('--parallel', type=int, default=8)
p.add_argument('--watch', default='8046a758,804c3684,804c3574,8048710c,8048848c,803ffe40')
p.add_argument('--history', action='store_true', help='also write a BMP of every distinct GOP frame per run')
p.add_argument('--prefix', default='r', help='run directory prefix')
a = p.parse_args()
base = json.load(open(root / 'configs/checkpoints/connected-menu-base.json'))['argv']
out = Path(a.out)
out.mkdir(parents=True, exist_ok=True)
raws = [int(x, 0) for x in a.raws.split(',')]


def run(raw):
    d = out / ('%s%02x' % (a.prefix, raw))
    summary = d / 'summary.json'
    if summary.exists():
        return raw, 'cached'
    (d / 'hist').mkdir(parents=True, exist_ok=True)
    cmd = [a.emulator] + base + ['--headless', '--summary', str(summary), '--load-state', a.snapshot, '--max-instructions', str(a.inject + a.window),
                                 '--ir-script', '%d:raw=0x%02x' % (a.inject, raw), '--watch-pc-regs', a.watch]
    if a.history:
        cmd += ['--gop-frame-history', str(d / 'hist')]
    t0 = time.time()
    with open(d / 'out.log', 'w') as lg:
        pr = subprocess.Popen(cmd, cwd=repo, stdout=lg, stderr=subprocess.STDOUT)
        while pr.poll() is None:
            time.sleep(2)
            if summary.exists() and time.time() - summary.stat().st_mtime > 6:
                pr.kill()
                break
    return raw, int(time.time() - t0)


with ThreadPoolExecutor(a.parallel) as ex:
    for raw, r in ex.map(run, raws):
        print('0x%02x' % raw, r, flush=True)
