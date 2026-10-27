"""Model of the Manta LCD4214 remote control (layout taken from a photo of the real remote), drawn with Qt.

Two input modes: KEYS (the computer keyboard drives the remote, every key shows its binding) and MOUSE (the keys are clicked on the model).
A key whose function has no evidence-backed IR code in configs/input/ir-keymap.tsv is drawn with a dotted outline: it is still sent to the
emulator (which logs it as unmapped) but the original firmware cannot receive it yet.
"""
from PySide6.QtCore import QPointF, QRectF, Qt, QTimer, Signal
from PySide6.QtGui import QBrush, QColor, QFont, QKeySequence, QPainter, QPainterPath, QPen, QPolygonF
from PySide6.QtWidgets import QSizePolicy, QToolTip, QWidget

W, H = 343.0, 905.0   # coordinate system = the photo of the remote


class RKey:
    def __init__(self, fn, cx, cy, w, h, text='', cap='', kind='grey', glyph='', bind=None, title=''):
        self.fn, self.cx, self.cy, self.w, self.h = fn, cx, cy, w, h
        self.text, self.cap, self.kind, self.glyph = text, cap, kind, glyph
        self.bind = bind or []          # Qt.Key values (or QKeyCombination-free ints)
        self.title = title or fn
        self.path = QPainterPath()
        self.path.addRoundedRect(QRectF(cx - w / 2, cy - h / 2, w, h), 8 if kind != 'power' else h / 2, 8 if kind != 'power' else h / 2)


def _dpad():
    """Four wedge-shaped arrow keys around the OK button, like the photo."""
    keys = []
    cx, cy, ro, ri = 176.0, 702.0, 98.0, 42.0
    for fn, start in (('RIGHT', -45), ('UP', 45), ('LEFT', 135), ('DOWN', 225)):
        p = QPainterPath()
        outer = QRectF(cx - ro, cy - ro, 2 * ro, 2 * ro)
        inner = QRectF(cx - ri - 4, cy - ri - 4, 2 * (ri + 4), 2 * (ri + 4))
        p.arcMoveTo(outer, start + 3)
        p.arcTo(outer, start + 3, 84)
        p.arcTo(inner, start + 87, -84)
        p.closeSubpath()
        k = RKey(fn, cx, cy, 10, 10, kind='dpad', title=fn)
        k.path = p
        keys.append(k)
    return keys


def build_keys():
    K = Qt.Key
    keys = [
        RKey('POWER', 176, 120, 90, 36, kind='power', glyph='power', bind=[K.Key_P], title='POWER'),
        RKey('WIDE', 84, 175, 54, 25, '16:9', 'WIDE', bind=[K.Key_W], title='WIDE (aspect)'),
        RKey('SMART_S', 146, 175, 54, 25, 'SMART S', bind=[K.Key_Q], title='SMART S (sound mode)'),
        RKey('SMART_P', 206, 175, 54, 25, 'SMART P', bind=[K.Key_E], title='SMART P (picture mode)'),
        RKey('MUTE', 266, 175, 54, 25, '', 'MUTE', glyph='mute', bind=[K.Key_X], title='MUTE'),
    ]
    digit_caps = {1: '', 2: 'ABC', 3: 'DEF', 4: 'GHI', 5: 'JKL', 6: 'MNO', 7: 'PQR', 8: 'STU', 9: 'VWX', 0: ''}
    rows = [(222, ['1', '2', '3']), (262, ['4', '5', '6']), (302, ['7', '8', '9'])]
    caps = {'1': '', '2': 'ABC', '3': 'DEF', '4': 'GHI', '5': 'JKL', '6': 'MNO', '7': 'PQR', '8': 'STU', '9': 'VWX'}
    # the photo prints the letters above the digit keys of the row below them: 1 has none, 2 ABC, 3 DEF ...
    for y, row in rows:
        for i, d in enumerate(row):
            keys.append(RKey('DIGIT_' + d, (84, 146, 206)[i], y, 54, 25, d, caps[d], bind=[getattr(K, 'Key_' + d)], title='Digit ' + d))
    keys.append(RKey('DIGIT_0', 266, 302, 54, 25, '0', '', bind=[K.Key_0], title='Digit 0'))
    keys += [
        RKey('LISTICON', 266, 222, 54, 25, '', glyph='list', bind=[K.Key_L], title='Teletext / list key'),
        RKey('3D', 266, 262, 54, 25, '3D', bind=[K.Key_D], title='3D'),
        RKey('INFO', 84, 345, 54, 25, 'INFO', bind=[K.Key_I], title='INFO'),
        RKey('SLEEP', 146, 345, 54, 25, '', glyph='clock', bind=[K.Key_Z], title='Sleep timer (clock)'),
        RKey('REPEAT', 206, 345, 54, 25, '', glyph='repeat', bind=[K.Key_Y], title='Repeat'),
        RKey('LR', 266, 345, 54, 25, 'L/R', bind=[K.Key_R], title='L/R (3D left/right)'),
        RKey('INDEX', 84, 397, 54, 25, '', 'INDEX', glyph='play', bind=[K.Key_F1], title='INDEX / play'),
        RKey('ZOOM', 146, 397, 54, 25, '', 'ZOOM', glyph='pause', bind=[K.Key_F2], title='ZOOM / pause'),
        RKey('REVEAL', 206, 397, 54, 25, '', 'REVEAL', glyph='stop', bind=[K.Key_F3], title='REVEAL / stop'),
        RKey('TTX', 266, 397, 54, 25, '', 'TTX', glyph='ttx', bind=[K.Key_F4], title='Teletext'),
        RKey('HOLD', 84, 438, 54, 25, '', 'HOLD', glyph='prev', bind=[K.Key_F5], title='HOLD / previous'),
        RKey('LIST', 146, 438, 54, 25, '', 'LIST', glyph='next', bind=[K.Key_F6], title='LIST / next'),
        RKey('LANGUAGE', 206, 438, 54, 25, '', 'LANGUAGE', glyph='rew', bind=[K.Key_F7], title='LANGUAGE / rewind'),
        RKey('RADIO', 266, 438, 54, 25, '', 'RADIO', glyph='ff', bind=[K.Key_F8], title='RADIO / fast forward'),
        RKey('REC', 80, 495, 54, 25, '', 'REC', kind='red', bind=[K.Key_C], title='REC'),
        RKey('SUBTITLE', 146, 495, 54, 25, '', 'SUBTITLE', kind='green', bind=[K.Key_H], title='SUBTITLE'),
        RKey('FAV', 206, 495, 54, 25, '', 'FAV', kind='yellow', bind=[K.Key_F], title='FAV'),
        RKey('EPG', 270, 495, 54, 25, '', 'EPG', kind='blue', bind=[K.Key_G], title='EPG'),
        RKey('VOL_DOWN', 80, 538, 54, 25, 'VOL−', bind=[K.Key_BracketLeft, K.Key_Minus], title='Volume down'),
        RKey('VOL_UP', 146, 538, 54, 25, 'VOL+', bind=[K.Key_BracketRight, K.Key_Equal], title='Volume up'),
        RKey('CH_DOWN', 206, 538, 54, 25, 'CH−', bind=[K.Key_PageDown], title='Channel down'),
        RKey('CH_UP', 270, 538, 54, 25, 'CH+', bind=[K.Key_PageUp], title='Channel up'),
        RKey('MENU', 80, 580, 54, 25, 'MENU', bind=[K.Key_M], title='MENU'),
        RKey('SOURCE', 270, 580, 54, 25, 'SOURCE', bind=[K.Key_S], title='SOURCE'),
    ]
    dp = _dpad()
    for k in dp:
        k.bind = {'UP': [K.Key_Up], 'DOWN': [K.Key_Down], 'LEFT': [K.Key_Left], 'RIGHT': [K.Key_Right]}[k.fn]
    keys += dp
    ok = RKey('OK', 176, 702, 76, 76, 'OK', kind='ok', bind=[K.Key_Return, K.Key_Enter, K.Key_Space], title='OK')
    ok.path = QPainterPath()
    ok.path.addEllipse(QPointF(176, 702), 38, 38)
    keys.append(ok)
    return keys


KEYS = build_keys()
_seen = {}
for _k in KEYS:
    for _b in _k.bind:
        assert _b not in _seen, 'duplicate binding %s / %s' % (_k.fn, _seen[_b])
        _seen[_b] = _k.fn
BINDINGS = _seen   # Qt.Key -> function


def key_label(qt_key):
    s = QKeySequence(int(qt_key)).toString()
    return {'Return': 'Enter', 'BracketLeft': '[', 'BracketRight': ']'}.get(s, s)


class RemoteWidget(QWidget):
    keyPressed = Signal(str)       # function name, e.g. "MENU", "DIGIT_5"
    modeChanged = Signal(str)

    def __init__(self, mapped_functions=()):
        super().__init__()
        self.mapped = set(mapped_functions)
        self.mode = 'keys'
        self.down = {}             # function -> remaining flash ticks
        self.hover = None
        self.setMouseTracking(True)
        self.setMinimumSize(190, 500)
        self.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Expanding)
        self._timer = QTimer(self)
        self._timer.setInterval(40)
        self._timer.timeout.connect(self._tick)

    # --- state -----------------------------------------------------------------
    def set_mode(self, mode):
        if mode != self.mode:
            self.mode = mode
            self.update()
            self.modeChanged.emit(mode)

    def set_mapped(self, functions):
        self.mapped = set(functions)
        self.update()

    def flash(self, fn):
        self.down[fn] = 4
        if not self._timer.isActive():
            self._timer.start()
        self.update()

    def _tick(self):
        for fn in list(self.down):
            self.down[fn] -= 1
            if self.down[fn] <= 0:
                del self.down[fn]
        if not self.down:
            self._timer.stop()
        self.update()

    # --- geometry ---------------------------------------------------------------
    def _xf(self):
        s = min(self.width() / W, self.height() / H)
        return s, (self.width() - W * s) / 2, (self.height() - H * s) / 2

    def _hit(self, pos):
        s, ox, oy = self._xf()
        p = QPointF((pos.x() - ox) / s, (pos.y() - oy) / s)
        for k in reversed(KEYS):
            if k.path.contains(p):
                return k
        return None

    # --- input -------------------------------------------------------------------
    def mousePressEvent(self, e):
        if e.button() != Qt.LeftButton:
            return
        k = self._hit(e.position())
        if k is None:
            return
        if self.mode == 'mouse':
            self.flash(k.fn)
            self.keyPressed.emit(k.fn)

    def mouseMoveEvent(self, e):
        k = self._hit(e.position())
        if (k.fn if k else None) != self.hover:
            self.hover = k.fn if k else None
            self.update()
        if k:
            tip = k.title + ('' if k.fn in self.mapped else '\nno evidence-backed IR code for the original firmware yet')
            if k.bind and self.mode == 'keys':
                tip += '\nkeyboard: ' + ' / '.join(key_label(b) for b in k.bind)
            QToolTip.showText(e.globalPosition().toPoint(), tip, self)
        else:
            QToolTip.hideText()

    def leaveEvent(self, e):
        self.hover = None
        self.update()

    # --- painting ----------------------------------------------------------------
    def paintEvent(self, e):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        s, ox, oy = self._xf()
        p.translate(ox, oy)
        p.scale(s, s)
        # body
        body = QPainterPath()
        body.moveTo(60, 12)
        body.lineTo(283, 12)
        body.quadTo(325, 12, 325, 60)
        body.lineTo(325, 770)
        body.quadTo(325, 830, 270, 872)
        body.quadTo(176, 915, 82, 872)
        body.quadTo(24, 830, 24, 770)
        body.lineTo(24, 60)
        body.quadTo(24, 12, 60, 12)
        p.setPen(QPen(QColor('#2a2a2e'), 3))
        p.setBrush(QBrush(QColor('#151517')))
        p.drawPath(body)
        # grey plate around the key field (like the printed area on the photo)
        p.setPen(Qt.NoPen)
        p.setBrush(QBrush(QColor('#1c1c1f')))
        p.drawRoundedRect(QRectF(34, 70, 275, 548), 18, 18)
        capfont = QFont('Segoe UI', 1)
        capfont.setPixelSize(10)
        capfont.setBold(True)
        keyfont = QFont('Segoe UI', 1)
        keyfont.setPixelSize(12)
        keyfont.setBold(True)
        binds = QFont('Segoe UI', 1)
        binds.setPixelSize(8)
        for k in KEYS:
            self._draw_key(p, k, capfont, keyfont, binds)
        # logo
        p.setPen(QColor('#d8d8d8'))
        lf = QFont('Segoe UI', 1)
        lf.setPixelSize(30)
        lf.setBold(True)
        p.setFont(lf)
        p.drawText(QRectF(24, 800, 301, 50), Qt.AlignCenter, 'MANTA')
        p.end()

    def _draw_key(self, p, k, capfont, keyfont, binds):
        pressed = k.fn in self.down
        hover = k.fn == self.hover
        colors = {'grey': '#8a8a8e', 'power': '#e02020', 'red': '#d82020', 'green': '#169a5a', 'yellow': '#e8c820', 'blue': '#2a8ae0',
                  'dpad': '#7a7a7e', 'ok': '#8e8e92'}
        base = QColor(colors[k.kind])
        if pressed:
            base = base.lighter(150)
        elif hover and self.mode == 'mouse':
            base = base.lighter(118)
        unmapped = k.fn not in self.mapped
        p.setBrush(QBrush(base))
        pen = QPen(QColor('#101012'), 2)
        if unmapped:
            pen = QPen(QColor('#ffb030') if pressed else QColor('#4a4a50'), 2, Qt.DotLine)
        p.setPen(pen)
        p.drawPath(k.path)
        # caption printed above the key
        if k.cap and k.kind != 'dpad':
            p.setFont(capfont)
            p.setPen(QColor('#f2f2f2'))
            p.drawText(QRectF(k.cx - 34, k.cy - k.h / 2 - 14, 68, 13), Qt.AlignCenter, k.cap)
        r = QRectF(k.cx - k.w / 2, k.cy - k.h / 2, k.w, k.h)
        fg = QColor('#101012') if k.kind in ('grey', 'yellow', 'ok', 'dpad') else QColor('#ffffff')
        p.setPen(fg)
        if k.glyph:
            self._glyph(p, k, r, fg)
        elif k.kind == 'dpad':
            self._arrow(p, k, fg)
        elif k.text:
            f = QFont(keyfont)
            if k.kind == 'ok':
                f.setPixelSize(20)
            p.setFont(f)
            tr = r.adjusted(0, -2, 0, -3) if k.kind != 'ok' else QRectF(k.cx - 30, k.cy - 30, 60, 60)
            p.drawText(tr, Qt.AlignCenter, k.text)
        # keyboard binding badge (KEYS mode) or function name
        if self.mode == 'keys' and k.bind and k.kind != 'dpad':
            p.setFont(binds)
            p.setPen(QColor('#ffe08a'))
            lbl = key_label(k.bind[0])
            br = QRectF(r.right() - 17, r.bottom() - 8, 19, 10)
            p.setBrush(QColor(0, 0, 0, 150))
            p.setPen(Qt.NoPen)
            p.drawRoundedRect(br, 3, 3)
            p.setPen(QColor('#ffe08a'))
            p.drawText(br, Qt.AlignCenter, lbl[:4])
        elif self.mode == 'keys' and k.kind == 'dpad':
            p.setFont(binds)
            p.setPen(QColor('#ffe08a'))
            c = k.path.boundingRect().center()
            p.drawText(QRectF(c.x() - 10, c.y() + 8, 20, 12), Qt.AlignCenter, '↑↓←→'[('UP', 'DOWN', 'LEFT', 'RIGHT').index(k.fn)])

    def _arrow(self, p, k, fg):
        c = k.path.boundingRect().center()
        dx, dy = {'UP': (0, -1), 'DOWN': (0, 1), 'LEFT': (-1, 0), 'RIGHT': (1, 0)}[k.fn]
        # centre of the wedge is farther from the pad centre than the bounding-box centre
        cx, cy = 176 + dx * 70, 702 + dy * 70
        pts = [QPointF(cx + dx * 12, cy + dy * 12), QPointF(cx - dx * 8 + dy * 12, cy - dy * 8 + dx * 12), QPointF(cx - dx * 8 - dy * 12, cy - dy * 8 - dx * 12)]
        p.setPen(Qt.NoPen)
        p.setBrush(fg)
        p.drawPolygon(QPolygonF(pts))

    def _glyph(self, p, k, r, fg):
        g, c = k.glyph, r.center()
        p.setPen(Qt.NoPen)
        p.setBrush(fg)
        pen = QPen(fg, 2.2)
        if g == 'power':
            p.setBrush(Qt.NoBrush)
            p.setPen(QPen(QColor('#ffffff'), 3))
            p.drawArc(QRectF(c.x() - 11, c.y() - 11, 22, 22), 120 * 16, -300 * 16)
            p.drawLine(QPointF(c.x(), c.y() - 13), QPointF(c.x(), c.y() - 2))
        elif g == 'mute':
            p.drawPolygon(QPolygonF([QPointF(c.x() - 10, c.y() - 4), QPointF(c.x() - 4, c.y() - 4), QPointF(c.x() + 3, c.y() - 10), QPointF(c.x() + 3, c.y() + 10),
                                     QPointF(c.x() - 4, c.y() + 4), QPointF(c.x() - 10, c.y() + 4)]))
            p.setPen(pen)
            p.drawLine(QPointF(c.x() + 6, c.y() - 5), QPointF(c.x() + 13, c.y() + 5))
            p.drawLine(QPointF(c.x() + 13, c.y() - 5), QPointF(c.x() + 6, c.y() + 5))
        elif g == 'list':
            p.setPen(pen)
            for i in (-6, 0, 6):
                p.drawLine(QPointF(c.x() - 9, c.y() + i), QPointF(c.x() + 9, c.y() + i))
        elif g == 'clock':
            p.setBrush(Qt.NoBrush)
            p.setPen(pen)
            p.drawEllipse(c, 9, 9)
            p.drawLine(c, QPointF(c.x(), c.y() - 6))
            p.drawLine(c, QPointF(c.x() + 4, c.y() + 2))
        elif g == 'repeat':
            p.setBrush(Qt.NoBrush)
            p.setPen(pen)
            p.drawArc(QRectF(c.x() - 9, c.y() - 7, 18, 14), 30 * 16, 300 * 16)
            p.setBrush(fg)
            p.drawPolygon(QPolygonF([QPointF(c.x() + 11, c.y() - 7), QPointF(c.x() + 3, c.y() - 7), QPointF(c.x() + 8, c.y() - 1)]))
        elif g == 'play':
            p.drawPolygon(QPolygonF([QPointF(c.x() - 6, c.y() - 8), QPointF(c.x() - 6, c.y() + 8), QPointF(c.x() + 8, c.y())]))
        elif g == 'pause':
            p.drawRect(QRectF(c.x() - 7, c.y() - 8, 5, 16))
            p.drawRect(QRectF(c.x() + 2, c.y() - 8, 5, 16))
        elif g == 'stop':
            p.drawRect(QRectF(c.x() - 7, c.y() - 7, 14, 14))
        elif g == 'ttx':
            p.setBrush(Qt.NoBrush)
            p.setPen(pen)
            p.drawArc(QRectF(c.x() - 8, c.y() - 8, 16, 16), 60 * 16, 290 * 16)
            p.setBrush(fg)
            p.setPen(Qt.NoPen)
            p.drawPolygon(QPolygonF([QPointF(c.x() + 1, c.y() - 11), QPointF(c.x() + 9, c.y() - 6), QPointF(c.x() + 1, c.y() - 3)]))
        elif g in ('prev', 'next', 'rew', 'ff'):
            right = g in ('next', 'ff')
            d = 1 if right else -1
            for off in ((-5, 4) if g in ('rew', 'ff') else (0,)):
                x = c.x() + off
                p.drawPolygon(QPolygonF([QPointF(x - d * 6, c.y() - 7), QPointF(x - d * 6, c.y() + 7), QPointF(x + d * 5, c.y())]))
            if g in ('prev', 'next'):
                p.drawRect(QRectF(c.x() + d * 7 - 1.5, c.y() - 7, 3, 14))
