"""Headless trial of another firmware image: prints where the run stops. usage: try_firmware.py IMAGE.bin [millions of dispatches]"""
import sys, subprocess, json, time
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emulator_session import Settings, build_command, REPO
st = Settings(); st.emulator = str(REPO / 'work/build/manta.exe'); st.firmware = sys.argv[1]; st.start_mode = 'cold'; st.engine = 'interpreter'
prog, args = build_command(st, 'x')
for flag in ('--frontend-shm', '--preview-interval-log2', '--preview-max-fps'):
    j = args.index(flag); del args[j:j + 2]
j = args.index('--max-instructions'); args[j + 1] = str(int(float(sys.argv[2]) * 1e6) if len(sys.argv) > 2 else 200000000)
summ = str(REPO / 'work/try_fw.json')
t = time.time()
r = subprocess.run([prog] + args + ['--headless', '--summary', summ], cwd=str(REPO), capture_output=True, text=True)
print('rc', r.returncode, 'wall', round(time.time() - t, 1)); print(r.stderr[-500:])
d = json.load(open(summ)); print(d['reason'], '| dispatches', d['instruction_dispatches'], '| pc', d.get('stop_pc'), '| addr', d.get('stop_address'), '| inflate', d.get('guest_inflate_entered'))
print(d.get('uart', '')[:300] if isinstance(d.get('uart'), str) else '')
