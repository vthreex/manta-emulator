"""Process shutdown regression: a finished run must leave the process promptly.

Headless: the process exits within GRACE seconds of the summary being complete.
Windowed + --preview-close-after-ms: same (a windowed run WITHOUT the close option keeps the last frame open by design and is not tested here).
"""
import argparse, json, os, subprocess, tempfile, time
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--firmware', required=True)
p.add_argument('--emulator', required=True)
p.add_argument('--report', required=True)
p.add_argument('--budget', type=int, default=20000000)
p.add_argument('--grace', type=float, default=10.0)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
repo = root.parents[1]
base = json.load(open(root / 'configs/checkpoints/connected-menu-base.json'))['argv']
exe = str(Path(a.emulator).resolve())
checks = {}
with tempfile.TemporaryDirectory(prefix='manta-shutdown-') as d:
    for tag, extra in (('headless', ['--headless']), ('headless_with_ir_and_watch', ['--headless', '--ir-script', '10000000:raw=0x0b', '--watch-pc-regs', '8046a758']),
                       ('windowed_close_after', ['--preview-close-after-ms', '500'])):
        summary = Path(d) / (tag + '.json')
        t0 = time.time()
        pr = subprocess.Popen([exe] + base + extra + ['--summary', str(summary), '--max-instructions', str(a.budget)], cwd=repo, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        seen = None
        while pr.poll() is None and time.time() - t0 < 600:
            time.sleep(0.25)
            if seen is None and summary.exists():
                seen = time.time()
            if seen is not None and time.time() - seen > a.grace:
                pr.kill()
                raise SystemExit('FAIL: %s still running %.0fs after the summary' % (tag, a.grace))
        assert pr.poll() is not None, tag + ' timed out'
        assert seen is not None or summary.exists(), tag + ' wrote no summary'
        checks[tag] = {'exit_code': pr.returncode, 'seconds_summary_to_exit': round(max(0.0, time.time() - (seen or time.time())), 2)}
Path(a.report).write_text(json.dumps({'result': 'PASS', 'checks': checks}, indent=1))
print('PASS: headless and auto-closing windowed runs exit promptly after the summary')
