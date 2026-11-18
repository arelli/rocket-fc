"""Checks KiCad's DRC cannot do for the flight PCB (hardware/flight-pcb), a home-made board without plated holes.

1. Solder access. A through-hole part is soldered on the face opposite the part. Nothing lying flat on that face
   may cover the joint. A module on headers ("raised") may, if the joint is soldered before the module is fitted:
   those joints give the assembly order.
2. Connections. Without plating, a track only connects to a pin on a face where that pin is soldered: the face
   opposite its part, or the part's own face when the pin is exposed there (e.g. the leads of a resistor lying
   flat; those pins are listed to be soldered on both faces). Wire vias must be reachable on both faces.
3. Keep-outs. No foreign copper under the IRLB8748's metal tab (it is the drain) and none under the M3 screw heads.
4. Tube envelope. The board is centred in a 32 mm airframe: every part must fit inside a 15.5 mm radius
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
    'U1': 10.0,   # XIAO ESP32-S3 with the Wio-SX1262 stacked on top, on low 2.54 mm headers (estimate: measure)
    'U2': 5.1,    # SparkFun MPU-9250 on headers
    'U3': 5.1,    # GY-BMP280 on headers
    'C1': 10.0,   # 10 mm electrolytic lying flat (checked as a cylinder, see CYLINDER)
    'Q1': 4.6,    # TO-220 lying flat
    'R3': 4.5,    # 1 ohm 2 W metal oxide lying flat
    'BZ1': 9.5,   # 12 mm active buzzer
    'J1': 6.1,    # JST XH 4-pin side entry
    'J3': 10.0, 'J4': 10.0,  # 5 mm screw terminals, centred
    'J5': 3.0,    # GPS lead soldered to pads
    'C2': 3.5, 'C3': 3.5,    # ceramic discs, bent flat (they sit at the board edge)
    'H1': 2.0, 'H2': 2.0, 'H3': 2.0, 'H4': 2.0,  # M3 screw heads
}
RAISED = {'U1', 'U2', 'U3'}      # modules on headers: joints underneath can be soldered before fitting them
CYLINDER = {'C1': 5.0}           # parts lying flat that are round: radius (mm), axis along the board
NO_BODY = {'J5'}                 # wire pads: nothing sits on the board
METAL_TAB = {'Q1': '2'}          # body is metal on the board face, connected to this pad: no other net under it
SCREW_HEAD_R = 3.0               # keep copper this far from the centre of an M3 hole (screw head / nut)


def mm(v):
    return pcbnew.ToMM(v)


def other(layer):
    return pcbnew.B_Cu if layer == pcbnew.F_Cu else pcbnew.F_Cu


def lname(layer):
    return 'F.Cu' if layer == pcbnew.F_Cu else 'B.Cu'


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
                pts.append((mm(v.x), mm(v.y)))
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def body_box(fp):
    """Body extent on the board face from the Fab drawing: its rectangles / polygons / circles when it has a
    rectangle or polygon (lines are then leads or marks), otherwise all its shapes."""
    if fp.GetReference() in NO_BODY:
        return None
    layer = pcbnew.F_Fab if fp.GetLayer() == pcbnew.F_Cu else pcbnew.B_Fab
    shapes = [g for g in fp.GraphicalItems() if g.GetLayer() == layer and g.Type() == pcbnew.PCB_SHAPE_T]
    if any(g.GetShape() in (pcbnew.SHAPE_T_RECTANGLE, pcbnew.SHAPE_T_POLY) for g in shapes):
        shapes = [g for g in shapes if g.GetShape() != pcbnew.SHAPE_T_SEGMENT]
    if not shapes:
        return None
    bbs = [g.GetBoundingBox() for g in shapes]
    return (min(mm(b.GetLeft()) for b in bbs), min(mm(b.GetTop()) for b in bbs),
            max(mm(b.GetRight()) for b in bbs), max(mm(b.GetBottom()) for b in bbs))


def pad_radius(pad):
    return mm(max(pad.GetSize(pcbnew.F_Cu).x, pad.GetSize(pcbnew.F_Cu).y)) / 2


def in_box(x, y, box, grow=0.0):
    return box[0] - grow < x < box[2] + grow and box[1] - grow < y < box[3] + grow


class Model:
    """Bodies, faces and where every pin can be soldered."""

    def __init__(self, board):
        self.board = board
        self.fps = {fp.GetReference(): fp for fp in board.GetFootprints()}
        self.layer = {r: fp.GetLayer() for r, fp in self.fps.items()}
        self.body = {r: body_box(fp) for r, fp in self.fps.items()}
        self.errors, self.order, self.solder = [], {}, {}
        for r, fp in self.fps.items():
            for p in fp.Pads():
                if p.GetAttribute() != pcbnew.PAD_ATTRIB_PTH:
                    continue
                x, y, rad = mm(p.GetPosition().x), mm(p.GetPosition().y), pad_radius(p)
                layers = set()
                own = self.layer[r]                     # the part's own face: only if the pin is exposed there
                b = self.body[r]
                if (b is None or not in_box(x, y, b)) and not self.covering(r, x, y, rad, own):
                    layers.add(own)
                face = other(own)                       # the usual solder face
                blockers = self.covering(r, x, y, rad, face)
                flat = [o for o in blockers if o not in RAISED]
                if not flat:
                    layers.add(face)
                    if not layers - {face}:             # only that face: modules over it fix the order
                        for o in blockers:
                            self.order.setdefault(o, set()).add(r)
                if not layers:
                    self.errors.append('%s pad %s is under %s: it cannot be soldered' % (r, p.GetNumber(), flat[0]))
                self.solder[(r, p.GetNumber())] = layers

    def covering(self, ref, x, y, rad, face):
        """Parts on `face` (other than ref) whose body covers (x, y)."""
        return [o for o, b in self.body.items()
                if o != ref and b is not None and self.layer[o] == face and in_box(x, y, b, rad)]


def seg_points(t, step=0.2):
    a, b = t.GetStart(), t.GetEnd()
    n = max(1, int(math.hypot(mm(b.x - a.x), mm(b.y - a.y)) / step))
    return [pcbnew.VECTOR2I(int(a.x + (b.x - a.x) * i / n), int(a.y + (b.y - a.y) * i / n)) for i in range(n + 1)]


def check_copper(model):
    board, errors, both = model.board, model.errors, set()
    pads_by_net = {}
    for r, fp in model.fps.items():
        for p in fp.Pads():
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                pads_by_net.setdefault(p.GetNetname(), []).append((r, p))
    holes = [(mm(fp.GetPosition().x), mm(fp.GetPosition().y)) for r, fp in model.fps.items() if r.startswith('H')]
    for t in board.GetTracks():
        net = t.GetNetname()
        if t.Type() == pcbnew.PCB_VIA_T:
            x, y = mm(t.GetPosition().x), mm(t.GetPosition().y)
            rad = mm(t.GetWidth(pcbnew.F_Cu)) / 2
            for face in (pcbnew.F_Cu, pcbnew.B_Cu):
                for o in model.covering(None, x, y, rad, face):
                    if o in RAISED:
                        model.order.setdefault(o, set()).add('via %s (%.1f, %.1f)' % (net, x, y))
                    else:
                        errors.append('via %s at (%.1f, %.1f) is under %s: it cannot be soldered' % (net, x, y, o))
            for hx, hy in holes:
                if math.hypot(x - hx, y - hy) < SCREW_HEAD_R + rad:
                    errors.append('via %s at (%.1f, %.1f) is under an M3 screw head' % (net, x, y))
            continue
        layer, pts = t.GetLayer(), seg_points(t)
        half = mm(t.GetWidth()) / 2
        for r, p in pads_by_net.get(net, []):
            if any(p.HitTest(q) for q in pts):
                ok = model.solder.get((r, p.GetNumber()), set())
                if layer not in ok:
                    errors.append('track %s on %s meets %s pad %s, which is soldered only on %s'
                                  % (net, lname(layer), r, p.GetNumber(), ' and '.join(lname(l) for l in ok) or 'no face'))
                elif layer == model.layer[r]:
                    both.add('%s pad %s' % (r, p.GetNumber()))
        for ref, padnum in METAL_TAB.items():
            fp, b = model.fps.get(ref), model.body.get(ref)
            if fp is None or b is None or layer != fp.GetLayer():
                continue
            tab_net = [p.GetNetname() for p in fp.Pads() if p.GetNumber() == padnum][0]
            if net != tab_net and any(in_box(mm(q.x), mm(q.y), b, half) for q in pts):
                errors.append('track %s on %s runs under the metal tab of %s' % (net, lname(layer), ref))
        for hx, hy in holes:
            if any(math.hypot(mm(q.x) - hx, mm(q.y) - hy) < SCREW_HEAD_R + half for q in pts):
                errors.append('track %s on %s runs under an M3 screw head' % (net, lname(layer)))
                break
    return sorted(both)


def check_tube(model, cx):
    print('Tube fit (radius %.1f mm):' % R_TUBE)
    for r in sorted(model.body, key=lambda k: (lname(model.layer[k]), k)):
        b = model.body[r]
        if b is None:
            continue
        h, known = height(r)
        if r in CYLINDER:
            rad = CYLINDER[r]
            margin = R_TUBE - (math.hypot((b[0] + b[2]) / 2 - cx, HALF_T + h - rad) + rad)
        else:
            off = max(abs(b[0] - cx), abs(b[2] - cx))
            margin = math.sqrt(max(R_TUBE ** 2 - off ** 2, 0)) - HALF_T - h
        if margin < 0:
            model.errors.append('%s does not fit the tube (%.1f mm over)' % (r, -margin))
        if margin < 1.0 or not known:
            print('  %s %-4s %s height %4.1f  margin %4.1f mm%s' % ('OK ' if margin >= 0 else 'ERR', r,
                  lname(model.layer[r])[0], h, margin, '' if known else '  (height not listed)'))
    print('  (parts with 1 mm margin or more not listed)')


def main(path):
    board = pcbnew.LoadBoard(path)
    x0, y0, x1, y1 = outline(board)
    model = Model(board)
    print('Board %.1f x %.1f mm' % (x1 - x0, y1 - y0))
    check_tube(model, (x0 + x1) / 2)
    for r, b in model.body.items():
        if b and (b[0] < x0 - 0.01 or b[2] > x1 + 0.01 or b[1] < y0 - 0.01 or b[3] > y1 + 0.01):
            model.errors.append('%s sticks out of the board outline' % r)
    both = check_copper(model)
    if model.order:
        print('Assembly order (joints under a module must be soldered before it is fitted):')
        for o, rs in sorted(model.order.items()):
            print('  before %s (%s): %s' % (o, lname(model.layer[o]), ', '.join(sorted(rs))))
    if both:
        print('Solder on both faces (copper on the part side too):', ', '.join(both))
    for e in model.errors:
        print('ERROR:', e)
    print('pcb_check:', 'FAILED' if model.errors else 'ok')
    return 1 if model.errors else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'hardware/flight-pcb/rocket-fc-flight.kicad_pcb'))
