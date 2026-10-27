import subprocess, sys, os, json
here = os.path.dirname(os.path.abspath(__file__))
extra = ['--exploratory-mstar-timer', '50', '--stuck-poll-limit', '3000000', '--exploratory-mmio-bits', '1f203105:01:01+rmw'] + sys.argv[2:]
env = dict(os.environ, STRICT='')
print(subprocess.run([sys.executable, os.path.join(here, 'try_firmware.py'), sys.argv[1], '2000'] + extra, capture_output=True, text=True, env=env).stdout[-1800:])
