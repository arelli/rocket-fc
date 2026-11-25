"""Vector isolation G-code for a laser: instead of rastering the whole board, the laser traces a few outlines around
every piece of copper, so only a band around it is cleared (paint ablated, then etched) and the rest of the copper
stays as unconnected islands. Much faster than a raster image, and the etch removes far less copper.

    <KiCad python> tools/pcb_isolation.py board.kicad_pcb out_dir [name] [faces]
    <KiCad python> tools/pcb_isolation.py --fill board.kicad_pcb out.kicad_pcb    # for the raster images

Writes <name>-top-isolation.nc and <name>-bottom-isolation.nc (bottom mirrored, as seen from below); faces
"bottom" for a single-sided board (default "top,bottom"). Coordinates are
mm from the lower-left corner of the board outline's bounding box: the same frame as the laser SVGs and the jig, so
all of them go at the same offset. Per face:
  - outlines around all copper at spot/2, +step, ... until the cleared band is BAND wide (offsets of the merged
    copper never enter copper, and where two outlines meet in a narrow gap the gap is already clear);
  - nothing else: areas without copper further than BAND from any (the keep-outs under the screw heads and the
    flight board's Q1 tab, the gaps the GND pour leaves round the labels) stay painted, as isolated copper
    islands, and so does everything outside the outline (the USB notch);
  - a small dot at every hole centre (an etched pit centres the drill), like the raster images;
  - the board outline, as a cutting line.
Then a check: the copper the passes leave must contain the design's copper, and no two nets may end up joined.

--fill writes a copy of the board with every such area added as copper (no net) on both faces: its negative SVG,
the raster image, then burns only the same BAND round the copper.

Settings (environment): LASER_SPOT (mm, default 0.1), LASER_BAND (mm, 0.4), LASER_FEED (mm/min, 1000),
LASER_S (power, 980 of GRBL's default $30=1000). Power M4 (dynamic: needs GRBL laser mode $32=1; it lowers the power
in corners where the head slows down); travel moves at S0.
"""
import math
import os
import sys

import pcbnew

MM = 1e6
SPOT = float(os.environ.get('LASER_SPOT', '0.1'))
BAND = float(os.environ.get('LASER_BAND', '0.4'))
FEED = float(os.environ.get('LASER_FEED', '1000'))
POWER = int(os.environ.get('LASER_S', '980'))
STEP = 0.8 * SPOT                       # 20 % overlap between neighbouring outlines
ERR = int(0.005 * MM)
ROUND = pcbnew.CORNER_STRATEGY_ROUND_ALL_CORNERS


def chains(ps):
    """Every closed contour (outlines and holes) of a polygon set, as lists of (x, y) in nm."""
    out = []
    for i in range(ps.OutlineCount()):
        for c in [ps.Outline(i)] + [ps.Hole(i, j) for j in range(ps.HoleCount(i))]:
            pts = [(c.CPoint(k).x, c.CPoint(k).y) for k in range(c.PointCount())]
            if len(pts) > 1:
                out.append(pts + [pts[0]])
    return out


def copper(board, layer):
    ps = pcbnew.SHAPE_POLY_SET()
    board.ConvertBrdLayerToPolygonalContours(layer, ps)
    ps.Simplify()
    return ps


def inflated(ps, amount):
    c = ps.CloneDropTriangulation()
    if amount > 0:
        c.Inflate(int(amount), ROUND, ERR, True)
    elif amount < 0:
        c.Deflate(int(-amount), ROUND, ERR)
    c.Simplify()
    return c


def keepouts(board, layer):
    ps = pcbnew.SHAPE_POLY_SET()
    for z in board.Zones():
        if z.GetIsRuleArea() and z.GetDoNotAllowZoneFills() and z.IsOnLayer(layer):
            ps.Append(z.Outline())
    ps.Simplify()
    return ps


def board_outline(board):
    ps = pcbnew.SHAPE_POLY_SET()
    board.GetBoardPolygonOutlines(ps, False)
    return ps


def paths_for(board, layer):
    cu = copper(board, layer)
    paths = []
    n = max(1, int(math.ceil((BAND - SPOT) / STEP)) + 1)
    for i in range(n):                                   # isolation outlines, innermost first
        paths += chains(inflated(cu, (SPOT / 2 + i * STEP) * MM))
    paths += chains(board_outline(board))               # cutting line
    dots = []                                            # drill-centre dots (burned into the pads on purpose, and
    for fp in board.GetFootprints():                     # into the copper islands left at the M3 holes)
        for p in fp.Pads():
            if (p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH and p.IsOnLayer(layer)) or                     p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                dots.append(circle(p.GetPosition(), 0.1))
    for t in board.GetTracks():
        if t.GetClass() == 'PCB_VIA':
            dots.append(circle(t.GetPosition(), 0.1))
    return cu, paths, dots


def circle(c, r, n=12):
    pts = [(c.x + r * MM * math.cos(2 * math.pi * k / n), c.y + r * MM * math.sin(2 * math.pi * k / n))
           for k in range(n)]
    return pts + [pts[0]]


def order(paths):
    """Nearest-neighbour ordering to cut travel time."""
    left, out, pos = list(paths), [], (0, 0)
    while left:
        i = min(range(len(left)), key=lambda k: (left[k][0][0] - pos[0]) ** 2 + (left[k][0][1] - pos[1]) ** 2)
        p = left.pop(i)
        out.append(p)
        pos = p[-1]
    return out


def burned(paths):
    """The area the spot clears along the paths."""
    ps = pcbnew.SHAPE_POLY_SET()
    for p in paths:
        for (ax, ay), (bx, by) in zip(p, p[1:]):
            pcbnew.SHAPE_SEGMENT(pcbnew.VECTOR2I(int(ax), int(ay)), pcbnew.VECTOR2I(int(bx), int(by)),
                                 int(SPOT * MM)).TransformToPolygon(ps, ERR, pcbnew.ERROR_INSIDE)
    ps.Simplify()
    return ps


def verify(board, layer, cu, paths):
    """Copper left = blank minus the burned area. It must cover the design copper, and each piece of it may touch
    only one net."""
    bb = board.GetBoardEdgesBoundingBox()
    bb.Inflate(int(2 * MM))
    blank = pcbnew.SHAPE_POLY_SET()
    blank.NewOutline()
    for x, y in ((bb.GetLeft(), bb.GetTop()), (bb.GetRight(), bb.GetTop()), (bb.GetRight(), bb.GetBottom()),
                 (bb.GetLeft(), bb.GetBottom())):
        blank.Append(x, y)
    left = blank.CloneDropTriangulation()
    left.BooleanSubtract(burned(paths))
    left.Simplify()
    lost = cu.CloneDropTriangulation()
    lost.BooleanSubtract(left)
    lost_mm2 = lost.Area() / MM / MM
    pieces = {}
    probes = []
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.IsOnLayer(layer) and p.GetAttribute() != pcbnew.PAD_ATTRIB_NPTH:
                probes.append((p.GetNetname(), p.GetPosition() + pcbnew.VECTOR2I(int(p.GetSize(layer).x * 0.4), 0)))
    for t in board.GetTracks():
        if t.IsOnLayer(layer):
            probes.append((t.GetNetname(), (t.GetStart() + t.GetEnd()) / 2 if t.GetClass() == 'PCB_TRACK'
                           else t.GetPosition() + pcbnew.VECTOR2I(int(t.GetWidth(layer) * 0.4), 0)))
    for net, pt in probes:
        pt = pcbnew.VECTOR2I(int(pt.x), int(pt.y))
        for i in range(left.OutlineCount()):
            if left.Contains(pt, i):
                pieces.setdefault(i, set()).add(net)
                break
    shorts = [nets for nets in pieces.values() if len(nets - {''}) > 1]
    return lost_mm2, shorts


def gcode(paths, mirror, bb, path, name):
    x0, x1, y1 = bb.GetLeft() / MM, bb.GetRight() / MM, bb.GetBottom() / MM

    def xy(p):
        x, y = p[0] / MM, p[1] / MM
        return (x1 - x if mirror else x - x0), y1 - y
    length = 0.0
    lines = ['; %s, %s isolation (mm from the lower-left of the board outline box)'
             % (name, 'bottom, mirrored' if mirror else 'top'),
             '; spot %.3f mm, band %.2f mm, feed %g mm/min, power S%d' % (SPOT, BAND, FEED, POWER),
             'G21', 'G90', 'M4 S0']
    for p in paths:
        pts = [xy(q) for q in p]
        lines.append('G0 X%.3f Y%.3f S0' % pts[0])
        lines.append('G1 F%g S%d' % (FEED, POWER))
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            lines.append('X%.3f Y%.3f' % (bx, by))
            length += math.hypot(bx - ax, by - ay)
    lines += ['S0', 'M5', 'G0 X0 Y0']
    with open(path, 'w', newline='\n') as fh:
        fh.write('\n'.join(lines) + '\n')
    return length


def fill(pcb, out):
    """A copy of the board with copper (no net) on both faces wherever the design has none further than BAND from
    its copper, the area outside the outline included: in a negative plot only the band round the copper is black."""
    board = pcbnew.LoadBoard(pcb)
    bb = board.GetBoardEdgesBoundingBox()
    bb.Inflate(int(2 * MM))
    shapes = []
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        area = pcbnew.SHAPE_POLY_SET()
        area.NewOutline()
        for x, y in ((bb.GetLeft(), bb.GetTop()), (bb.GetRight(), bb.GetTop()), (bb.GetRight(), bb.GetBottom()),
                     (bb.GetLeft(), bb.GetBottom())):
            area.Append(x, y)
        area.BooleanSubtract(inflated(copper(board, layer), BAND * MM))
        area.Fracture()                                  # holes joined to their outlines: plain filled polygons
        for i in range(area.OutlineCount()):
            one = pcbnew.SHAPE_POLY_SET()
            one.AddOutline(area.Outline(i))
            s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_POLY)
            s.SetPolyShape(one)
            s.SetFilled(True)
            s.SetWidth(0)
            s.SetLayer(layer)
            shapes.append(s)
    for s in shapes:
        board.Add(s)
    board.Save(out)
    return 0


def main(pcb, out_dir, name='rocket-fc-flight', faces='top,bottom'):
    board = pcbnew.LoadBoard(pcb)
    bb = board_outline(board).BBox()                     # the outline itself (not its line width): the SVGs' page
    ok = True
    for face, layer, mirror in (('top', pcbnew.F_Cu, False), ('bottom', pcbnew.B_Cu, True)):
        if face not in faces.split(','):
            continue
        cu, paths, dots = paths_for(board, layer)
        lost, shorts = verify(board, layer, cu, paths)
        paths = order(paths + dots)
        length = gcode(paths, mirror, bb, os.path.join(out_dir, '%s-%s-isolation.nc' % (name, face)), name)
        print('isolation %s: %d outlines, %.1f m of cutting, about %.0f min at %g mm/min; copper lost %.2f mm2, '
              'nets joined: %s' % (face, len(paths), length / 1000, length / FEED * 1.15, FEED, lost,
                                   shorts or 'none'))
        ok &= lost < 0.05 and not shorts
    return 0 if ok else 1


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    if sys.argv[1] == '--fill':
        sys.exit(fill(*sys.argv[2:4]))
    sys.exit(main(*sys.argv[1:]))
