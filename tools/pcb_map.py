"""Placement map of a home-made PCB (flight or ground station): the laser copper images (top as lasered, bottom
mirrored as lasered) with every part's outline and reference, the wire vias (red X) and the pins to solder on both
faces (red ring, on the face of the part only). For building the board: which part goes where, and what to solder first.

Two halves, because pcbnew and PyMuPDF live in different Pythons:
    <KiCad python> tools/pcb_map.py extract board.kicad_pcb parts.json     # needs pcbnew (and tools/pcb_check.py)
    python tools/pcb_map.py draw parts.json out/laser map.png [name [top.svg]]   # needs PyMuPDF and the laser SVGs
tools/pcb_export.py runs both in its laser step. name is the KiCad project name (default rocket-fc-flight). A
single-sided board has no top copper image: top.svg (its bottom copper, not mirrored) is drawn under the top
face's parts instead, as if seen through the board.
"""
import json
import os
import sys

NAMES = {'rocket-fc-flight': {
    'U1': 'XIAO ESP32-S3 + Wio-SX1262 (USB-C at the notch)',
    'U2': 'SparkFun MPU-9250 (labels face the board)',
    'U3': 'GY-BMP280, SENSOR DOWN, VIN in square pad',
    'BZ1': 'Buzzer 12 mm',
    'C1': '470 uF 10 V, standing (+ square pad)',
    'C2': '100 nF, bent flat', 'C3': '100 nF, bent flat',
    'Q1': 'IRLB8748 lying flat, tab down',
    'R1': '100k battery divider', 'R2': '100k battery divider',
    'R3': '1 ohm 2 W pyro resistor', 'R4': '100k continuity divider', 'R5': '100k continuity divider',
    'R6': '100 ohm gate resistor', 'R7': '100k gate pull-down',
    'J1': 'Wire pads: 1 cell -, 2 cell +, 3-4 main switch',
    'J3': 'Wire pads: ARM link (CELL+ left, ARM right)', 'J4': 'Screw terminal: igniter',
    'J5': 'GPS lead: 3V3, GND, TX',
}, 'rocket-fc-ground': {
    'U1': 'XIAO + Wio-SX1262 on 2 x 7-pin female headers',
    'BZ1': 'Buzzer 12 mm (+ square pad)',
    'C1': '1000 uF 16 V, standing (+ square pad)',
    'Q1': 'IRLB8748 standing, tab to the top edge',
    'R1': '100k battery divider', 'R2': '100k battery divider',
    'R3': '1 ohm 5 W launch resistor', 'R4': '100k launch divider', 'R5': '100k launch divider',
    'R6': '100 ohm gate resistor', 'R7': '100k gate pull-down', 'R8': '100k ARM bypass',
    'R9': '330 ohm ON LED', 'R10': '330 ohm LINK LED',
    'LED1': 'green ON LED (flat side = square pad)', 'LED2': 'blue LINK LED (flat side = square pad)',
    'J1': 'Screw terminal: igniter leads', 'J2': 'Screw terminal: 18650 (- left, + right)',
    'J3': 'Screw terminal: ARM switch (5 A or more)',
    'J4': 'JST-XH 2: power switch', 'J5': 'JST-XH 2: XIAO BAT pads (- left)',
    'J6': 'JST-XH 2: push button', 'J7': 'JST-XH 2: MODE switch',
}}
# board edge named in the panel titles, and the mounting holes
EDGE = {'rocket-fc-flight': 'USB end', 'rocket-fc-ground': 'USB edge'}
HOLES = {'rocket-fc-flight': 'H1-H4: M3 holes to the sled', 'rocket-fc-ground': 'H1-H4: M3 holes to the box'}
COL = {'U': (0.25, 0.75, 1.0), 'R': (1.0, 0.85, 0.2), 'C': (0.55, 1.0, 0.45), 'J': (1.0, 0.45, 0.85),
       'Q': (1.0, 0.35, 0.3), 'B': (0.75, 0.6, 1.0), 'H': (0.8, 0.8, 0.8)}
RED, WHITE = (0.95, 0.08, 0.08), (1, 1, 1)
COPPER, SUBSTRATE = (196, 120, 52), (22, 40, 32)
K = 10.0                                   # points per mm on the page
LEG_W, GAP, TOPM = 74.0, 8.0, 12.0         # legend width, gaps, top margin (mm)


def extract(pcb, out):
    """KiCad Python: board bbox, parts (Fab outlines, pads, body box), vias, pins soldered on both faces."""
    import pcbnew
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import pcb_check

    b = pcbnew.LoadBoard(pcb)
    mm = lambda v: v / 1e6
    bb = b.GetBoardEdgesBoundingBox()
    data = {'bbox': [mm(bb.GetX()), mm(bb.GetY()), mm(bb.GetRight()), mm(bb.GetBottom())], 'parts': []}
    for f in b.GetFootprints():
        fab = pcbnew.B_Fab if f.IsFlipped() else pcbnew.F_Fab
        shapes = []
        for g in f.GraphicalItems():
            if g.GetLayer() != fab or g.Type() != pcbnew.PCB_SHAPE_T:
                continue
            st = g.GetShape()
            if st == pcbnew.SHAPE_T_CIRCLE:
                c = g.GetCenter()
                shapes.append(['circle', mm(c.x), mm(c.y), mm(g.GetRadius())])
                continue
            if st == pcbnew.SHAPE_T_RECT:
                s, e = g.GetStart(), g.GetEnd()
                pts = [(s.x, s.y), (e.x, s.y), (e.x, e.y), (s.x, e.y), (s.x, s.y)]
            elif st == pcbnew.SHAPE_T_POLY:
                ps = g.GetPolyShape().Outline(0)
                pts = [(ps.CPoint(i).x, ps.CPoint(i).y) for i in range(ps.PointCount())]
                pts.append(pts[0])
            else:
                pts = [(g.GetStart().x, g.GetStart().y), (g.GetEnd().x, g.GetEnd().y)]
            shapes.append(['line', [(mm(x), mm(y)) for x, y in pts]])
        cb = f.GetBoundingBox(False)
        data['parts'].append({
            'ref': f.GetReference(), 'value': f.GetValue(), 'fp': f.GetFPIDAsString(),
            'side': 'B' if f.IsFlipped() else 'F', 'shapes': shapes,
            'pads': [[p.GetNumber(), mm(p.GetPosition().x), mm(p.GetPosition().y), p.GetNetname()] for p in f.Pads()],
            'bbox': [mm(cb.GetX()), mm(cb.GetY()), mm(cb.GetRight()), mm(cb.GetBottom())],
            'pos': [mm(f.GetPosition().x), mm(f.GetPosition().y)]})
    data['vias'] = [[mm(t.GetPosition().x), mm(t.GetPosition().y), t.GetNetname()]
                    for t in b.GetTracks() if t.GetClass() == 'PCB_VIA']
    both = pcb_check.check_copper(pcb_check.Model(b))           # ['R1 pad 1', ...]
    data['both'] = [s.split(' pad ') for s in both]
    with open(out, 'w') as fh:
        json.dump(data, fh, indent=1)


def copper_image(pymupdf, svg):
    """The negative laser SVG as a coloured pixmap (copper on dark substrate) and its size in mm."""
    dpi = 400
    pix = pymupdf.open(svg)[0].get_pixmap(dpi=dpi, colorspace=pymupdf.csGRAY)
    gray = bytes(pix.samples)
    rgb = bytearray(3 * len(gray))
    for ch in range(3):
        lut = bytes(round(SUBSTRATE[ch] + (COPPER[ch] - SUBSTRATE[ch]) * v / 255) for v in range(256))
        rgb[ch::3] = gray.translate(lut)
    out = pymupdf.Pixmap(pymupdf.csRGB, pix.width, pix.height, bytes(rgb), False)
    return out, pix.width / dpi * 25.4, pix.height / dpi * 25.4


def draw(parts_json, laser_dir, out_png, name='rocket-fc-flight', top_svg=None):
    import pymupdf

    d = json.load(open(parts_json))
    names = NAMES.get(name, {})
    X0, Y0, X1, Y1 = d['bbox']
    CY = (Y0 + Y1) / 2
    BW, BH = X1 - X0, Y1 - Y0
    both = {tuple(x) for x in d['both']}
    per_side = max(sum(1 for p in d['parts'] if p['side'] == s and not p['ref'].startswith('H')) for s in 'FB')
    legend_h = 3 + 4.2 * per_side + 11 + 3 * 4.6
    doc = pymupdf.open()
    page = doc.new_page(width=(2 * (BW + LEG_W) + GAP * 3) * K, height=(max(BH, legend_h) + TOPM + 6) * K)
    page.draw_rect(page.rect, color=None, fill=(0.97, 0.97, 0.95))

    def cross(c, a, widths):
        for col, w in zip((WHITE, RED), widths):
            page.draw_line(pymupdf.Point(c.x - a, c.y - a), pymupdf.Point(c.x + a, c.y + a), color=col, width=w * K)
            page.draw_line(pymupdf.Point(c.x - a, c.y + a), pymupdf.Point(c.x + a, c.y - a), color=col, width=w * K)

    top_title = ('TOP face: parts (bottom copper seen through the board)' if top_svg else
                 'TOP face (as lasered, parts on top)')
    bottom_title = ('BOTTOM face: the copper (mirrored as lasered, no parts)' if top_svg else
                    'BOTTOM face (mirrored as lasered, parts on bottom)')
    for panel, (side, svg, title) in enumerate((('F', 'top', top_title),
                                                ('B', 'bottom', bottom_title))):
        ox, oy = GAP + panel * (BW + LEG_W + GAP), TOPM

        def P(x, y, mirror=(side == 'B')):
            if mirror:
                x = X0 + X1 - x
            return pymupdf.Point((ox + x - X0) * K, (oy + y - Y0) * K)

        pix, wmm, hmm = copper_image(pymupdf, top_svg if top_svg and side == 'F' else
                                     os.path.join(laser_dir, '%s-%s.svg' % (name, svg)))
        page.insert_image(pymupdf.Rect((ox + BW / 2 - wmm / 2) * K, (oy + BH / 2 - hmm / 2) * K,
                                       (ox + BW / 2 + wmm / 2) * K, (oy + BH / 2 + hmm / 2) * K), pixmap=pix)
        page.insert_text(pymupdf.Point(ox * K, (TOPM - 4) * K), title, fontsize=3.2 * K, fontname='helv')
        page.insert_text(pymupdf.Point(ox * K, (TOPM - 0.8) * K), EDGE.get(name, ''), fontsize=2.4 * K, fontname='helv',
                         color=(0.3, 0.3, 0.3))
        mine = sorted((p for p in d['parts'] if p['side'] == side),
                      key=lambda p: (p['ref'][0], len(p['ref']), p['ref']))
        legend, tags = [], []
        for p in mine:                                      # part outlines
            ref = p['ref']
            col = COL['B'] if ref.startswith('BZ') else COL.get(ref[0], WHITE)
            sh = page.new_shape()
            for s in p['shapes']:
                if s[0] == 'circle':
                    sh.draw_circle(P(s[1], s[2]), s[3] * K)
                else:
                    sh.draw_polyline([P(x, y) for x, y in s[1]])
            if not p['shapes']:                             # mounting holes
                sh.draw_circle(P(*p['pos']), 3.0 * K)
            sh.finish(color=col, width=0.35 * K, fill=None)
            sh.commit()
            bx0, by0, bx1, by1 = p['bbox']
            cx, cy = (bx0 + bx1) / 2, (by0 + by1) / 2
            if ref.startswith('H'):
                cx, cy = p['pos'][0], p['pos'][1] + (4.6 if p['pos'][1] < CY else -4.4)
            else:
                legend.append((ref, names.get(ref, ''), col))
            tags.append((P(cx, cy), ref, col))
        n_both = 0                                          # pins soldered on both faces: ring, part's face only
        for p in mine:
            for num, x, y, *_ in p['pads']:
                if (p['ref'], num) in both:
                    n_both += 1
                    for col, w in ((WHITE, 0.75), (RED, 0.4)):
                        page.draw_circle(P(x, y), 1.1 * K, color=col, width=w * 0.7 * K)
        for x, y, net in d['vias']:                         # wire vias: red X on both faces
            cross(P(x, y), 0.75 * K, (0.55, 0.3))
        for c, ref, col in tags:                            # references last, on dark tags
            fs = 2.6 if len(ref) <= 2 else 2.3
            tw = pymupdf.get_text_length(ref, fontname='hebo', fontsize=fs * K)
            page.draw_rect(pymupdf.Rect(c.x - tw / 2 - 0.5 * K, c.y - fs * 0.62 * K, c.x + tw / 2 + 0.5 * K,
                                        c.y + fs * 0.5 * K), color=None, fill=(0.05, 0.05, 0.05), fill_opacity=0.8)
            page.insert_text(pymupdf.Point(c.x - tw / 2, c.y + fs * 0.33 * K), ref, fontsize=fs * K,
                             fontname='hebo', color=col)
        lx, ly = ox + BW + 2.5, oy + 3                      # legend
        for ref, text, col in legend:
            page.draw_rect(pymupdf.Rect(lx * K, (ly - 2.2) * K, (lx + 2.2) * K, ly * K), color=None, fill=col)
            page.insert_text(pymupdf.Point((lx + 3.2) * K, ly * K), ref, fontsize=2.5 * K, fontname='hebo')
            page.insert_text(pymupdf.Point((lx + 9.5) * K, ly * K), text, fontsize=2.3 * K, fontname='helv')
            ly += 4.2
        page.insert_text(pymupdf.Point(lx * K, (ly + 2) * K), HOLES.get(name, ''), fontsize=2.3 * K,
                         fontname='helv', color=(0.3, 0.3, 0.3))
        ly += 9
        if d['vias']:
            rows = [('x', '%d wire vias (0.8 mm): do them ALL FIRST, before any part:' % len(d['vias'])),
                    ('', 'wire through, solder both faces, trim flush')]
        else:
            rows = [('', 'No wire vias: every track is on the bottom face' if top_svg else 'No wire vias')]
        rows.append(('ring', '%d pins to solder on both faces: on this side as well' % n_both) if n_both else
                    ('', 'Every pin is soldered on the bottom face only'))
        for kind, text in rows:
            c = pymupdf.Point((lx + 1.1) * K, (ly - 1.0) * K)
            if kind == 'ring':
                for col, w in ((WHITE, 0.8), (RED, 0.45)):
                    page.draw_circle(c, 1.2 * K, color=col, width=w * K)
            elif kind == 'x':
                cross(c, 1.0 * K, (0.8, 0.45))
            page.insert_text(pymupdf.Point((lx + 3.6) * K, ly * K), text, fontsize=2.3 * K, fontname='helv')
            ly += 4.6
    page.get_pixmap(dpi=130).save(out_png)


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'extract':
        extract(sys.argv[2], sys.argv[3])
    elif len(sys.argv) in (5, 6, 7) and sys.argv[1] == 'draw':
        draw(*sys.argv[2:])
    else:
        sys.exit(__doc__)
