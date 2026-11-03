"""Interactive viewer / driver for the Sunplus emulator (sunplus_run) - EXPLORATORY.

Starts the runner with a command file, then either
  * opens a window (default): keyboard -> remote keys, the OSD plane is polled and shown (placement on the panel is a guess), or
  * runs a headless script:  --script "wait 3,key MENU,wait 2,shot out1.png,key RIGHT,wait 2,shot out2.png"

Remote keys are the firmware's own event ids (found by probing the Manta LED4202 image):
  MENU 0x1021  UP 0x1018  DOWN 0x1019  LEFT 0x101a  RIGHT 0x101b  OK 0x101c  EXIT 0x1043  POWER 0x1017
The OSD buffer address (0x04a17a00, 1216x660, 8bpp) and the CLUT address (0x85cbf4b8) are specific to this image/config.

usage: python menu_viewer.py --runner sunplus_run.exe --kernel lzma_059070.bin --flash Manta.bin [--hyp hyp.tsv] [--script ...]
"""
import argparse, os, subprocess, sys, time, tempfile
from PIL import Image

KEYS = {'MENU': 0x1021, 'UP': 0x1018, 'DOWN': 0x1019, 'LEFT': 0x101a, 'RIGHT': 0x101b, 'OK': 0x101c, 'EXIT': 0x1043, 'POWER': 0x1017, 'SOURCE': 0x1047, 'VOLUP': 0x101f, 'VOLDOWN': 0x1020}
W, H = 1216, 660

def start(args, workdir):
    cmdfile = os.path.join(workdir, 'cmds.txt'); open(cmdfile, 'w').close()
    env = dict(os.environ, SUNPLUS_BEST_SCREEN='%x,%d,%d' % (args.osd_addr, W, H), SUNPLUS_PALETTE_ADDR='%x' % args.clut_addr)
    cmd = [args.runner, args.kernel, '--cpu', '24kec', '--flash', args.flash, '--flash-xip', '0x18000000', '--readback', '--unknown-zero', '--map-iop',
           '--iop-set', '0x1eec=0x98000000', '--iop-set', '0x0=0', '--iop-set', '0x8=1', '--irq-vec', '0x48', '--poll-limit', '50000000', '--max-idle', '400000000',
           '--max-blocks', '400000000000', '--gpu', '--cmd-file', cmdfile, '--realtime-after', str(args.realtime_after)]
    if args.hyp: cmd += ['--hyp', args.hyp]
    log = open(os.path.join(workdir, 'runner.log'), 'wb')
    p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, env=env)
    return p, cmdfile

class Session:
    def __init__(self, args):
        self.args = args; self.dir = tempfile.mkdtemp(prefix='sunplus_view_'); self.proc, self.cmdfile = start(args, self.dir); self.n = 0
    def send(self, line):
        with open(self.cmdfile, 'a') as f: f.write(line + '\n')
    def key(self, name): self.send('key 0x%x' % KEYS[name])
    def shot(self, timeout=30):
        self.n += 1; path = os.path.join(self.dir, 's%d.bin' % self.n)
        self.send('shot ' + path); t0 = time.time()
        while not os.path.exists(path + '.done'):
            if time.time() - t0 > timeout or self.proc.poll() is not None: return None
            time.sleep(0.05)
        try:
            raw = open(path, 'rb').read(); pal = open(path + '.pal', 'rb').read() if os.path.exists(path + '.pal') else bytes(1024)
            if len(raw) != W * H or len(pal) != 1024: return None
            im = render(raw, pal)
        except Exception: return None
        for ext in ('', '.pal', '.done'):
            try: os.remove(path + ext)
            except OSError: pass
        return im
    def close(self):
        try: self.proc.kill()
        except Exception: pass

def render(raw, pal, bg=(48, 48, 56)):
    lut = [bg if (i == 0 and pal[0] == 0) else tuple((c * pal[4 * i] + k * (255 - pal[4 * i])) // 255 for c, k in zip(pal[4 * i + 1:4 * i + 4], bg)) for i in range(256)]
    flat = bytes(b for p in raw for b in lut[p])
    return Image.frombytes('RGB', (W, H), flat)

def run_script(args):
    s = Session(args)
    try:
        for step in args.script.split(','):
            step = step.strip(); cmd, _, arg = step.partition(' ')
            if cmd == 'wait': time.sleep(float(arg))
            elif cmd == 'key': s.key(arg.upper())
            elif cmd == 'shot':
                im = s.shot()
                if im is None: print('shot failed'); break
                im.save(arg); print('saved', arg)
    finally:
        rc = s.proc.poll(); s.close()
        print('runner exit code:', rc)
        try:
            txt = open(os.path.join(s.dir, 'runner.log'), 'rb').read().decode('latin1'); lines = [ln for ln in txt.splitlines() if ln.split(':')[0].split(' ')[0] in ('stop', 'pc=', 'regs', 'last', 'host')]; print('runner stop lines:', lines[-6:]); print('runner log tail:', txt[-300:].replace(chr(10), ' | '))
        except Exception as ex: print('no log', ex)

def run_gui(args):
    import tkinter as tk
    from PIL import ImageTk
    s = Session(args); root = tk.Tk(); root.title('Sunplus OSD (exploratory)'); lbl = tk.Label(root); lbl.pack()
    status = tk.Label(root, text='booting... (about a minute); then press M for MENU', anchor='w'); status.pack(fill='x')
    keymap = {'m': 'MENU', 'Up': 'UP', 'Down': 'DOWN', 'Left': 'LEFT', 'Right': 'RIGHT', 'Return': 'OK', 'Escape': 'EXIT', 'p': 'POWER', 's': 'SOURCE', 'plus': 'VOLUP', 'minus': 'VOLDOWN'}
    def onkey(e):
        k = keymap.get(e.keysym) or keymap.get(e.char)
        if k: s.key(k); status.config(text='sent %s' % k)
    root.bind('<Key>', onkey)
    def tick():
        if s.proc.poll() is not None: status.config(text='emulator exited'); return
        try:
            im = s.shot(timeout=2)
            if im is not None:
                ph = ImageTk.PhotoImage(im); lbl.configure(image=ph); lbl.image = ph
        except Exception as ex:
            status.config(text='refresh error: %s' % ex)
        root.after(250, tick)
    root.after(1000, tick)
    root.protocol('WM_DELETE_WINDOW', lambda: (s.close(), root.destroy())); root.mainloop()

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--runner', required=True); ap.add_argument('--kernel', required=True); ap.add_argument('--flash', required=True); ap.add_argument('--hyp')
    ap.add_argument('--osd-addr', type=lambda x: int(x, 0), default=0x4a17a00); ap.add_argument('--clut-addr', type=lambda x: int(x, 0), default=0x85cbf4b8)
    ap.add_argument('--realtime-after', type=int, default=26000); ap.add_argument('--script')
    a = ap.parse_args()
    run_script(a) if a.script else run_gui(a)
