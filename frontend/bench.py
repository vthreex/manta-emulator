"""Measures emulator speed headlessly: runs N dispatches from the snapshot and prints dispatches/second.
usage: bench.py [millions] [exe]"""
import json, os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emulator_session import Settings, build_command, emulator_candidates, REPO
n = float(sys.argv[1]) if len(sys.argv) > 1 else 400
st = Settings(); st.emulator = sys.argv[2] if len(sys.argv) > 2 else emulator_candidates()[0]
st.snapshot = str(REPO / 'work' / 'checkpoints' / 'atv-boot.snap')
st.cpi = int(os.environ.get('CPI', '16'))
prog, args = build_command(st, 'x')
i = args.index('--frontend-shm'); del args[i:i + 2]
for flag in ('--preview-interval-log2', '--preview-max-fps'):
    j = args.index(flag); del args[j:j + 2]
i = args.index('--max-instructions'); base = None
args[i + 1] = str(int(n * 1e6))
summ = str(REPO / 'work' / 'bench_summary.json')
cmd = [prog] + args + ['--headless', '--summary', summ]
subprocess.run(cmd, cwd=str(REPO), capture_output=True, text=True)
base = json.load(open(summ))['instruction_dispatches']
args[i + 1] = str(base + int(n * 1e6))
cmd = [prog] + args + ['--headless', '--summary', summ] + (['--profile', str(REPO / 'work' / 'bench_profile.txt')] if os.environ.get('PROFILE') else [])
t = time.time()
r = subprocess.run(cmd, cwd=str(REPO), capture_output=True, text=True)
dt = time.time() - t
d = json.load(open(summ))
done = d['instruction_dispatches'] - base
print('rc', r.returncode, 'wall %.1fs' % dt, 'dispatches', done, '-> %.1f M/s' % (done / dt / 1e6), 'stop', d.get('stop_reason', d.get('reason')))
