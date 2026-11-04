"""Builds the emulator command line from the front end's settings and talks to the running process (stdin = remote keys,
named shared memory = the picture; protocol in docs/frontend-protocol.md)."""
import hashlib
import json
import mmap
import os
import struct
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path

from PySide6.QtCore import QObject, QProcess, QTimer, Signal
from PySide6.QtGui import QImage

HEADER = 1024
MAX_PIXELS = 1920 * 1080
_HDR = struct.Struct('<IIIIIIIIQ')   # magic version seqBegin seqEnd width height flags reserved checksum
MAGIC = 0x41544E4D


FROZEN = getattr(sys, 'frozen', False)


def find_repo_root():
    here = Path(sys.executable).resolve() if FROZEN else Path(__file__).resolve()
    for p in [here.parent] + list(here.parents):
        if (p / 'work' / 'firmware-original.bin').exists():
            return p
    return here.parents[3] if len(here.parents) > 3 else here.parent


REPO = find_repo_root()
PKG = Path(getattr(sys, '_MEIPASS', '')) if FROZEN else Path(__file__).resolve().parents[1]
ORIGINAL_SHA256 = None   # computed lazily from work/firmware-original.bin (the verified original image)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def original_sha():
    global ORIGINAL_SHA256
    if ORIGINAL_SHA256 is None:
        p = REPO / 'work' / 'firmware-original.bin'
        ORIGINAL_SHA256 = sha256_file(p) if p.exists() else ''
    return ORIGINAL_SHA256


def emulator_candidates():
    out = []
    for rel in ('work/build-irq/manta_native_full.exe', 'work/build/manta_native_full.exe', 'work/build/manta.exe'):
        p = REPO / rel
        if p.exists():
            out.append(str(p))
    return out


@dataclass
class Settings:
    emulator: str = ''
    firmware: str = ''
    start_mode: str = 'snapshot'        # snapshot | cold
    snapshot: str = ''
    engine: str = 'native'              # native (recompiled stock firmware) | interpreter
    cpi: int = 16                       # guest cycles per instruction (uncalibrated time scale): lower = faster emulation, 16 = the stock setting of the saved states
    video_plane: str = 'setting'        # black | blue | setting (the guest's own Blue Screen setting) | none
    analog_input: str = 'none'          # none: no input signal (NO SIGNAL, the TV with nothing connected) | pal-test: EXPLORATORY generated PAL colour-bar signal
    colour_key: str = 'edge'            # none | edge | full
    menu_stays_open: bool = True
    frame_rate: str = 'smooth'          # smooth | normal | economy
    budget_billions: float = 8.0
    nvram: str = ''
    extra_args: str = ''
    interpolate: bool = True
    miu1_mb: int = 128                  # second DRAM bank; 128 = the stock board
    miu0_mb: int = 128                  # first DRAM bank mapped into the guest (EXPERIMENT: 2..128); 128 = the stock board
    pm51_divider: int = 32
    or1k_divider: int = 4
    scale_mode: str = 'fit'
    on_top: bool = False
    show_plane: bool = True
    fast_jit: bool = False              # EXPLORATORY: Unicorn JIT without per-instruction provenance/bookkeeping (any image, much faster, not strict)
    bringup: bool = True                # other firmware images: apply the labelled bring-up hypotheses (docs/other-board-bringup.md)
    remote_mode: str = 'keys'
    board: str = 'manta'                # manta | blaupunkt (see BOARDS)
    board_state: dict = field(default_factory=dict)   # last used machine settings per board, restored when switching back

    def save(self, path):
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        Path(path).write_text(json.dumps(asdict(self), indent=1))

    @staticmethod
    def load(path):
        s = Settings()
        try:
            d = json.loads(Path(path).read_text())
            for k, v in d.items():
                if hasattr(s, k):
                    setattr(s, k, v)
            if 'board' not in d and 'okbin' in (s.firmware or '').lower():
                s.board = 'blaupunkt'     # settings saved before the board switch existed: take the board's preset, not the old mix
                for k, v in board_preset('blaupunkt').items():
                    setattr(s, k, v)
        except Exception:
            pass
        return s


FRAME_FPS = {'smooth': 60, 'normal': 30, 'economy': 15, 'minimal': 5}


# The profile carries the EXPLORATORY generated PAL test signal (colour bars behind the OSD). With no input connected the analog front end has no
# HSYNC and no sync events: these are the absent-input options of the NO SIGNAL save states (configs/checkpoints/connected-menu-base.json).
TEST_SIGNAL_VALUED = ('--exploratory-atv-signal', '--max-avd-instructions')
TEST_SIGNAL_FLAGS = ('--exploratory-atv-picture',)
ABSENT_INPUT = ['--exploratory-avd-no-hsync', '--diagnostic-htotal-branch-join', '--exploratory-avd-no-sync-events', '--diagnostic-avd-11db-bit7', '0',
                '--exploratory-avd-htotal-constant', '0', '--exploratory-avd-noise-constant', '0']


def without_test_signal(args):
    out, i = [], 0
    while i < len(args):
        if args[i] in TEST_SIGNAL_VALUED: i += 2; continue
        if args[i] in TEST_SIGNAL_FLAGS: i += 1; continue
        out.append(args[i]); i += 1
    return out + ABSENT_INPUT


def default_snapshot(st):
    """The saved state that matches the analog input setting: NO SIGNAL (no input) or the PAL test-signal boot."""
    return str(REPO / 'work' / 'checkpoints' / ('atv-boot.snap' if st.analog_input == 'pal-test' else 'no-signal-ready-v2.snap'))


def build_command(st: Settings, shm_name: str):
    """Returns (program, args). Hardware/hypothesis flags come from configs/frontend/profile-original.json, unchanged."""
    prof = json.loads((PKG / 'configs' / 'frontend' / 'profile-original.json').read_text())
    args = list(prof['args'])
    if st.analog_input != 'pal-test':
        args = without_test_signal(args)
    # EXPLORATORY axis-aligned GE line model (docs/factory-menu.md): the factory (8202) and hotel (4578) windows draw their frames with GE line
    # commands; without it the machine stops there ("GE raster primitive/mode unsupported")
    if '--exploratory-ge-line' not in args:
        args.append('--exploratory-ge-line')
    fw = st.firmware or str(REPO / 'work' / 'firmware-original.bin')
    args = ['--firmware', fw] + args
    try:
        if sha256_file(fw) != original_sha():
            args.append('--unverified-firmware')
            args += ['--stuck-poll-limit', '20000000']
            if st.bringup:
                args += ['--exploratory-mstar-timer', '50', '--exploratory-miu1-done', '2000', '--exploratory-spi-status', '0', '--exploratory-miu1-alias-at', '60000000', '--exploratory-pm51-register-storage', '--pm51-instruction-budget', '100000000000', '--exploratory-mmio-bits', '1f001d84:08:08', '--exploratory-mmio-bits', '1f006008:01:01', '--exploratory-self-clearing-bit', '1f260d85:01', '--exploratory-self-clearing-bit', '1f2631e9:01', '--exploratory-self-clearing-bit', '1f2631f9:01', '--exploratory-self-clearing-bit', '1f264705:01', '--exploratory-tsp-dma-shift', '8', '--exploratory-bdma-mem-copy', '--max-or1k-instructions', '100000000000', '--exploratory-ge-fill-ignore-reg00', '0x8006',
                         '--pm51-dispatch-divider', '256', '--exploratory-self-clearing-bit', '1f206728:ff', '--exploratory-self-clearing-bit', '1f206729:ff',
                         '--exploratory-gop-surface', 'r1f240684:952x700:1904', '--exploratory-fast-jit']
    except OSError:
        pass
    def override(flag, value):
        if flag in args:
            args[args.index(flag) + 1] = str(value)
        else:
            args.extend([flag, str(value)])
    stock = (st.miu1_mb == 128 and st.miu0_mb == 128 and st.pm51_divider == 32 and st.or1k_divider == 4)
    cold = st.start_mode != 'snapshot' or not stock
    if cold:
        override('--exploratory-cpi', st.cpi)
    if st.miu1_mb != 128:
        args += ['--miu1-mb', str(st.miu1_mb)]
    if st.miu0_mb != 128:
        args += ['--miu0-mb', str(st.miu0_mb)]
    if st.pm51_divider != 32:
        override('--pm51-dispatch-divider', st.pm51_divider)
    if st.or1k_divider != 4:
        override('--or1k-dispatch-divider', st.or1k_divider)
    unverified_image = '--unverified-firmware' in args
    if st.start_mode == 'snapshot' and st.snapshot and (stock or unverified_image):
        args += ['--load-state', st.snapshot, '--load-state-allow-extra-options']
        if st.cpi != 16:
            args += ['--exploratory-cpi-after-load', str(max(2, st.cpi))]
    if st.fast_jit and st.engine != 'native' and '--exploratory-fast-jit' not in args:
        args += ['--exploratory-fast-jit']
    if st.engine == 'native' and not cold:     # the native engine only takes over from a saved state
        args += ['--native-cpu']
    if st.video_plane != 'none':
        args += ['--exploratory-video-plane', st.video_plane]
        if st.colour_key == 'edge':
            args += ['--exploratory-gop-black-colorkey-edge']
        elif st.colour_key == 'full':
            args += ['--exploratory-gop-black-colorkey']
    if st.menu_stays_open:
        args += ['--exploratory-menu-no-autoexit']
    if st.nvram:
        args += ['--nvram', st.nvram]
    args += ['--max-instructions', str(int(st.budget_billions * 1e9)), '--preview-interval-log2', '14', '--preview-max-fps', str(FRAME_FPS.get(st.frame_rate, 30)),
             '--frontend-shm', shm_name]
    if st.extra_args.strip():
        import shlex
        args += shlex.split(st.extra_args)
    return st.emulator, args


# Board presets: what "switch board" changes. The firmware image, how the machine starts, the engine and every hypothesis flag follow from the image
# (build_command adds the labelled bring-up profile for any non-stock image); the remote's mapped keys follow from the board's key source.
BOARDS = {
    'manta': {
        'label': 'Manta LCD4214 (stock firmware)',
        'keymap': 'ir-keymap.tsv',
        'preset': {'firmware': '', 'start_mode': 'snapshot', 'snapshot': '', 'engine': 'native', 'cpi': 16, 'miu1_mb': 128, 'miu0_mb': 128, 'pm51_divider': 32,
                   'or1k_divider': 4, 'budget_billions': 8.0, 'bringup': True, 'fast_jit': False},
        'remote_hint': 'IR remote (original firmware key path). Dotted outline = no verified IR code yet.',
    },
    'blaupunkt': {
        'label': 'Blaupunkt TP.MSD309.BP85 (OKbin, exploratory)',
        'keymap': 'sar-keymap.tsv',
        'image_candidates': ['work/firmware-okbin.bin', '<downloads>/OKbin.bin'],
        'preset': {'start_mode': 'snapshot', 'snapshot': 'work/checkpoints/okbin-app-up.snap', 'engine': 'interpreter', 'cpi': 16, 'miu1_mb': 128, 'miu0_mb': 128,
                   'pm51_divider': 32, 'or1k_divider': 4, 'budget_billions': 0.0, 'bringup': True, 'fast_jit': True},
        'remote_hint': 'Local keypad (ADC) keys: arrows, VOL+/-, CH+/-, MENU. Each press is held ~20 M dispatches, so expect a short delay. Dotted outline = not mapped.',
    },
}


def board_image(board):
    """Firmware image path for a board preset ('' = the stock default). The Blaupunkt dump is copied into work/ the first time it is found."""
    b = BOARDS.get(board, {})
    for c in b.get('image_candidates', []):
        p = Path(c) if os.path.isabs(c) else REPO / c
        if p.exists():
            dest = REPO / 'work' / 'firmware-okbin.bin'
            if not dest.exists() and p != dest:
                try:
                    dest.write_bytes(p.read_bytes())
                except OSError:
                    return str(p)
            return str(dest if dest.exists() else p)
    return ''


def board_preset(board):
    p = dict(BOARDS[board]['preset'])
    if 'firmware' not in p:
        p['firmware'] = board_image(board)
    if p.get('snapshot') and not os.path.isabs(p['snapshot']):
        p['snapshot'] = str(REPO / p['snapshot'])
    return p


def load_keymap_functions(board='manta'):
    fns = set()
    p = PKG / 'configs' / 'input' / BOARDS.get(board, BOARDS['manta'])['keymap']
    try:
        for line in p.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith('#'):
                fns.add(line.split()[0])
    except OSError:
        pass
    return fns


class Session(QObject):
    log = Signal(str)
    state = Signal(str)            # idle | starting | running | stopped
    frame = Signal(QImage)
    info = Signal(str, str, int)   # label, description, flags
    speed = Signal(float)          # guest dispatches per second (millions)

    def __init__(self):
        super().__init__()
        self.proc = None
        self.shm = None
        self.seq = 0
        self.frames = 0
        self.flags = 0
        self.shm_name = ''
        self.timer = QTimer(self)
        self.timer.setInterval(16)
        self.timer.timeout.connect(self._poll)
        self.last_disp = (0, 0.0)
        self.paused = False

    def running(self):
        return self.proc is not None and self.proc.state() != QProcess.NotRunning

    def start(self, st: Settings):
        if self.running():
            return
        self.shm_name = 'MantaFrame_%d' % os.getpid()
        program, args = build_command(st, self.shm_name)
        self.log.emit('> ' + program + ' ' + ' '.join(('"%s"' % a if ' ' in a else a) for a in args[:12]) + ' ... (%d arguments)' % len(args))
        self.proc = QProcess(self)
        self.proc.setWorkingDirectory(str(REPO))
        self.proc.setProcessChannelMode(QProcess.MergedChannels)
        self.proc.readyReadStandardOutput.connect(self._read_out)
        self.proc.finished.connect(self._finished)
        self.proc.errorOccurred.connect(lambda e: self.log.emit('process error: %s' % e))
        self.seq = 0
        self.frames = 0
        self.state.emit('starting')
        self.proc.start(program, args)
        self.timer.start()

    def stop(self):
        if not self.running():
            return
        self.proc.write(b'QUIT\n')
        self.proc.closeWriteChannel()
        QTimer.singleShot(2500, self._kill_if_running)

    def _kill_if_running(self):
        if self.running():
            self.proc.kill()

    def send_key(self, fn):
        if self.running():
            self.proc.write(('KEY %s\n' % fn).encode())

    def send_command(self, text):
        if self.running():
            self.proc.write((text + '\n').encode())

    def set_paused(self, on):
        self.paused = on
        self.send_command('PAUSE' if on else 'RESUME')

    def send_raw(self, value):
        if self.running():
            self.proc.write(('RAW 0x%02x\n' % value).encode())

    def _read_out(self):
        data = bytes(self.proc.readAllStandardOutput()).decode('utf-8', 'replace')
        for line in data.splitlines():
            if line.startswith('FRONTEND_READY'):
                self._open_shm()
                self.state.emit('running')
            if len(line) > 400:
                line = line[:400] + ' ...'
            if line.strip():
                self.log.emit(line)

    def _open_shm(self):
        try:
            self.shm = mmap.mmap(-1, HEADER + MAX_PIXELS * 4, tagname=self.shm_name)
        except OSError as e:
            self.log.emit('cannot open shared memory: %s' % e)

    def _finished(self, code, status):
        self.timer.stop()
        if self.shm:
            self.shm.close()
            self.shm = None
        self.log.emit('emulator exited with code %s' % code)
        self.state.emit('stopped')

    def _poll(self):
        m = self.shm
        if m is None:
            return
        magic, ver, sb, se, w, h, flags, _r, checksum = _HDR.unpack_from(m, 0)
        if magic != MAGIC:
            return
        if flags != self.flags:
            self.flags = flags
            self.info.emit('', '', flags)
        st = bytes(m[776:776 + 64]).split(b'\0', 1)[0].decode('ascii', 'replace')
        if st.startswith('MIPS dispatches '):
            import time as _t
            try:
                n = int(st.split()[-1])
                now = _t.time()
                if now - self.last_disp[1] >= 1.0:
                    if self.last_disp[1] and n >= self.last_disp[0] and not self.paused:
                        self.speed.emit((n - self.last_disp[0]) / (now - self.last_disp[1]) / 1e6)
                    self.last_disp = (n, now)
            except ValueError:
                pass
        if sb != se or se == self.seq or w == 0 or h == 0 or w * h > MAX_PIXELS:
            return
        data = bytes(m[HEADER:HEADER + w * h * 4])
        magic, ver, sb2, se2, *_ = _HDR.unpack_from(m, 0)
        if sb2 != se or se2 != se:        # the writer started the next frame while we copied: try again next tick
            return
        self.seq = se
        self.frames += 1
        img = QImage(data, w, h, w * 4, QImage.Format_ARGB32).copy()   # alpha 0xFF guest pixel, 0 backdrop (video plane)
        raw = bytes(m[40:40 + 256]).split(b'\0', 1)[0].decode('utf-8', 'replace')
        desc = bytes(m[296:296 + 480]).split(b'\0', 1)[0].decode('utf-8', 'replace')
        self.frame.emit(img)
        self.info.emit(raw, desc, flags)
