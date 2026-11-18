"""Placement checks KiCad's DRC cannot do for the flight PCB (hardware/flight-pcb).

1. Solder access. The board is home-made, without plated holes: a through-hole part is soldered on the face
   opposite the part. Nothing lying flat on that face may cover the joint. A module on headers ("raised")
   may, if the joint is soldered before the module is fitted: those are listed as the assembly order.
2. Tube envelope. The board is centred in a 32 mm airframe: every part must fit inside a 15.5 mm radius
   (0.5 mm clearance), taking its height and its distance from the centre line.

Run with KiCad's Python (it has the pcbnew module):
    "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" tools/pcb_check.py hardware/flight-pcb/rocket-fc-flight.kicad_pcb
tools/pcb_export.py runs it as part of its check step. Exit code 1 on any error.

Heights are mm above the board face, mostly measured or from the KiCad 3D models; update HEIGHT when a part
changes. Parts not listed are assumed low (2.7 mm, a resistor lying flat).
"""
import math
import sys

import pcbnew

R_TUBE = 15.5          # 32 mm inner diameter, 0.5 mm clearance
HALF_T = 0.8           # half the board thickness
DEFAULT_HEIGHT = 2.7

HEIGHT = {
    'U1': 9.0,    # XIAO ESP32-S3 + Wio-SX1262 on 2.54 mm headers (estimate: measure the stack)
    'U2': 5.1,    # SparkFun MPU-9250 on headers
    'U3': 5.1,    # GY-BMP280 on headers
    'C1': 10.0,   # 10 mm electrolytic lying flat (checked as a cylinder, see CYLINDER)
    'Q1': 4.6,    # TO-220 lying flat
    'R3': 9.5,    # 5 W cement resistor
    'BZ1': 9.5,   # 12 mm active buzzer
    'J1': 6.1, 'J2': 6.1,    # JST XH side entry
    'J3': 10.0, 'J4': 10.0,  # 5 mm screw terminals (measure yours: 10.4 mm is the limit where they sit)
    'J5': 3.0,    # GPS lead soldered to pads
    'D1': 5.9,    # 5 mm LED lying flat
    'C2': 6.0, 'C3': 6.0,    # ceramic discs
    'H1': 2.0, 'H2': 2.0,    # M3 screw heads
}
RAISED = {'U1', 'U2', 'U3'}          # modules on headers: joints underneath can be soldered before fitting them
CYLINDER = {'C1': 5.0}               # parts lying flat that are round: radius (mm), axis along the board
BODY_TRIM = {'R3': 2.74}             # lead length at each end of a body drawn with its leads on the Fab layer


def height(ref):
    if ref in HEIGHT:
        return HEIGHT[ref], True
    if ref.startswith('R'):
        return DEFAULT_HEIGHT, True   # resistors lie flat
    return DEFAULT_HEIGHT, False


def outline(board):
    """Edge.Cuts extent, ignoring the line width."""
    pts = []
    for d in board.GetDrawings():
        if d.GetLayer() == pcbnew.Edge_Cuts and d.Type() == pcbnew.PCB_SHAPE_T:
            for v in (d.GetStart(), d.GetEnd()):
                pts.append((pcbnew.ToMM(v.x), pcbnew.ToMM(v.y)))
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def fab_box(fp):
    """Body extent from the Fab-layer drawing (shapes only, not text)."""
    layer = pcbnew.F_Fab if fp.GetLayer() == pcbnew.F_Cu else pcbnew.B_Fab
    boxes = [g.GetBoundingBox() for g in fp.GraphicalItems()
             if g.GetLayer() == layer and g.Type() == pcbnew.PCB_SHAPE_T]
    if not boxes:
        return None
    b = [min(pcbnew.ToMM(x.GetLeft()) for x in boxes), min(pcbnew.ToMM(x.GetTop()) for x in boxes),
         max(pcbnew.ToMM(x.GetRight()) for x in boxes), max(pcbnew.ToMM(x.GetBottom()) for x in boxes)]
    t = BODY_TRIM.get(fp.GetReference(), 0)
    if t:
        if b[3] - b[1] > b[2] - b[0]:
            b[1], b[3] = b[1] + t, b[3] - t
        else:
            b[0], b[2] = b[0] + t, b[2] - t
    return b


def main(path):
    board = pcbnew.LoadBoard(path)
    x0, y0, x1, y1 = outline(board)
    cx = (x0 + x1) / 2
    fps = {fp.GetReference(): fp for fp in board.GetFootprints()}
    side = {r: 'top' if fp.GetLayer() == pcbnew.F_Cu else 'bottom' for r, fp in fps.items()}
    body = {r: fab_box(fp) for r, fp in fps.items()}
    errors, order = [], {}

    for r, fp in fps.items():
        for p in fp.Pads():
            if p.GetAttribute() != pcbnew.PAD_ATTRIB_PTH:
                continue
            px, py = pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)
            rad = pcbnew.ToMM(max(p.GetSize().x, p.GetSize().y)) / 2
            for o, b in body.items():
                if o == r or side[o] == side[r] or b is None:
                    continue
                if b[0] - rad < px < b[2] + rad and b[1] - rad < py < b[3] + rad:
                    if o in RAISED:
                        order.setdefault(o, set()).add(r)
                    else:
                        errors.append('%s pad %s is under %s (%s): it cannot be soldered' % (r, p.GetNumber(), o, side[o]))

    print('Tube fit (radius %.1f mm, board %.1f x %.1f mm):' % (R_TUBE, x1 - x0, y1 - y0))
    for r in sorted(body, key=lambda k: (side[k], k)):
        b = body[r]
        if b is None:
            continue
        h, known = height(r)
        if r in CYLINDER:
            rad = CYLINDER[r]
            reach = math.hypot((b[0] + b[2]) / 2 - cx, HALF_T + h - rad) + rad
            margin = R_TUBE - reach
        else:
            off = max(abs(b[0] - cx), abs(b[2] - cx))
            margin = math.sqrt(max(R_TUBE ** 2 - off ** 2, 0)) - HALF_T - h
        flag = 'OK ' if margin >= 0 else 'ERR'
        if margin < 0:
            errors.append('%s does not fit the tube (%.1f mm over)' % (r, -margin))
        if margin < 1.0 or not known:
            print('  %s %-4s %-6s height %4.1f  margin %4.1f mm%s' % (flag, r, side[r], h, margin,
                                                                      '' if known else '  (height not listed)'))
        if b[0] < x0 - 0.01 or b[2] > x1 + 0.01 or b[1] < y0 - 0.01 or b[3] > y1 + 0.01:
            errors.append('%s sticks out of the board outline' % r)
    print('  (parts with 1 mm margin or more not listed)')

    if order:
        print('Assembly order (joints under a module must be soldered before it is fitted):')
        for o, rs in sorted(order.items()):
            print('  before %s (%s): %s' % (o, side[o], ', '.join(sorted(rs))))
    for e in errors:
        print('ERROR:', e)
    print('pcb_check:', 'FAILED' if errors else 'ok')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'hardware/flight-pcb/rocket-fc-flight.kicad_pcb'))
