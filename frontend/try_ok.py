"""OKbin trial: the bring-up hypotheses come from emulator_session.build_command; extra labelled options are passed through. usage: try_ok.py IMAGE [options...]"""
import subprocess, sys, os
here = os.path.dirname(os.path.abspath(__file__))
env = dict(os.environ, STRICT='')
r = subprocess.run([sys.executable, os.path.join(here, 'try_firmware.py'), sys.argv[1], '2000'] + sys.argv[2:], capture_output=True, text=True, env=env)
print(r.stdout[-1800:])
