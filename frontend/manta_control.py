"""Manta LCD4214 emulator - control centre (Qt). Starts the original-firmware emulator, shows its picture, drives the remote.

    python frontend/manta_control.py

The emulator itself is a separate process (manta_native_full.exe / manta.exe); this program only builds its command line, shows the
GOP preview frames it publishes in shared memory and sends remote-key events to it. Nothing here touches guest state.
"""
import sys
import time
from pathlib import Path

from PySide6.QtCore import QEvent, QPointF, QRectF, QSettings, Qt, QTimer
from PySide6.QtGui import (QAction, QColor, QFont, QGuiApplication, QImage, QKeySequence, QPainter, QPalette, QPen)
from PySide6.QtWidgets import (QAbstractSpinBox, QApplication, QCheckBox, QComboBox, QDockWidget, QDoubleSpinBox, QSpinBox, QFileDialog, QFormLayout, QFrame, QGroupBox,
                               QHBoxLayout, QLabel, QLineEdit, QMainWindow, QMessageBox, QPlainTextEdit, QPushButton, QScrollArea, QSizePolicy, QStatusBar,
                               QToolBar, QVBoxLayout, QWidget)

sys.path.insert(0, str(Path(__file__).resolve().parent))
from emulator_session import (REPO, Session, Settings, emulator_candidates, load_keymap_functions, original_sha, sha256_file)  # noqa: E402
from remote_widget import BINDINGS, KEYS, RemoteWidget  # noqa: E402

VERSION = '1.0.0'
ACCENT = '#f29a1d'
SETTINGS_FILE = Path.home() / 'AppData' / 'Roaming' / 'MantaEmulator' / 'settings.json'
APP_ICON = None

STYLE = f"""
QMainWindow, QDockWidget, QWidget {{ background: #1b1c20; color: #dcdde0; font-family: 'Segoe UI'; font-size: 13px; }}
QDockWidget::title {{ background: #25262c; padding: 6px 10px; font-weight: 600; }}
QGroupBox {{ border: 1px solid #34363e; border-radius: 6px; margin-top: 14px; padding: 10px 8px 8px 8px; }}
QGroupBox::title {{ subcontrol-origin: margin; left: 10px; padding: 0 6px; color: {ACCENT}; font-weight: 600; }}
QLineEdit, QComboBox, QDoubleSpinBox, QSpinBox, QPlainTextEdit {{ background: #121316; border: 1px solid #34363e; border-radius: 4px; padding: 4px 6px; selection-background-color: {ACCENT}; }}
QComboBox QAbstractItemView {{ background: #121316; selection-background-color: {ACCENT}; selection-color: #101012; }}
QPushButton {{ background: #2b2d35; border: 1px solid #3c3f49; border-radius: 5px; padding: 6px 14px; }}
QPushButton:hover {{ background: #353844; }}
QPushButton:pressed {{ background: #20222a; }}
QPushButton:disabled {{ color: #6b6d75; background: #222329; }}
QPushButton#go {{ background: #1f8a4c; border-color: #27a95d; font-weight: 600; }}
QPushButton#go:hover {{ background: #26a05a; }}
QPushButton#stop {{ background: #a3332f; border-color: #c4413c; font-weight: 600; }}
QPushButton#stop:hover {{ background: #bd3c37; }}
QPushButton#mode {{ padding: 5px 12px; }}
QPushButton#mode:checked {{ background: {ACCENT}; color: #101012; border-color: {ACCENT}; font-weight: 600; }}
QToolBar {{ background: #212228; border: none; spacing: 8px; padding: 6px; }}
QStatusBar {{ background: #212228; color: #aeb0b8; }}
QLabel#warn {{ color: {ACCENT}; }}
QLabel#good {{ color: #4cc27a; }}
QLabel#dim {{ color: #8a8c95; }}
QToolTip {{ background: #2b2d35; color: #f0f0f0; border: 1px solid #4a4d58; padding: 4px; }}
QScrollBar:vertical {{ background: #1b1c20; width: 10px; }}
QScrollBar::handle:vertical {{ background: #3a3d47; border-radius: 4px; min-height: 24px; }}
QScrollBar::add-line, QScrollBar::sub-line {{ height: 0; }}
"""


class Display(QWidget):
    def __init__(self):
        super().__init__()
        self.image = None
        self.smooth = True
        self.scale_mode = 'fit'
        self.show_plane = False
        self._rgb = None
        self.message = 'Press Start to boot the original firmware'
        self.setMinimumSize(480, 270)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.setFocusPolicy(Qt.StrongFocus)

    def set_image(self, img):
        self.image = img
        self._rgb = None
        self.update()

    def set_message(self, text):
        self.message = text
        self.update()

    def paintEvent(self, e):
        p = QPainter(self)
        p.fillRect(self.rect(), QColor('#0b0b0d'))
        if self.image is None or self.image.isNull():
            p.setPen(QColor('#8a8c95'))
            f = QFont('Segoe UI', 14)
            p.setFont(f)
            p.drawText(self.rect(), Qt.AlignCenter, self.message)
            return
        if self.show_plane:
            if self._rgb is None:
                self._rgb = self.image.convertToFormat(QImage.Format_RGB32)
            img = self._rgb
        else:
            img = self.image
        iw, ih = img.width(), img.height()
        s = min(self.width() / iw, self.height() / ih)
        if self.scale_mode == 'integer' and s >= 1:
            s = float(int(s))
        if self.scale_mode == 'stretch':
            w, h = float(self.width()), float(self.height())
        else:
            w, h = iw * s, ih * s
        target = QRectF((self.width() - w) / 2, (self.height() - h) / 2, w, h)
        p.setRenderHint(QPainter.SmoothPixmapTransform, self.smooth)
        p.fillRect(target, QColor('#000000'))
        p.drawImage(target, img)
        p.setPen(QPen(QColor('#2a2c33'), 1))
        p.drawRect(target.adjusted(-0.5, -0.5, 0.5, 0.5))


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle('Manta LCD4214 Emulator %s' % VERSION)
        self.resize(1560, 900)
        self.settings = Settings.load(SETTINGS_FILE)
        self.session = Session()
        self.run_cpi = 16
        self.keymap_fns = load_keymap_functions()
        self.last_fps_t = time.time()
        self.last_fps_frames = 0
        self.fps = 0.0
        self.osk_flag = False
        self.display = Display()
        self.setCentralWidget(self.display)
        self._build_toolbar()
        self._build_machine_dock()
        self._build_remote_dock()
        self._build_log_dock()
        self._build_menu()
        self.status = QStatusBar()
        self.setStatusBar(self.status)
        self.state_label = QLabel('idle')
        self.frame_label = QLabel('')
        self.fps_label = QLabel('')
        self.status.addWidget(self.state_label)
        self.status.addWidget(self.frame_label, 1)
        self.status.addPermanentWidget(self.fps_label)
        self.speed_label = QLabel('')
        self.status.addPermanentWidget(self.speed_label)
        self.session.speed.connect(lambda v: self.speed_label.setText('%.1f M instr/s  (guest clock about %.0f MHz)   ' % (v, v * self.run_cpi)))
        self.session.log.connect(self.log_line)
        self.session.state.connect(self.on_state)
        self.session.frame.connect(self.on_frame)
        self.session.info.connect(self.on_info)
        self.fps_timer = QTimer(self)
        self.fps_timer.timeout.connect(self._fps)
        self.fps_timer.start(1000)
        QApplication.instance().installEventFilter(self)
        self.apply_settings_to_ui()
        self.on_state('idle')

    # ------------------------------------------------------------------ UI construction
    def _build_toolbar(self):
        tb = QToolBar('Run')
        tb.setMovable(False)
        self.addToolBar(tb)
        self.btn_start = QPushButton('▶  Start')
        self.btn_start.setObjectName('go')
        self.btn_start.clicked.connect(self.start)
        self.btn_stop = QPushButton('■  Stop')
        self.btn_stop.setObjectName('stop')
        self.btn_stop.clicked.connect(self.session.stop)
        self.btn_pause = QPushButton(chr(0x275A) + chr(0x275A) + '  Pause')
        self.btn_pause.setCheckable(True)
        self.btn_pause.toggled.connect(self.toggle_pause)
        self.btn_restart = QPushButton('↻  Restart')
        self.btn_restart.clicked.connect(self.restart)
        for b in (self.btn_start, self.btn_stop, self.btn_pause, self.btn_restart):
            tb.addWidget(b)
        tb.addSeparator()
        self.chk_smooth = QCheckBox('Smooth scaling')
        self.chk_smooth.setChecked(True)
        self.chk_smooth.toggled.connect(self._smooth)
        tb.addWidget(self.chk_smooth)
        self.cb_scale = QComboBox()
        for label, v in (('Fit', 'fit'), ('Whole pixels', 'integer'), ('Stretch', 'stretch')):
            self.cb_scale.addItem(label, v)
        self.cb_scale.currentIndexChanged.connect(self._scale)
        tb.addWidget(self.cb_scale)
        self.chk_plane = QCheckBox('ATV test picture')
        self.chk_plane.setToolTip('Exploratory: shows the PAL colour-bar test picture behind the on-screen display (not decoded video).')
        self.chk_plane.toggled.connect(self._plane)
        tb.addWidget(self.chk_plane)
        spacer = QWidget()
        spacer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        tb.addWidget(spacer)
        lab = QLabel('Remote input:')
        tb.addWidget(lab)
        self.btn_keys = QPushButton('Keyboard')
        self.btn_keys.setObjectName('mode')
        self.btn_keys.setCheckable(True)
        self.btn_mouse = QPushButton('Mouse')
        self.btn_mouse.setObjectName('mode')
        self.btn_mouse.setCheckable(True)
        self.btn_keys.clicked.connect(lambda: self.set_remote_mode('keys'))
        self.btn_mouse.clicked.connect(lambda: self.set_remote_mode('mouse'))
        tb.addWidget(self.btn_keys)
        tb.addWidget(self.btn_mouse)

    def _build_machine_dock(self):
        dock = QDockWidget('Machine')
        dock.setFeatures(QDockWidget.DockWidgetMovable | QDockWidget.DockWidgetFloatable)
        host = QWidget()
        lay = QVBoxLayout(host)
        lay.setContentsMargins(10, 8, 10, 8)

        g = QGroupBox('Firmware')
        f = QFormLayout(g)
        row = QHBoxLayout()
        self.ed_fw = QLineEdit()
        self.ed_fw.setPlaceholderText('work/firmware-original.bin')
        self.ed_fw.editingFinished.connect(self.firmware_changed)
        b = QPushButton('Browse…')
        b.clicked.connect(self.browse_firmware)
        row.addWidget(self.ed_fw, 1)
        row.addWidget(b)
        f.addRow(row)
        self.lbl_fw = QLabel('')
        self.lbl_fw.setWordWrap(True)
        f.addRow(self.lbl_fw)
        lay.addWidget(g)

        g = QGroupBox('Start-up')
        f = QFormLayout(g)
        self.cb_mode = QComboBox()
        self.cb_mode.addItem('Resume saved machine state (seconds)', 'snapshot')
        self.cb_mode.addItem('Cold boot from reset (slow)', 'cold')
        self.cb_mode.currentIndexChanged.connect(self.refresh_enabled)
        f.addRow('Mode', self.cb_mode)
        self.cb_snap = QComboBox()
        self.cb_snap.setEditable(True)
        snaps = sorted((REPO / 'work' / 'checkpoints').glob('*.snap'))
        for s in snaps:
            self.cb_snap.addItem(s.name, str(s))
        bs = QPushButton('…')
        bs.setFixedWidth(34)
        bs.clicked.connect(self.browse_snapshot)
        rr = QHBoxLayout()
        rr.addWidget(self.cb_snap, 1)
        rr.addWidget(bs)
        f.addRow('State file', rr)
        self.cb_engine = QComboBox()
        self.cb_engine.addItem('Native (recompiled firmware, fast)', 'native')
        self.cb_engine.addItem('Interpreter (any firmware image)', 'interpreter')
        self.cb_engine.currentIndexChanged.connect(self.refresh_enabled)
        f.addRow('CPU engine', self.cb_engine)
        lay.addWidget(g)

        g = QGroupBox('Machine')
        f = QFormLayout(g)
        self.cb_cpi = QComboBox()
        for n, speed in ((16, '~12 M instr/s'), (8, '~20 M instr/s'), (4, '~31 M instr/s'), (2, '~39 M instr/s'), (1, 'cold boot only')):
            self.cb_cpi.addItem('%d cycles / instruction  (%s)' % (n, speed), n)
        self.cb_cpi.setToolTip('Uncalibrated time scale. Fewer cycles per instruction = less co-processor work per instruction = much faster emulation,\nbut guest timers advance more slowly against the instruction count. 16 is the setting the saved states were made with.')
        f.addRow('Guest clock', self.cb_cpi)
        self.lbl_cpi = QLabel('Lower = faster emulation (about 3x at 2). Time scale only; not a measured clock.')
        self.lbl_cpi.setObjectName('dim')
        self.lbl_cpi.setWordWrap(True)
        f.addRow(self.lbl_cpi)
        f.addRow('CPU', QLabel('MIPS (MSD309 / Titania4), original firmware'))
        self.cb_ram = QComboBox()
        for mb in (0, 64, 128, 192, 256, 352):
            self.cb_ram.addItem('%d MB%s' % (128 + mb, '  (stock board)' if mb == 128 else ''), mb)
        self.cb_ram.setToolTip('Total DRAM = MIU0 (fixed 128 MB) + MIU1. Other firmware images may expect a different amount.\nA non-stock size needs a cold boot (saved states are bound to the stock size).')
        f.addRow('RAM', self.cb_ram)
        self.sp_pm = QSpinBox()
        self.sp_pm.setRange(1, 4096)
        self.sp_pm.setToolTip('MIPS dispatches per PM51 (standby MCU) tick. Hypothetical, not a measured clock.')
        f.addRow('PM51 divider', self.sp_pm)
        self.sp_or = QSpinBox()
        self.sp_or.setRange(1, 4096)
        self.sp_or.setToolTip('MIPS dispatches per multimedia co-processor (OpenRISC) step. Hypothetical.')
        f.addRow('Co-processor divider', self.sp_or)
        self.lbl_stock = QLabel('')
        self.lbl_stock.setObjectName('warn')
        self.lbl_stock.setWordWrap(True)
        f.addRow(self.lbl_stock)
        self.cb_ram.currentIndexChanged.connect(self.refresh_enabled)
        self.sp_pm.valueChanged.connect(self.refresh_enabled)
        self.sp_or.valueChanged.connect(self.refresh_enabled)
        self.sp_budget = QDoubleSpinBox()
        self.sp_budget.setRange(0.1, 200.0)
        self.sp_budget.setDecimals(1)
        self.sp_budget.setSuffix(' G instructions')
        f.addRow('Run length', self.sp_budget)
        lay.addWidget(g)

        g = QGroupBox('Picture')
        f = QFormLayout(g)
        self.cb_plane = QComboBox()
        for label, v in (('Black', 'black'), ('Blue', 'blue'), ('Follow the guest Blue-Screen setting', 'setting'), ('Neutral checker (no video plane)', 'none')):
            self.cb_plane.addItem(label, v)
        f.addRow('Video plane', self.cb_plane)
        self.cb_key = QComboBox()
        for label, v in (('Off (menus opaque)', 'none'), ('Edge only (menus see-through at the border)', 'edge'), ('All black (aggressive)', 'full')):
            self.cb_key.addItem(label, v)
        f.addRow('Black colour key', self.cb_key)
        self.cb_rate = QComboBox()
        for label, v in (('Up to 60 fps', 'smooth'), ('Up to 30 fps', 'normal'), ('Up to 15 fps', 'economy'), ('Up to 5 fps (fastest emulation)', 'minimal')):
            self.cb_rate.addItem(label, v)
        f.addRow('Frame updates', self.cb_rate)
        self.chk_menu = QCheckBox('Keep the menu open (ignore its auto-exit)')
        f.addRow(self.chk_menu)
        lay.addWidget(g)

        g = QGroupBox('Advanced')
        f = QFormLayout(g)
        self.cb_emu = QComboBox()
        self.cb_emu.setEditable(True)
        for c in emulator_candidates():
            self.cb_emu.addItem(c)
        f.addRow('Emulator', self.cb_emu)
        self.ed_nv = QLineEdit()
        self.ed_nv.setPlaceholderText('optional NVRAM overlay (settings keep between runs)')
        f.addRow('NVRAM', self.ed_nv)
        self.ed_extra = QLineEdit()
        self.ed_extra.setPlaceholderText('extra emulator options')
        f.addRow('Extra', self.ed_extra)
        lay.addWidget(g)
        lay.addStretch(1)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setWidget(host)
        dock.setWidget(scroll)
        dock.setMinimumWidth(330)
        self.addDockWidget(Qt.LeftDockWidgetArea, dock)
        self.dock_machine = dock

    def _build_remote_dock(self):
        dock = QDockWidget('Remote')
        dock.setFeatures(QDockWidget.DockWidgetMovable | QDockWidget.DockWidgetFloatable)
        host = QWidget()
        lay = QVBoxLayout(host)
        lay.setContentsMargins(8, 8, 8, 8)
        self.remote = RemoteWidget(self.keymap_fns)
        self.remote.keyPressed.connect(self.remote_key)
        lay.addWidget(self.remote, 1)
        self.lbl_remote = QLabel('')
        self.lbl_remote.setObjectName('dim')
        self.lbl_remote.setWordWrap(True)
        lay.addWidget(self.lbl_remote)
        row = QHBoxLayout()
        for text, fn in (('Keyboard (K)', 'OSK'), ('Network (N)', 'NET'), ('Browser (B)', 'BROWSER')):
            b = QPushButton(text)
            b.clicked.connect(lambda _=False, f=fn: self.remote_key(f))
            row.addWidget(b)
        lay.addLayout(row)
        dock.setWidget(host)
        dock.setMinimumWidth(270)
        self.addDockWidget(Qt.RightDockWidgetArea, dock)
        self.dock_remote = dock

    def _build_log_dock(self):
        dock = QDockWidget('Log')
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(2000)
        self.log.setFont(QFont('Consolas', 9))
        dock.setWidget(self.log)
        self.addDockWidget(Qt.BottomDockWidgetArea, dock)
        dock.hide()
        self.dock_log = dock

    def _build_menu(self):
        mb = self.menuBar()
        fm = mb.addMenu('&File')
        a = QAction('Open firmware image…', self)
        a.setShortcut('Ctrl+O')
        a.triggered.connect(self.browse_firmware)
        fm.addAction(a)
        a = QAction('Save picture…', self)
        a.setShortcut('Ctrl+S')
        a.triggered.connect(self.save_picture)
        fm.addAction(a)
        a = QAction('Copy picture', self)
        a.setShortcut('Ctrl+C')
        a.triggered.connect(self.copy_picture)
        fm.addAction(a)
        fm.addSeparator()
        a = QAction('Save machine profile...', self)
        a.triggered.connect(self.save_profile)
        fm.addAction(a)
        a = QAction('Load machine profile...', self)
        a.triggered.connect(self.load_profile)
        fm.addAction(a)
        fm.addSeparator()
        a = QAction('Quit', self)
        a.setShortcut('Ctrl+Q')
        a.triggered.connect(self.close)
        fm.addAction(a)
        mm = mb.addMenu('&Machine')
        a = QAction('Pause / resume', self)
        a.setShortcut('Ctrl+P')
        a.triggered.connect(lambda: self.btn_pause.toggle())
        mm.addAction(a)
        for text, fn, sc in (('Start', self.start, 'F5'), ('Stop', self.session.stop, 'Shift+F5'), ('Restart', self.restart, 'Ctrl+F5')):
            a = QAction(text, self)
            a.setShortcut(sc)
            a.triggered.connect(fn)
            mm.addAction(a)
        vm = mb.addMenu('&View')
        a = QAction('Always on top', self)
        a.setCheckable(True)
        a.toggled.connect(self.toggle_on_top)
        vm.addAction(a)
        a = QAction('Full screen', self)
        a.setShortcut('F11')
        a.triggered.connect(self.toggle_fullscreen)
        vm.addAction(a)
        for dock in (self.dock_machine, self.dock_remote, self.dock_log):
            vm.addAction(dock.toggleViewAction())
        hm = mb.addMenu('&Help')
        a = QAction('Keyboard bindings', self)
        a.triggered.connect(self.show_bindings)
        hm.addAction(a)
        a = QAction('About', self)
        a.triggered.connect(self.about)
        hm.addAction(a)

    # ------------------------------------------------------------------ settings <-> UI
    def apply_settings_to_ui(self):
        s = self.settings
        self.ed_fw.setText(s.firmware)
        self._set_combo(self.cb_mode, s.start_mode)
        snap = s.snapshot or str(REPO / 'work' / 'checkpoints' / 'atv-boot.snap')
        i = self.cb_snap.findData(snap)
        if i >= 0:
            self.cb_snap.setCurrentIndex(i)
        else:
            self.cb_snap.setEditText(snap)
        self._set_combo(self.cb_engine, s.engine)
        self._set_combo(self.cb_cpi, s.cpi)
        self._set_combo(self.cb_ram, s.miu1_mb)
        self.sp_pm.setValue(s.pm51_divider)
        self.sp_or.setValue(s.or1k_divider)
        self.sp_budget.setValue(s.budget_billions)
        self._set_combo(self.cb_plane, s.video_plane)
        self._set_combo(self.cb_key, s.colour_key)
        self._set_combo(self.cb_rate, s.frame_rate)
        self.chk_menu.setChecked(s.menu_stays_open)
        self.cb_emu.setEditText(s.emulator or (emulator_candidates() or [''])[0])
        self.ed_nv.setText(s.nvram)
        self.ed_extra.setText(s.extra_args)
        self.chk_smooth.setChecked(s.interpolate)
        self._set_combo(self.cb_scale, s.scale_mode)
        self.chk_plane.setChecked(s.show_plane)
        self.set_remote_mode(s.remote_mode)
        self.firmware_changed()

    @staticmethod
    def _set_combo(cb, data):
        i = cb.findData(data)
        if i >= 0:
            cb.setCurrentIndex(i)

    def collect_settings(self):
        s = self.settings
        s.firmware = self.ed_fw.text().strip()
        s.start_mode = self.cb_mode.currentData()
        sd = self.cb_snap.currentData()
        s.snapshot = self.cb_snap.currentText() if self.cb_snap.currentText() != self.cb_snap.itemText(self.cb_snap.currentIndex()) else (sd or self.cb_snap.currentText())
        s.engine = self.cb_engine.currentData()
        s.cpi = self.cb_cpi.currentData()
        s.miu1_mb = self.cb_ram.currentData()
        s.pm51_divider = self.sp_pm.value()
        s.or1k_divider = self.sp_or.value()
        s.budget_billions = self.sp_budget.value()
        s.video_plane = self.cb_plane.currentData()
        s.colour_key = self.cb_key.currentData()
        s.frame_rate = self.cb_rate.currentData()
        s.menu_stays_open = self.chk_menu.isChecked()
        s.emulator = self.cb_emu.currentText().strip()
        s.nvram = self.ed_nv.text().strip()
        s.extra_args = self.ed_extra.text().strip()
        s.interpolate = self.chk_smooth.isChecked()
        s.scale_mode = self.cb_scale.currentData()
        s.show_plane = self.chk_plane.isChecked()
        s.remote_mode = self.remote.mode
        return s

    def is_original(self):
        fw = self.ed_fw.text().strip() or str(REPO / 'work' / 'firmware-original.bin')
        try:
            return original_sha() != '' and sha256_file(fw) == original_sha()
        except OSError:
            return False

    def firmware_changed(self):
        fw = self.ed_fw.text().strip() or str(REPO / 'work' / 'firmware-original.bin')
        try:
            sha = sha256_file(fw)
            size = Path(fw).stat().st_size
        except OSError:
            self.lbl_fw.setObjectName('warn')
            self.lbl_fw.setStyleSheet('')
            self.lbl_fw.setText('Cannot read this file.')
            self.refresh_enabled()
            return
        if sha == original_sha():
            self.lbl_fw.setObjectName('good')
            self.lbl_fw.setText('Verified original image (%d KB, SHA-256 %s…). Snapshots and the native engine are available.' % (size // 1024, sha[:12]))
        else:
            self.lbl_fw.setObjectName('warn')
            self.lbl_fw.setText('Different image (%d KB, SHA-256 %s…). It runs on the interpreter from a cold boot; hardware behaviour the model does not know stops the run (strict mode).' % (size // 1024, sha[:12]))
        self.lbl_fw.setStyleSheet('')
        self.lbl_fw.style().unpolish(self.lbl_fw)
        self.lbl_fw.style().polish(self.lbl_fw)
        self.refresh_enabled()

    def refresh_enabled(self):
        orig = self.is_original()
        if not orig:
            self._set_combo(self.cb_mode, 'cold')
            self._set_combo(self.cb_engine, 'interpreter')
        self.cb_mode.setEnabled(orig)
        self.cb_engine.setEnabled(orig)
        stock = self.cb_ram.currentData() == 128 and self.sp_pm.value() == 32 and self.sp_or.value() == 4
        snapshot = self.cb_mode.currentData() == 'snapshot'
        self.lbl_stock.setText('' if stock else 'Non-stock RAM or clock dividers: saved machine states do not apply, the machine cold-boots.')
        if snapshot and not stock:
            self.lbl_stock.setText(self.lbl_stock.text() + ' (Start will ignore the state file.)')
        self.cb_snap.setEnabled(snapshot and orig)
        self.cb_cpi.model().item(4).setEnabled(not snapshot or not stock)

    def browse_firmware(self):
        p, _ = QFileDialog.getOpenFileName(self, 'Firmware image', str(REPO / 'work'), 'SPI flash images (*.bin);;All files (*)')
        if p:
            self.ed_fw.setText(p)
            self.firmware_changed()

    def browse_snapshot(self):
        p, _ = QFileDialog.getOpenFileName(self, 'Machine state', str(REPO / 'work' / 'checkpoints'), 'Machine states (*.snap);;All files (*)')
        if p:
            self.cb_snap.setEditText(p)

    # ------------------------------------------------------------------ run control
    def start(self):
        if self.session.running():
            return
        st = self.collect_settings()
        if not st.emulator or not Path(st.emulator).exists():
            QMessageBox.warning(self, 'Emulator', 'Emulator executable not found. Build manta_native_full (see docs/native-port.md) or set it under Advanced.')
            return
        if st.start_mode == 'snapshot' and not Path(st.snapshot).exists():
            QMessageBox.warning(self, 'Machine state', 'State file not found:\n%s' % st.snapshot)
            return
        st.save(SETTINGS_FILE)
        self.run_cpi = st.cpi
        self.display.set_image(None)
        self.display.set_message('Starting…' if st.start_mode == 'snapshot' else 'Cold boot: the original firmware starts from reset. This takes a long time.')
        self.session.start(st)

    def restart(self):
        if self.session.running():
            self.session.stop()
            QTimer.singleShot(3200, self.start)
        else:
            self.start()

    def on_state(self, state):
        self.state_label.setText({'idle': 'Idle', 'starting': 'Starting…', 'running': 'Running', 'stopped': 'Stopped'}.get(state, state))
        running = state in ('starting', 'running')
        self.btn_start.setEnabled(not running)
        self.btn_stop.setEnabled(running)
        self.btn_pause.setEnabled(running)
        if not running and self.btn_pause.isChecked():
            self.btn_pause.setChecked(False)
        self.btn_restart.setEnabled(True)
        if state == 'stopped' and self.display.image is None:
            self.display.set_message('The emulator stopped before showing a picture. See View → Log.')

    def on_frame(self, img):
        self.display.smooth = self.chk_smooth.isChecked()
        self.display.set_image(img)

    def on_info(self, label, desc, flags):
        if label:
            self.frame_label.setText(label if len(label) < 160 else label[:157] + '…')
            self.frame_label.setToolTip(desc)
        self.osk_flag = bool(flags & 2)
        self._update_remote_label()

    def _fps(self):
        now = time.time()
        n = self.session.frames
        self.fps = (n - self.last_fps_frames) / max(0.001, now - self.last_fps_t)
        self.last_fps_frames, self.last_fps_t = n, now
        self.fps_label.setText('%.0f fps   %s' % (self.fps, '' if not self.display.image else '%dx%d' % (self.display.image.width(), self.display.image.height())))

    def log_line(self, text):
        self.log.appendPlainText(text)

    def _smooth(self, on):
        self.display.smooth = on
        self.display.update()

    def toggle_pause(self, on):
        self.session.set_paused(on)
        self.btn_pause.setText(chr(0x25B6) + '  Resume' if on else chr(0x275A) + chr(0x275A) + '  Pause')
        if on:
            self.speed_label.setText('paused   ')

    def _scale(self):
        self.display.scale_mode = self.cb_scale.currentData()
        self.display.update()

    def toggle_on_top(self, on):
        flags = self.windowFlags()
        self.setWindowFlags(flags | Qt.WindowStaysOnTopHint if on else flags & ~Qt.WindowStaysOnTopHint)
        self.show()

    def save_profile(self):
        p, _ = QFileDialog.getSaveFileName(self, 'Save machine profile', str(Path.home() / 'manta-profile.json'), 'Profiles (*.json)')
        if p:
            self.collect_settings().save(p)

    def load_profile(self):
        p, _ = QFileDialog.getOpenFileName(self, 'Load machine profile', str(Path.home()), 'Profiles (*.json)')
        if p:
            self.settings = Settings.load(p)
            self.apply_settings_to_ui()

    def _plane(self, on):
        self.display.show_plane = on
        self.display.update()

    def toggle_fullscreen(self):
        self.showNormal() if self.isFullScreen() else self.showFullScreen()

    def save_picture(self):
        if not self.display.image:
            return
        p, _ = QFileDialog.getSaveFileName(self, 'Save picture', 'manta-%s.png' % time.strftime('%Y%m%d-%H%M%S'), 'PNG (*.png)')
        if p:
            self.display.image.save(p)

    def copy_picture(self):
        if self.display.image:
            QGuiApplication.clipboard().setImage(self.display.image)

    # ------------------------------------------------------------------ remote
    def set_remote_mode(self, mode):
        self.remote.set_mode(mode)
        self.btn_keys.setChecked(mode == 'keys')
        self.btn_mouse.setChecked(mode == 'mouse')
        self._update_remote_label()

    def _update_remote_label(self):
        if self.remote.mode == 'mouse':
            t = 'Mouse mode: click the keys on the remote. Dotted outline = no verified IR code for the original firmware yet.'
        elif self.osk_flag:
            t = 'Keyboard mode, text entry: type; Enter = OK, F10 = accept, Esc = cancel, Tab = shift.'
        else:
            t = 'Keyboard mode: the yellow badge on each key is its binding. Esc sends EXIT (the real remote has no such key).'
        self.lbl_remote.setText(t)

    def remote_key(self, fn):
        self.remote.flash(fn)
        self.session.send_key(fn)
        if fn not in self.keymap_fns and fn not in ('OSK', 'NET', 'BROWSER') and not fn.startswith('OSK_'):
            self.log_line('key %s: no evidence-backed IR code yet (not deliverable to the original firmware)' % fn)

    OSK_KEYS = {Qt.Key_Up: 'UP', Qt.Key_Down: 'DOWN', Qt.Key_Left: 'LEFT', Qt.Key_Right: 'RIGHT', Qt.Key_Return: 'OK', Qt.Key_Enter: 'OK',
                Qt.Key_Escape: 'EXIT', Qt.Key_Tab: 'MENU', Qt.Key_Backspace: 'OSK_BACKSPACE', Qt.Key_PageUp: 'PAGEUP', Qt.Key_PageDown: 'PAGEDOWN',
                Qt.Key_F10: 'OSK_ENTER'}

    def eventFilter(self, obj, ev):
        if ev.type() != QEvent.KeyPress or ev.isAutoRepeat() or not self.isActiveWindow():
            return False
        fw = QApplication.focusWidget()
        if isinstance(fw, (QLineEdit, QPlainTextEdit, QAbstractSpinBox)) or (isinstance(fw, QComboBox) and fw.isEditable()):
            return False
        if ev.modifiers() & (Qt.ControlModifier | Qt.AltModifier):
            return False
        key = ev.key()
        if self.remote.mode != 'keys':
            return False
        if self.osk_flag:
            fn = self.OSK_KEYS.get(key)
            if fn is None and ev.text() and ev.text().isprintable():
                fn = 'OSK_CHAR:' + ev.text()
            if fn:
                self.session.send_key(fn)
                return True
            return False
        fn = BINDINGS.get(key)
        if fn is None:
            fn = {Qt.Key_Escape: 'EXIT', Qt.Key_K: 'OSK', Qt.Key_N: 'NET', Qt.Key_B: 'BROWSER'}.get(key)
        if fn:
            self.remote_key(fn)
            return True
        return False

    # ------------------------------------------------------------------ dialogs
    def show_bindings(self):
        rows = ['%-10s  %s' % (k.fn, ' / '.join(__import__('remote_widget').key_label(b) for b in k.bind)) for k in KEYS if k.bind]
        QMessageBox.information(self, 'Keyboard bindings', '\n'.join(rows) + '\n\nEsc  EXIT (firmware candidate; not on the real remote)\nK / N / B  on-screen keyboard, network page, browser (native engine)')

    def about(self):
        QMessageBox.about(self, 'About', '<b>Manta LCD4214 Emulator %s</b><br>Runs the original, unmodified firmware of the Manta LCD4214 (MStar MSD309 / Titania4).<br><br>'
                          'The picture is the guest\'s own GOP framebuffer; hardware the model does not know is left unknown, and the exploratory models that are '
                          'switched on are labelled in the status line. This is not a verified reproduction of the panel output.' % VERSION)

    def closeEvent(self, e):
        self.collect_settings().save(SETTINGS_FILE)
        if self.session.running():
            self.session.stop()
            t0 = time.time()
            while self.session.running() and time.time() - t0 < 3:
                QApplication.processEvents()
            self.session._kill_if_running()
        super().closeEvent(e)


def main():
    app = QApplication(sys.argv)
    app.setStyle('Fusion')
    pal = QPalette()
    pal.setColor(QPalette.Window, QColor('#1b1c20'))
    pal.setColor(QPalette.WindowText, QColor('#dcdde0'))
    pal.setColor(QPalette.Base, QColor('#121316'))
    pal.setColor(QPalette.Text, QColor('#dcdde0'))
    pal.setColor(QPalette.Button, QColor('#2b2d35'))
    pal.setColor(QPalette.ButtonText, QColor('#dcdde0'))
    pal.setColor(QPalette.Highlight, QColor(ACCENT))
    pal.setColor(QPalette.HighlightedText, QColor('#101012'))
    app.setPalette(pal)
    app.setStyleSheet(STYLE)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
