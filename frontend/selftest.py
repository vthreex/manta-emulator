"""Headless self-test: starts the emulator through the Session class, waits for frames, optionally sends keys, prints the log.
usage: selftest.py SECONDS [KEY ...]"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PySide6.QtCore import QCoreApplication, QTimer
from PySide6.QtGui import QImage
from emulator_session import Session, Settings, default_snapshot, emulator_candidates
app = QCoreApplication(sys.argv)
st = Settings(); st.emulator = emulator_candidates()[0]
st.snapshot = default_snapshot(st)
s = Session(); t0 = time.time(); n = [0]
s.log.connect(lambda t: print('LOG', t[:200], flush=True))
last=[None]
s.frame.connect(lambda i: (n.__setitem__(0, n[0] + 1), last.__setitem__(0, i)))
s.state.connect(lambda x: print('STATE', x, round(time.time() - t0, 1), flush=True))
secs = float(sys.argv[1]); keys = sys.argv[2:]
s.start(st)
for i, k in enumerate(keys):
    QTimer.singleShot(int((secs * 0.5 + i * 3) * 1000), lambda k=k: (print('KEY', k, flush=True), s.send_key(k)))
QTimer.singleShot(int(secs * 1000), lambda: (print('frames', n[0], 'seq', s.seq, flush=True), last[0] and last[0].convertToFormat(QImage.Format_RGB32).save(os.environ.get('SHOT', 'selftest.png')), s.stop()))
QTimer.singleShot(int(secs * 1000) + 5000, app.quit)
app.exec()
