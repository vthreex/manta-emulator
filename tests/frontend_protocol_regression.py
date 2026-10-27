"""Front-end protocol: the emulator started with --frontend-shm publishes frames in named shared memory, takes remote keys on stdin, pauses/resumes and quits on request.

Needs work/checkpoints/atv-boot.snap; SKIPS when absent (also on non-Windows). No Qt: plain subprocess + mmap.
Checks: FRONTEND_READY, a first complete frame (magic/version/seq/size), the dispatch status advancing, KEY MENU changes the picture (the original menu opens),
PAUSE freezes the dispatch counter, RESUME continues it, QUIT ends the process promptly.
"""
import argparse, json, mmap, os, struct, subprocess, sys, threading, time
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--firmware', required=True)
p.add_argument('--emulator', required=True)
p.add_argument('--report', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
repo = root.parents[1]
snap = repo / 'work/checkpoints/atv-boot.snap'
if sys.platform != 'win32' or not snap.exists():
    Path(a.report).write_text(json.dumps({'result': 'SKIP', 'reason': 'windows only / checkpoint absent'}))
    print('SKIP: windows only / checkpoint absent')
    sys.exit(0)
prof = json.load(open(root / 'configs/frontend/profile-original.json'))['args']
name = 'MantaFrameTest_%d' % os.getpid()
cmd = [str(Path(a.emulator).resolve()), '--firmware', str(Path(a.firmware).resolve())] + prof + [
    '--load-state', str(snap), '--load-state-allow-extra-options', '--exploratory-video-plane', 'black', '--exploratory-gop-black-colorkey-edge',
    '--exploratory-menu-no-autoexit', '--max-instructions', '4000000000', '--preview-interval-log2', '14', '--preview-max-fps', '30', '--frontend-shm', name]
proc = subprocess.Popen(cmd, cwd=repo, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
ready = threading.Event()


def pump():
    for line in proc.stdout:
        if b'FRONTEND_READY' in line:
            ready.set()


threading.Thread(target=pump, daemon=True).start()
HDR = struct.Struct('<IIIIIIIIQ')
try:
    assert ready.wait(60), 'no FRONTEND_READY'
    m = mmap.mmap(-1, 1024 + 1920 * 1080 * 4, tagname=name)

    def header():
        return HDR.unpack_from(m, 0)

    def status():
        t = bytes(m[776:840]).split(b'\0', 1)[0].decode()
        return int(t.split()[-1]) if t.startswith('MIPS dispatches ') else None

    def wait(pred, secs):
        t0 = time.time()
        while time.time() - t0 < secs:
            if pred():
                return True
            time.sleep(0.05)
        return False

    assert wait(lambda: header()[0] == 0x41544E4D and header()[2] == header()[3] and header()[2] >= 1 and header()[4] > 0, 60), 'no first frame'
    magic, ver, sb, se, w, h, flags, _r, ck0 = header()
    assert ver == 1 and (w, h) == (1360, 768), (ver, w, h)
    d0 = status()
    assert wait(lambda: (status() or 0) > (d0 or 0) + 1000000, 30), 'dispatch counter does not advance'
    seq0 = header()[2]
    proc.stdin.write(b'KEY MENU\n'); proc.stdin.flush()
    assert wait(lambda: header()[8] != ck0 and header()[2] == header()[3], 60), 'MENU did not change the picture'
    proc.stdin.write(b'PAUSE\n'); proc.stdin.flush()
    time.sleep(1.0)
    p1 = status(); time.sleep(1.5); p2 = status()
    assert p1 == p2, ('paused but dispatches advance', p1, p2)
    assert header()[6] & 4, 'paused flag not set'
    proc.stdin.write(b'RESUME\n'); proc.stdin.flush()
    assert wait(lambda: (status() or 0) > p2 + 1000000, 20), 'RESUME did not continue'
    t = time.time()
    proc.stdin.write(b'QUIT\n'); proc.stdin.flush(); proc.stdin.close()
    proc.wait(15)
    quit_s = time.time() - t
    assert quit_s < 10, quit_s
    Path(a.report).write_text(json.dumps({'result': 'PASS', 'size': [w, h], 'seq_after_menu': header()[2], 'quit_seconds': round(quit_s, 2)}))
    print('PASS: shared-memory frames, KEY MENU opens the original menu, PAUSE/RESUME freeze and continue the guest, QUIT ends the process')
finally:
    if proc.poll() is None:
        proc.kill()
