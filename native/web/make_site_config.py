#!/usr/bin/env python3
"""Prepare site/live/ for the in-browser emulator: config.json (arguments of the control-centre live profile, host paths remapped into the wasm file
system), the build (manta.js / manta.wasm from native/web/build.py) and the small config files. The firmware image and the save state are copied too,
for local testing; publishing them is decided separately (see .gitignore and the Pages workflow).

usage: python native/web/make_site_config.py WEB_BUILD_DIR SNAPSHOT
"""
import json, os, shutil, sys
here = os.path.dirname(os.path.abspath(__file__)); repo = os.path.abspath(os.path.join(here, '..', '..')); top = os.path.abspath(os.path.join(repo, '..', '..'))
sys.path.insert(0, os.path.join(repo, 'frontend'))
from emulator_session import Settings, build_command, emulator_candidates   # noqa: E402

web, snapshot = sys.argv[1], sys.argv[2]
live = os.path.join(repo, 'site', 'live'); os.makedirs(os.path.join(live, 'files'), exist_ok=True)
st = Settings(); st.emulator = emulator_candidates()[0]; st.snapshot = snapshot
_, args = build_command(st, 'live')
files = {}; out = []
i = 0
while i < len(args):
    a = args[i]
    if a in ('--frontend-shm', '--summary'): i += 2; continue
    if a == '--firmware': out += [a, '/data/firmware.bin']; files['/data/firmware.bin'] = 'firmware.bin'; i += 2; continue
    if a == '--load-state': out += [a, '/data/state.snap']; files['/data/state.snap'] = 'state.snap'; i += 2; continue
    if a.endswith('.tsv') and os.path.exists(os.path.join(top, a)):   # policy files keep their exact relative path: it is part of the snapshot's configuration fingerprint
        name = 'files/' + os.path.basename(a); shutil.copyfile(os.path.join(top, a), os.path.join(live, name)); files['/' + a.replace('\\', '/')] = name; out.append(a); i += 1; continue
    out.append(a); i += 1
for k in ('ir-keymap.tsv', 'sar-keymap.tsv'):
    src = os.path.join(repo, 'configs', 'input', k)
    if os.path.exists(src): shutil.copyfile(src, os.path.join(live, 'files', k)); files['/configs/input/' + k] = 'files/' + k
for k in ('manta.js', 'manta.wasm'): shutil.copyfile(os.path.join(web, k), os.path.join(live, k))
shutil.copyfile(os.path.join(repo, 'firmware', 'manta-lcd4214-original.bin'), os.path.join(live, 'firmware.bin'))
shutil.copyfile(snapshot, os.path.join(live, 'state.snap'))
sizes = {v: os.path.getsize(os.path.join(live, v)) for v in list(files.values()) + ['manta.wasm']}
# content versions (informational: the page reuses the browser cache; its Re-download button fetches fresh copies)
import hashlib
def ver(name):
    h = hashlib.sha256()
    with open(os.path.join(live, name), 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''): h.update(chunk)
    return h.hexdigest()[:12]
versions = {v: ver(v) for v in list(files.values()) + ['manta.wasm', 'manta.js', 'worker.js']}
json.dump({'args': out, 'files': files, 'sizes': sizes, 'versions': versions, 'snapshot': os.path.basename(snapshot)}, open(os.path.join(live, 'config.json'), 'w'), indent=1)
print('site/live ready:', len(out), 'args,', len(files), 'files, %.1f MB to download' % (sum(sizes.values()) / 1e6), versions)
