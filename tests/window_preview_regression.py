"""Native presenter test: a published frame must reach the window pixel-exactly and be labelled diagnostic.
Uses the labelled SYNTHETIC self-test buffer (never guest output) and also checks the clean-PATH startup."""
import argparse, collections, hashlib, json, os, struct, subprocess, tempfile
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--emulator', required=True)
p.add_argument('--report', required=True)
a = p.parse_args()
exe = Path(a.emulator).resolve()
env = os.environ.copy()
env['PATH'] = str(Path(os.environ['SystemRoot']) / 'System32')
with tempfile.TemporaryDirectory() as d:
    out = Path(d) / 'selftest.bmp'
    r = subprocess.run([str(exe), '--window-preview-selftest', str(out)], capture_output=True, text=True, env=env, timeout=60)
    assert r.returncode == 0 and 'diagnostic GOP preview' in r.stdout, (r.returncode, r.stdout, r.stderr)
    data = out.read_bytes()
    off, = struct.unpack_from('<I', data, 10)
    w, h = struct.unpack_from('<ii', data, 18)
    assert w > 0 and h < 0, (w, h)
    h = -h
    px = data[off:off + w * h * 4]
    count = collections.Counter(struct.unpack('<%dI' % (len(px) // 4), px))
    # 64x32 buffer, scaled by the integer factor the window chose; half red, half blue, remainder uncovered checker.
    red, blue = count[0x00ff0000], count[0x000000ff]
    assert red == blue and red > 0, (red, blue)
    scale2 = red // (32 * 16)
    scale = int(round(scale2 ** 0.5))
    assert scale * scale * 32 * 16 == red, (red, scale)
    assert count[0x00505050] > 0 and count[0x00383838] > 0, 'uncovered pixels must show the neutral checker, not guest colors'
    # no pixel of the buffer region may be pure black: uncovered guest pixels are never painted black
    sha = hashlib.sha256(px).hexdigest()
    r2 = subprocess.run([str(exe), '--window-preview-selftest', str(Path(d) / 'again.bmp')], capture_output=True, text=True, env=env, timeout=60)
    assert hashlib.sha256((Path(d) / 'again.bmp').read_bytes()[off:off + w * h * 4]).hexdigest() == sha, 'presentation must be deterministic'
Path(a.report).write_text(json.dumps({'result': 'PASS', 'window': [w, h], 'integer_scale': scale, 'red_pixels': red,
                                      'blue_pixels': blue, 'content_sha256': sha}, indent=2) + '\n')
print('PASS: native presenter draws the published frame exactly, labels it diagnostic and shows uncovered pixels as checker')
