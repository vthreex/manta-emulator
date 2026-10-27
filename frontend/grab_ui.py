"""Renders the control centre (and optionally a running session) to PNG files without showing a window - used for visual checks.
usage: grab_ui.py OUT.png [--start SECONDS]"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PySide6.QtCore import QTimer
from PySide6.QtGui import QImage
from PySide6.QtWidgets import QApplication
import manta_control as mc

out = sys.argv[1]
secs = float(sys.argv[sys.argv.index('--start') + 1]) if '--start' in sys.argv else 0
app = QApplication(sys.argv)
app.setStyle('Fusion')
app.setStyleSheet(mc.STYLE)
w = mc.MainWindow()
w.resize(1560, 900)
w.show()
if secs:
    w.start()
    for i, k in enumerate(sys.argv[sys.argv.index('--keys') + 1].split(',') if '--keys' in sys.argv else []):
        QTimer.singleShot(int((secs * 0.4 + i * 2.5) * 1000), lambda k=k: w.remote_key(k))


def snap():
    w.grab().save(out)
    if w.display.image is not None:
        w.display.image.save(out.replace('.png', '_raw.png'))
        w.display.image.convertToFormat(QImage.Format_RGB32).save(out.replace('.png', '_rgb.png'))
    w.remote.grab().scaled(w.remote.width() * 2, w.remote.height() * 2).save(out.replace('.png', '_remote.png'))
    w.session.stop()
    QTimer.singleShot(3500, app.quit)


QTimer.singleShot(int(max(secs, 0.5) * 1000), snap)
app.exec()
print('saved', out)
