"""Record a key tour of the Sunplus menu: every step = (frame before, key, frame after), frames deduplicated by content hash.
A step waits until the runner reports the key's host call (runner.log), then until the OSD has been unchanged for 3 shots.
Output: work/tour/<tag>/frames/<hash>.png + graph.json"""
import sys, os, time, json, hashlib, argparse
root = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/../../../..')   # directory holding work/ and outputs/manta-emulator/
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import menu_viewer as mv

TOUR = sys.argv[1].split(',')
tag = sys.argv[2] if len(sys.argv) > 2 else 'a'
exe = sys.argv[3] if len(sys.argv) > 3 else 'sunplus_m8.exe'
out = root + '/work/tour/' + tag; os.makedirs(out + '/frames', exist_ok=True)
a = argparse.Namespace(runner=root + '/work/build-irq/' + exe, kernel=root + '/work/sunplus-led4202/blobs/lzma_059070.bin',
                       flash=root + '/work/sunplus-led4202/Manta_en_country1.bin', hyp=root + '/work/sunplus-stv/hyp-v2.tsv',
                       osd_addr=0x4a17a00, clut_addr=0x85cbf4b8, realtime_after=26000)
s = mv.Session(a)
logf = os.path.join(s.dir, 'runner.log')

def delivered():
    try: return open(logf, 'rb').read().count(b'host call ')
    except OSError: return 0

def grab():
    im = s.shot()
    return (hashlib.sha1(im.tobytes()).hexdigest()[:12], im) if im is not None else (None, None)

def stable(n=3, gap=0.9, maxwait=40):
    hist = []; t0 = time.time()
    while time.time() - t0 < maxwait:
        h, im = grab()
        if h is None: time.sleep(0.5); continue
        hist.append((h, im))
        if len(hist) >= n and len({x[0] for x in hist[-n:]}) == 1: return hist[-1]
        time.sleep(gap)
    return hist[-1] if hist else (None, None)

graph = {'steps': [], 'frames': {}}
def keep(h, im):
    if h not in graph['frames']:
        im.save('%s/frames/%s.png' % (out, h)); graph['frames'][h] = len(graph['frames'])
try:
    time.sleep(40)
    h, im = stable(4, 1.5, 120)
    keep(h, im); graph['start'] = h
    for k in TOUR:
        before = delivered(); s.key(k); t0 = time.time()
        while delivered() <= before and time.time() - t0 < 30: time.sleep(0.2)
        if delivered() <= before: print('key', k, 'not delivered in 30 s', flush=True); break
        time.sleep(0.8)
        h2, im2 = stable()
        if h2 == h:   # no visible change yet: the firmware may start drawing late, look again before recording a no-op
            time.sleep(4); h2, im2 = stable()
        if h2 is None: print('no frame after', k); break
        keep(h2, im2); graph['steps'].append([h, k, h2]); print(len(graph['steps']), k, h, '->', h2, '%.1fs' % (time.time() - t0), flush=True); h = h2
        json.dump(graph, open(out + '/graph.json', 'w'), indent=0)
finally:
    s.close()
print('frames', len(graph['frames']), 'steps', len(graph['steps']))
