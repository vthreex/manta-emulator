"""Drive the native emulator window with REAL Windows key messages (WM_KEYDOWN) once the guest reaches the NO SIGNAL screen.

The emulator runs in windowed mode without any --ir-script. This script waits for the GOP history frame at >= 1.30B dispatches, then posts
M, Down, Down, Right, Left, Enter, Esc to the emulator's window handle; the host keydown path (WM_KEYDOWN -> RemoteQueue -> keymap -> PM IR FIFO) is the only input.
"""
import ctypes, json, os, re, subprocess, sys, time
from ctypes import wintypes

sp = os.path.dirname(os.path.abspath(__file__))
out = sp + '/live1'
os.makedirs(out + '/hist', exist_ok=True)
os.makedirs(sp + '/export/live1', exist_ok=True)
c = json.load(open(sp + '/m3/cmd.json'))
c = [a.replace('/m3/', '/live1/') for a in c]
i = c.index('--ir-script'); del c[i:i + 2]
i = c.index('--max-instructions'); c[i + 1] = '1500000000'
i = c.index('--watch-pc-regs'); c[i + 1] = '8046a758,804c3684,804c3574'
c += ['--ir-keymap', '<repo-root>/outputs/manta-emulator/configs/input/ir-keymap.tsv']
json.dump(c, open(out + '/cmd.json', 'w'))
proc = subprocess.Popen(c, stdout=open(out + '/out.log', 'w'), stderr=subprocess.STDOUT,
                        cwd='<repo-root>')
user32 = ctypes.windll.user32
EnumWindows = user32.EnumWindows
WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]


def find_window():
    found = []

    def cb(hwnd, lp):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == proc.pid:
            n = user32.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, buf, n + 1)
            cls = ctypes.create_unicode_buffer(64)
            user32.GetClassNameW(hwnd, cls, 64)
            if user32.IsWindowVisible(hwnd) or buf.value:
                found.append((hwnd, buf.value, cls.value))
        return True
    EnumWindows(WNDENUMPROC(cb), 0)
    return found


def newest_dispatch():
    best = 0
    for n in os.listdir(out + '/hist'):
        m = re.search(r'_d(\d+)\.bmp$', n)
        if m:
            best = max(best, int(m.group(1)))
    return best


log = []
hwnd = None
while proc.poll() is None and hwnd is None:
    w = find_window()
    if w:
        hwnd = w[0][0]
        log.append(('window', str(w[0][1:])))
    time.sleep(1)
WM_KEYDOWN, WM_KEYUP = 0x100, 0x101
while proc.poll() is None and newest_dispatch() < 1300000000:
    time.sleep(2)
log.append(('guest_reached', newest_dispatch()))
seq = [('M', 0x4D), ('DOWN', 0x28), ('DOWN', 0x28), ('RIGHT', 0x27), ('LEFT', 0x25), ('ENTER', 0x0D), ('ESC', 0x1B)]
for name, vk in seq:
    # wait until the previous key's effect shows up as a new frame (or 40 s) so each press is clearly separated in guest time
    before = newest_dispatch()
    t0 = time.time()
    user32.PostMessageW(hwnd, WM_KEYDOWN, vk, 0)
    user32.PostMessageW(hwnd, WM_KEYUP, vk, 0xC0000001)
    log.append(('posted', name, before, time.strftime('%H:%M:%S')))
    while proc.poll() is None and newest_dispatch() == before and time.time() - t0 < 120:
        time.sleep(1)
    log.append(('effect_frame_dispatch', name, newest_dispatch()))
    time.sleep(3)
json.dump(log, open(out + '/live-log.json', 'w'), indent=1)
while proc.poll() is None and not os.path.exists(out + '/m.json'):
    time.sleep(5)
time.sleep(10)
try:
    proc.kill()
except Exception:
    pass
