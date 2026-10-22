# Generates docs/schematics/flight.svg and ground.svg (wiring). Usage: python tools/schematics.py docs/schematics
import html, math
E = html.escape

class Sheet:
    def __init__(s): s.o = []
    def add(s, x): s.o.append(x)
    def svg(s): return '\n'.join(s.o)

def f(v): return ('%.1f' % v).rstrip('0').rstrip('.')
def pts(p): return ' '.join(f(a) + ',' + f(b) for a, b in p)

class Card:
    def __init__(c, x, y, w, h, title, sub=''):
        c.x, c.y, c.W, c.H = x, y, w, h
        c.o = [f'<g transform="translate({x},{y})">',
               f'<rect class="card" x="0" y="0" width="{w}" height="{h}" rx="6"/>',
               f'<text class="ttl" x="14" y="24">{E(title)}</text>']
        if sub: c.o.append(f'<text class="sub" x="14" y="41">{E(sub)}</text>')
    def end(c): c.o.append('</g>'); return '\n'.join(c.o)
    # primitives
    def w(c, *p, cls='w'): c.o.append(f'<polyline class="{cls}" points="{pts(p)}"/>')
    def dot(c, x, y): c.o.append(f'<circle class="dot" cx="{f(x)}" cy="{f(y)}" r="3"/>')
    def t(c, x, y, s, cls='lbl', a='start'): c.o.append(f'<text class="{cls}" x="{f(x)}" y="{f(y)}" text-anchor="{a}">{E(s)}</text>')
    def gnd(c, x, y, lead=12):
        c.w((x, y), (x, y + lead))
        y += lead
        c.o.append(f'<path class="sym" d="M{f(x-10)},{f(y)}H{f(x+10)}M{f(x-6)},{f(y+4)}H{f(x+6)}M{f(x-2.5)},{f(y+8)}H{f(x+2.5)}"/>')
    def net(c, x, y, name, side='right', cls='net'):
        tw = len(name) * 6.9 + 14
        h = 9
        if side == 'right':
            d = f'M{f(x)},{f(y)}L{f(x+7)},{f(y-h)}H{f(x+7+tw)}V{f(y+h)}H{f(x+7)}Z'
            tx = x + 7 + tw / 2
        else:
            d = f'M{f(x)},{f(y)}L{f(x-7)},{f(y-h)}H{f(x-7-tw)}V{f(y+h)}H{f(x-7)}Z'
            tx = x - 7 - tw / 2
        c.o.append(f'<path class="{cls}" d="{d}"/>')
        c.t(tx, y + 4, name, 'nett', 'middle')
    def res_v(c, x, y1, y2, ref, val, side='right'):
        m = (y1 + y2) / 2
        c.w((x, y1), (x, m - 16)); c.w((x, m + 16), (x, y2))
        c.o.append(f'<rect class="sym" x="{f(x-6)}" y="{f(m-16)}" width="12" height="32"/>')
        if side == 'right': c.t(x + 12, m - 2, ref); c.t(x + 12, m + 12, val, 'val')
        else: c.t(x - 12, m - 2, ref, 'lbl', 'end'); c.t(x - 12, m + 12, val, 'val', 'end')
    def res_h(c, x1, x2, y, ref, val):
        m = (x1 + x2) / 2
        c.w((x1, y), (m - 16, y)); c.w((m + 16, y), (x2, y))
        c.o.append(f'<rect class="sym" x="{f(m-16)}" y="{f(y-6)}" width="32" height="12"/>')
        c.t(m, y - 12, ref, 'lbl', 'middle'); c.t(m, y + 22, val, 'val', 'middle')
    def cap_v(c, x, y1, y2, ref, val):
        m = (y1 + y2) / 2
        c.w((x, y1), (x, m - 4)); c.w((x, m + 4), (x, y2))
        c.o.append(f'<path class="sym" d="M{f(x-12)},{f(m-4)}H{f(x+12)}"/>')
        c.o.append(f'<path class="sym" d="M{f(x-12)},{f(m+6)}Q{f(x)},{f(m+1)} {f(x+12)},{f(m+6)}"/>')
        c.t(x - 16, m - 6, '+', 'val', 'end')
        c.t(x + 18, m - 2, ref); c.t(x + 18, m + 12, val, 'val')
    def batt_v(c, x, y1, y2, ref, val):
        m = (y1 + y2) / 2
        c.w((x, y1), (x, m - 8)); c.w((x, m + 8), (x, y2))
        c.o.append(f'<path class="sym" d="M{f(x-16)},{f(m-8)}H{f(x+16)}M{f(x-8)},{f(m-2)}H{f(x+8)}M{f(x-16)},{f(m+3)}H{f(x+16)}M{f(x-8)},{f(m+8)}H{f(x+8)}"/>')
        c.t(x - 20, m - 6, '+', 'lbl', 'end')
        c.t(x + 22, m - 2, ref); c.t(x + 22, m + 12, val, 'val')
    def sw_h(c, x1, x2, y, ref, val):
        c.o.append(f'<circle class="sym" cx="{f(x1)}" cy="{f(y)}" r="3"/><circle class="sym" cx="{f(x2)}" cy="{f(y)}" r="3"/>')
        c.o.append(f'<path class="sym" d="M{f(x1+3)},{f(y-1)}L{f(x2-2)},{f(y-14)}"/>')
        c.t((x1 + x2) / 2, y + 18, ref, 'lbl', 'middle'); c.t((x1 + x2) / 2, y + 31, val, 'val', 'middle')
    def term(c, x, y): c.o.append(f'<circle class="term" cx="{f(x)}" cy="{f(y)}" r="4.5"/>')
    def nmos(c, cx, cy, ref, val):
        # gate (cx-30,cy), drain (cx+10,cy-30), source (cx+10,cy+30)
        c.o.append(f'<circle class="sym thin" cx="{f(cx+2)}" cy="{f(cy)}" r="21"/>')
        c.w((cx - 30, cy), (cx - 10, cy))
        c.o.append(f'<path class="sym" d="M{f(cx-10)},{f(cy-12)}V{f(cy+12)}M{f(cx-4)},{f(cy-15)}V{f(cy-6)}M{f(cx-4)},{f(cy-4)}V{f(cy+4)}M{f(cx-4)},{f(cy+6)}V{f(cy+15)}"/>')
        c.w((cx - 4, cy - 10), (cx + 10, cy - 10), (cx + 10, cy - 30))
        c.w((cx - 4, cy + 10), (cx + 10, cy + 10), (cx + 10, cy + 30))
        c.w((cx - 4, cy), (cx + 10, cy), (cx + 10, cy + 10))
        c.o.append(f'<path class="fill" d="M{f(cx-3)},{f(cy)}L{f(cx+4)},{f(cy-4)}L{f(cx+4)},{f(cy+4)}Z"/>')
        c.t(cx + 16, cy - 16, 'D', 'pin'); c.t(cx + 16, cy + 24, 'S', 'pin'); c.t(cx - 28, cy - 5, 'G', 'pin')
        c.t(cx + 30, cy - 2, ref); c.t(cx + 30, cy + 12, val, 'val')
    def led_v(c, x, y1, y2, ref, val):
        m = (y1 + y2) / 2
        c.w((x, y1), (x, m - 9)); c.w((x, m + 9), (x, y2))
        c.o.append(f'<path class="sym" d="M{f(x-10)},{f(m-9)}H{f(x+10)}L{f(x)},{f(m+9)}Z M{f(x-10)},{f(m+9)}H{f(x+10)}"/>')
        for k in (0, 9):
            c.o.append(f'<path class="sym thin" d="M{f(x+13)},{f(m-6+k)}l9,-7m-5,0h5v5"/>')
        c.t(x + 30, m - 2, ref); c.t(x + 30, m + 12, val, 'val')
    def diode_v(c, x, y1, y2, ref, val, cls='sym'):
        # cathode at top (y1)
        m = (y1 + y2) / 2
        c.w((x, y1), (x, m - 8), cls='w opt'); c.w((x, m + 8), (x, y2), cls='w opt')
        c.o.append(f'<path class="{cls}" d="M{f(x-9)},{f(m+8)}H{f(x+9)}L{f(x)},{f(m-8)}Z M{f(x-9)},{f(m-8)}H{f(x+9)}"/>')
        c.t(x + 14, m - 2, ref); c.t(x + 14, m + 12, val, 'val')
    def buzzer_v(c, x, y1, y2, ref, val):
        m = (y1 + y2) / 2
        c.w((x, y1), (x, m - 14)); c.w((x, m + 14), (x, y2))
        c.o.append(f'<rect class="sym" x="{f(x-14)}" y="{f(m-14)}" width="28" height="28" rx="3"/>')
        c.o.append(f'<path class="sym thin" d="M{f(x-7)},{f(m+6)}Q{f(x)},{f(m-10)} {f(x+7)},{f(m+6)}"/>')
        c.t(x - 18, m - 6, '+', 'lbl', 'end')
        c.t(x + 20, m - 2, ref); c.t(x + 20, m + 12, val, 'val')
    def button_h(c, x1, x2, y, ref, val):
        c.o.append(f'<circle class="sym" cx="{f(x1)}" cy="{f(y)}" r="3"/><circle class="sym" cx="{f(x2)}" cy="{f(y)}" r="3"/>')
        m = (x1 + x2) / 2
        c.o.append(f'<path class="sym" d="M{f(x1-4)},{f(y-10)}H{f(x2+4)}M{f(m)},{f(y-10)}V{f(y-20)}M{f(m-7)},{f(y-20)}H{f(m+7)}"/>')
        c.t(m, y + 18, ref, 'lbl', 'middle'); c.t(m, y + 31, val, 'val', 'middle')
    def coil_h(c, x1, x2, y, ref, val):
        n = 5; step = (x2 - x1) / n
        d = f'M{f(x1)},{f(y)}' + ''.join(f'a{f(step/2)},{f(step/2)} 0 0 1 {f(step)},0' for _ in range(n))
        c.o.append(f'<path class="sym hot" d="{d}"/>')
        c.t((x1 + x2) / 2, y - 18, ref, 'lbl', 'middle'); c.t((x1 + x2) / 2, y + 18, val, 'val', 'middle')
    def antenna(c, x, y, ref, val):
        c.w((x, y), (x, y - 26))
        c.o.append(f'<path class="sym" d="M{f(x-10)},{f(y-40)}L{f(x)},{f(y-26)}L{f(x+10)},{f(y-40)}M{f(x)},{f(y-26)}V{f(y-42)}"/>')
        c.t(x + 16, y - 30, ref); c.t(x + 16, y - 16, val, 'val')
    def module(c, x, y, w, name, pins, side='right', pitch=26, top=40, sub=None):
        h = top + pitch * len(pins) + 6
        c.o.append(f'<rect class="mod" x="{f(x)}" y="{f(y)}" width="{f(w)}" height="{f(h)}" rx="4"/>')
        c.t(x + w / 2, y + 20, name, 'mname', 'middle')
        if sub: c.t(x + w / 2, y + 34, sub, 'val', 'middle')
        ends = []
        for i, p in enumerate(pins):
            py = y + top + 10 + i * pitch
            if side == 'right':
                c.t(x + w - 8, py + 4, p, 'pin', 'end'); ends.append((x + w, py))
            else:
                c.t(x + 8, py + 4, p, 'pin'); ends.append((x, py))
        return ends
    def note(c, x, y, lines, cls='note'):
        for i, s in enumerate(lines): c.t(x, y + i * 16, s, cls)

def wire_pin(c, end, length, side):
    x, y = end
    x2 = x + length if side == 'right' else x - length
    c.w((x, y), (x2, y))
    return (x2, y)

# ---------------------------------------------------------------- XIAO hub
def xiao_hub(c, left_nets, right_nets, bottom, title_note):
    bx, by, bw, bh = 220, 70, 150, 270
    c.o.append(f'<rect class="mcu" x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="6"/>')
    c.t(bx + bw / 2, by + 26, 'XIAO', 'mname', 'middle')
    c.t(bx + bw / 2, by + 42, 'ESP32-S3', 'val', 'middle')
    c.o.append(f'<rect class="usb" x="{bx+bw/2-22}" y="{by-10}" width="44" height="14" rx="4"/>')
    c.t(bx + bw / 2, by - 16, 'USB-C', 'val', 'middle')
    for i, (pin, net, kind) in enumerate(left_nets):
        y = by + 70 + i * 28
        c.t(bx + 8, y + 4, pin, 'pin')
        c.w((bx, y), (bx - 30, y))
        if kind == 'nc': c.t(bx - 34, y + 4, 'not connected', 'val', 'end')
        else: c.net(bx - 30, y, net, 'left', 'net' if kind != 'hot' else 'net hotnet')
    for i, (pin, net, kind) in enumerate(right_nets):
        y = by + 70 + i * 28
        c.t(bx + bw - 8, y + 4, pin, 'pin', 'end')
        c.w((bx + bw, y), (bx + bw + 30, y))
        if kind == 'nc': c.t(bx + bw + 34, y + 4, 'not connected', 'val')
        elif kind == 'gnd':
            c.w((bx + bw + 30, y), (bx + bw + 40, y)); c.gnd(bx + bw + 40, y, 4)
        elif kind == 'wio': pass
        else: c.net(bx + bw + 30, y, net, 'right')
    # Wio bracket for D10, D9, D8 (rows 3..5)
    ys = [by + 70 + i * 28 for i, r in enumerate(right_nets) if r[2] == 'wio']
    if ys:
        x0 = bx + bw + 30
        c.o.append(f'<rect class="mod opt" x="{x0}" y="{ys[0]-14}" width="184" height="{ys[-1]-ys[0]+28}" rx="4"/>')
        c.t(x0 + 92, ys[0] + 4, 'Wio-SX1262', 'mname', 'middle')
        c.t(x0 + 92, ys[0] + 19, 'plugs onto B2B + D8–D10', 'val', 'middle')
        c.t(x0 + 92, ys[0] + 33, 'GPIO 38–42: no wiring', 'val', 'middle')
    for i, (pin, net) in enumerate(bottom):
        x = bx + 45 + i * 60
        c.t(x, by + bh - 10, pin, 'pin', 'middle')
        c.w((x, by + bh), (x, by + bh + 26))
        if net == 'GND': c.gnd(x, by + bh + 26, 0)
        else: c.net(x, by + bh + 26, net, 'left')  # (x,y) is end; label to the left
    c.note(14, c.H - 14 - 16 * (len(title_note) - 1), title_note)

# ======================================================================= FLIGHT
def flight():
    cards = []
    W = 1240
    # Hub
    c = Card(0, 0, 610, 460, 'Flight computer: XIAO pin map', 'net labels: same name = connected')
    xiao_hub(c,
             [('D0', 'SENSE_BAT', ''), ('D1', 'SENSE_PYRO', ''), ('D2', 'BUZZER', ''), ('D3', 'PYRO_GATE', 'hot'),
              ('D4', 'SDA', ''), ('D5', 'SCL', ''), ('D6', 'LED', '')],
             [('5V', '', 'nc'), ('GND', '', 'gnd'), ('3V3', '3V3', ''), ('D10', '', 'wio'), ('D9', '', 'wio'),
              ('D8', '', 'wio'), ('D7', 'GPS_TX', '')],
             [('BAT+', 'BAT+ sw'), ('BAT−', 'GND')],
             ['Wio-SX1262 U.FL: 868 MHz LoRa antenna.', 'XIAO U.FL (WiFi): unused in the rocket.'])
    cards.append(c.end())

    # Power
    c = Card(630, 0, 610, 460, 'Power and battery sense', 'cell on the XIAO BAT pads through the main switch')
    c.batt_v(70, 120, 250, 'B1', '18350 3.7 V')
    c.gnd(70, 250, 14)
    c.w((70, 120), (70, 80), (150, 80))
    c.dot(110, 80); c.w((110, 80), (110, 56)); c.net(110, 56, 'CELL+', 'right', 'net hotnet')
    c.t(176, 60, 'to the pyro channel (before the switch)', 'val')
    c.sw_h(150, 200, 80, 'SW1', 'main switch')
    c.w((203, 80), (560, 80))
    c.net(560, 80, 'BAT+ sw', 'left'); c.t(560, 108, 'to XIAO BAT+ pad', 'val', 'end')
    c.dot(260, 80); c.cap_v(260, 80, 200, 'C1', '1000 µF 16 V'); c.gnd(260, 200, 14)
    c.dot(400, 80); c.res_v(400, 80, 170, 'R1', '100k')
    c.dot(400, 170); c.res_v(400, 170, 260, 'R2', '100k'); c.gnd(400, 260, 14)
    c.w((400, 170), (470, 170)); c.net(470, 170, 'SENSE_BAT', 'right')
    c.note(14, 330, ['BAT− is the pad nearest the USB connector. Check twice.',
                     'C1 rides through pyro and radio current peaks (mind the polarity).',
                     'Charge: USB-C with SW1 on. Divider ratio 2.0 = setting batt_divider.'])
    cards.append(c.end())

    # Pyro
    c = Card(0, 480, 700, 400, 'Pyro channel (deployment charge)', 'thick line = high-current loop, ≥ 22 AWG')
    c.net(80, 90, 'CELL+', 'left', 'net hotnet')
    c.w((80, 90), (120, 90), cls='w thick')
    c.term(124, 90); c.term(166, 90)
    c.o.append('<path class="sym opt" d="M124,84 Q145,62 166,84"/>')
    c.t(145, 60, 'ARM link', 'lbl', 'middle'); c.t(145, 116, 'removable', 'val', 'middle')
    c.w((170, 90), (205, 90), cls='w thick')
    c.res_h(205, 275, 90, 'R3', '1 Ω 5 W')
    c.w((275, 90), (325, 90), cls='w thick')
    c.term(329, 90); c.coil_h(334, 394, 90, 'igniter', 'nichrome'); c.term(399, 90)
    c.w((403, 90), (450, 90), (450, 200), cls='w thick')
    c.dot(450, 120); c.w((450, 120), (620, 120))
    c.res_v(620, 120, 200, 'R4', '100k'); c.dot(620, 200); c.res_v(620, 200, 280, 'R5', '100k'); c.gnd(620, 280, 12)
    c.w((600, 200), (620, 200)); c.net(600, 200, 'SENSE_PYRO', 'left')
    c.nmos(440, 230, 'Q1', 'IRLB8748')
    c.w((450, 260), (450, 300), cls='w thick'); c.gnd(450, 300, 6)
    c.t(462, 318, 'to cell − directly', 'val')
    c.net(120, 230, 'PYRO_GATE', 'left', 'net hotnet')
    c.res_h(120, 200, 230, 'R6', '100 Ω')
    c.w((200, 230), (410, 230))
    c.dot(330, 230); c.res_v(330, 230, 300, 'R7', '100k', 'left'); c.gnd(330, 300, 12)
    c.note(14, 356, ['Fit the ARM link on the pad only, after the PAD state. R7 keeps Q1 off during boot/reset.',
                     'Continuity: the drain reads ≥ 60 % of the battery through the intact bridge.'])
    cards.append(c.end())

    # Buzzer
    c = Card(720, 480, 300, 400, 'Buzzer', 'active buzzer, low-side switch')
    c.net(200, 70, 'BAT+ sw', 'left')
    c.w((200, 70), (200, 100)); c.dot(200, 100); c.buzzer_v(200, 100, 170, 'BZ1', '')
    c.dot(200, 170)
    c.w((200, 100), (150, 100), cls='w opt'); c.diode_v(150, 100, 170, '', '', 'sym opt'); c.w((150, 170), (200, 170), cls='w opt')
    c.t(138, 132, '1N4148', 'val', 'end'); c.t(138, 146, 'optional', 'val', 'end')
    c.w((200, 170), (200, 210))
    c.nmos(190, 240, 'Q2', 'IRLB8748')
    c.gnd(200, 270, 14)
    c.net(72, 240, 'BUZZER', 'left')
    c.res_h(72, 127, 240, 'R8', '100 Ω'); c.w((127, 240), (160, 240))
    c.dot(145, 240); c.res_v(145, 240, 320, '', '', 'left'); c.t(135, 296, 'R9 100k', 'val', 'end'); c.gnd(145, 320, 10)
    c.note(14, 372, ['D2 is a strapping pin: R9 is required.'])
    cards.append(c.end())

    # LED
    c = Card(1040, 480, 200, 400, 'Status LED', '')
    c.net(52, 90, 'LED', 'left')
    c.res_h(52, 102, 90, 'R10', '100 Ω')
    c.w((102, 90), (110, 90), (110, 130)); c.led_v(110, 130, 200, 'LED1', 'any'); c.gnd(110, 200, 14)
    c.note(14, 300, ['≈ 13 mA (red).', 'Patterns: see', 'docs/INDICATORS.md'])
    cards.append(c.end())

    # Sensors
    c = Card(0, 900, 760, 330, 'Sensors (I²C 400 kHz)', 'both boards on 3V3; pull-ups are on the BMP280 board')
    ends = c.module(40, 70, 170, 'MPU-9250', ['VCC', 'GND', 'SDA', 'SCL', 'CS', 'FSYNC'], 'right', 28, 34, None)
    for (x, y), (n, k) in zip(ends, [('3V3', ''), ('GND', 'g'), ('SDA', ''), ('SCL', ''), ('3V3', ''), ('GND', 'g')]):
        c.w((x, y), (x + 30, y))
        if k == 'g': c.w((x + 30, y), (x + 40, y)); c.gnd(x + 40, y, 4)
        else: c.net(x + 30, y, n, 'right')
    c.t(14, 312, 'MPU-9250: SparkFun breakout, AD0 and INT open (address 0x68).', 'note')
    ends = c.module(420, 70, 170, 'GY-BMP280-5V', ['VCC', 'GND', 'SCL', 'SDA'], 'right', 28, 34, None)
    for (x, y), (n, k) in zip(ends, [('3V3', ''), ('GND', 'g'), ('SCL', ''), ('SDA', '')]):
        c.w((x, y), (x + 30, y))
        if k == 'g': c.w((x + 30, y), (x + 40, y)); c.gnd(x + 40, y, 4)
        else: c.net(x + 30, y, n, 'right')
    c.t(505, 250, 'VCC on 3V3, never 5V', 'warn', 'middle')
    c.t(505, 266, '(its pull-ups go to VCC)', 'val', 'middle')
    cards.append(c.end())

    # GPS
    c = Card(780, 900, 460, 330, 'GPS (optional): Seeed L76K', 'four leads, not stacked on the XIAO')
    ends = c.module(40, 60, 190, 'L76K', ['3V3', 'GND', 'TX', 'RX', 'D0 position', 'D2 position', 'D6 position'], 'right', 26, 34, None)
    for (x, y), (n, k) in zip(ends, [('3V3', ''), ('GND', 'g'), ('GPS_TX', ''), ('3V3', ''), ('3V3', ''), ('3V3', ''), ('', 'nc')]):
        c.w((x, y), (x + 30, y))
        if k == 'g': c.w((x + 30, y), (x + 40, y)); c.gnd(x + 40, y, 4)
        elif k == 'nc': c.t(x + 34, y + 4, 'not connected', 'val')
        else: c.net(x + 30, y, n, 'right')
    c.note(14, 312, ['RX and the two control pins held high (Seeed); 9600 baud.'])
    cards.append(c.end())
    return W, 1230, cards

# ======================================================================= GROUND
def ground():
    cards = []
    W = 1240
    c = Card(0, 0, 610, 460, 'Ground station: XIAO pin map', 'second kit: XIAO ESP32-S3 + Wio-SX1262')
    xiao_hub(c,
             [('D0', 'SENSE_BAT', ''), ('D1', 'BUZZER', ''), ('D2', 'BUTTON', ''), ('D3', 'LED_ON', ''),
              ('D4', 'LED_LINK', ''), ('D5', '', 'nc'), ('D6', '', 'nc')],
             [('5V', '5V', ''), ('GND', '', 'gnd'), ('3V3', '', 'nc'), ('D10', '', 'wio'), ('D9', '', 'wio'),
              ('D8', '', 'wio'), ('D7', '', 'nc')],
             [('BAT+', 'BAT+ sw'), ('BAT−', 'GND')],
             ['USB-C: power bank or laptop (charges the cell).', 'Antennas: Wio U.FL = 868 MHz LoRa,',
              'XIAO U.FL = 2.4 GHz WiFi (D-Link).'])
    cards.append(c.end())

    c = Card(630, 0, 610, 460, 'Battery and battery sense (optional)', 'same as the flight kit, without C1')
    c.batt_v(70, 120, 250, 'B1', '1S Li-ion, protected')
    c.gnd(70, 250, 14)
    c.w((70, 120), (70, 80), (150, 80))
    c.sw_h(150, 200, 80, 'SW1', 'power switch')
    c.w((203, 80), (560, 80))
    c.net(560, 80, 'BAT+ sw', 'left'); c.t(560, 108, 'to XIAO BAT+ pad', 'val', 'end')
    c.dot(330, 80); c.res_v(330, 80, 170, 'R1', '100k')
    c.dot(330, 170); c.res_v(330, 170, 260, 'R2', '100k'); c.gnd(330, 260, 14)
    c.w((330, 170), (400, 170)); c.net(400, 170, 'SENSE_BAT', 'right')
    c.note(14, 330, ['Reports volts and % (# battery, UI: Radio link → Ground station).',
                     'On USB the reading is the charger voltage. No cell: leave out R1/R2.',
                     'No automatic cutoff: switch off when LOW (ON LED double-blinks).'])
    cards.append(c.end())

    c = Card(0, 480, 380, 380, 'Buzzer', 'active buzzer, low-side switch')
    c.net(230, 60, 'BAT+ sw', 'left')
    c.note(14, 350, ['BZ1 +: BAT+ sw, or 5V (USB only, louder).'])
    c.w((230, 60), (230, 100)); c.buzzer_v(230, 100, 170, 'BZ1', '')
    c.w((230, 170), (230, 210))
    c.nmos(220, 240, 'Q1', 'IRLB8748 / AO3400')
    c.gnd(230, 270, 14)
    c.net(100, 240, 'BUZZER', 'left')
    c.res_h(100, 160, 240, 'R3', '100 Ω'); c.w((160, 240), (190, 240))
    c.dot(175, 240); c.res_v(175, 240, 320, '', '', 'left'); c.t(165, 290, 'R4 100k', 'val', 'end'); c.gnd(175, 320, 10)
    cards.append(c.end())

    c = Card(400, 480, 300, 380, 'Push button', 'momentary, normally open')
    c.net(110, 120, 'BUTTON', 'left')
    c.w((110, 120), (150, 120)); c.button_h(153, 213, 120, 'S1', ''); c.w((216, 120), (260, 120), (260, 150)); c.gnd(260, 150, 4)
    c.note(14, 220, ['Internal pull-up: no resistor.', 'Short press: ping. Hold 1.5 s: search mode.',
                     'Hold 3 s: manual ejection, only within', '60 s after "Enable station button" (UI).'])
    cards.append(c.end())

    c = Card(720, 480, 520, 380, 'LEDs', 'ON and "rocket connected"')
    for i, (net, ref, col) in enumerate([('LED_ON', 'LED1', 'green'), ('LED_LINK', 'LED2', 'blue')]):
        x0 = 20 + i * 250
        c.net(x0 + 85, 90, net, 'left')
        c.res_h(x0 + 85, x0 + 135, 90, 'R%d' % (5 + i), '330 Ω')
        c.w((x0 + 135, 90), (x0 + 160, 90), (x0 + 160, 130)); c.led_v(x0 + 160, 130, 200, ref, col); c.gnd(x0 + 160, 200, 14)
    c.note(14, 280, ['≈ 4 mA each. ON: steady = running, double blink = battery low,',
                     'fast = radio failed. CONNECTED: on while frames arrive,',
                     'off for an instant at each frame. Both alternate: eject button',
                     'enabled; both blink together: ejection in progress.'])
    cards.append(c.end())
    return W, 860, cards

STYLE_FILE = '''<rect x="-2" y="-2" width="100%" height="100%" fill="#ffffff"/><style>
.card{fill:#fbfcfc;stroke:#c9d2d6;stroke-width:1}
.w{fill:none;stroke:#1e2a31;stroke-width:1.6;stroke-linejoin:round;stroke-linecap:round}
.thick{stroke-width:4;stroke:#b4471f}
.opt{stroke-dasharray:4 3}
.sym{fill:none;stroke:#1e2a31;stroke-width:1.6;stroke-linejoin:round}
.sym.thin{stroke-width:1}
.hot{stroke:#b4471f;stroke-width:2}
.fill,.dot{fill:#1e2a31}
.term{fill:#fbfcfc;stroke:#1e2a31;stroke-width:1.6}
.mod{fill:#eef2f3;stroke:#1e2a31;stroke-width:1.4}
.mcu{fill:#e3ebee;stroke:#1e2a31;stroke-width:2}
.usb{fill:#1e2a31}
.net{fill:#e1eef0;stroke:#2b6f7a;stroke-width:1.2}
.hotnet{fill:#f7e3da;stroke:#b4471f}
text{font-family:"IBM Plex Mono",ui-monospace,Menlo,Consolas,monospace}
.ttl{font:600 15px "IBM Plex Sans Condensed","Arial Narrow",sans-serif;fill:#1e2a31}
.sub{font-size:11px;fill:#5d6c74}
.lbl{font-size:12px;fill:#1e2a31}
.val{font-size:11px;fill:#5d6c74}
.pin{font-size:11px;fill:#1e2a31;font-weight:600}
.mname{font-size:13px;font-weight:600;fill:#1e2a31}
.nett{font-size:11px;fill:#1b5862;font-weight:600}
.nett2{font-size:11px;fill:#1b5862;font-weight:600}
.note{font-size:11.5px;fill:#33434b}
.warn{font-size:11px;fill:#b4471f;font-weight:600}
</style>'''

def svg(sheet, file_style):
    W, H, cards = sheet()
    body = '\n'.join(cards)
    st = STYLE_FILE if file_style else ''
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-2 -2 {W+4} {H+4}" class="schem">{st}{body}</svg>'

if __name__ == '__main__':
    import sys
    out = sys.argv[1]
    for name, sh in (('flight', flight), ('ground', ground)):
        open(f'{out}/{name}.svg', 'w', encoding='utf-8').write(svg(sh, True))
