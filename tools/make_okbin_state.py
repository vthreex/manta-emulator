"""Save the Blaupunkt (OKbin) machine state at 5 B dispatches using exactly the control centre's command line, so 'Resume saved state' loads it.
usage: make_okbin_state.py [IMAGE.bin] [OUT.snap]   (exploratory bring-up profile, fast mode; ~11 minutes)"""
import os, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'frontend'))
from emulator_session import Settings, build_command, REPO
img = sys.argv[1] if len(sys.argv) > 1 else r'<downloads>/OKbin.bin'
out = sys.argv[2] if len(sys.argv) > 2 else str(REPO / 'work' / 'checkpoints' / 'okbin-app-up.snap')
st = Settings(); st.emulator = str(REPO / 'work/build-irq/manta_c4.exe'); st.firmware = img; st.start_mode = 'cold'; st.engine = 'interpreter'
prog, args = build_command(st, 'mk')
j = args.index('--frontend-shm'); del args[j:j + 2]
j = args.index('--max-instructions'); args[j + 1] = '5100000000'
cmd = [prog] + args + ['--headless', '--save-state', out, '--save-state-at', '5000000000', '--save-state-stop', '--summary', str(REPO / 'work' / 'mk_okbin_state.json')]
print(' '.join(cmd[:3]), '...'); r = subprocess.run(cmd, cwd=str(REPO)); print('exit', r.returncode)
