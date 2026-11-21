"""Dimensions sheet of a PCB: both faces seen straight on with the outline and mounting hole dimensions, the
wire vias ringed by net, and two 3D views; optionally the parts list beside it.

    python tools/pcb_dims.py parts.json top.png bottom.png 3d-top.png 3d-bottom.png out.png [name [parts]]

parts.json comes from `tools/pcb_map.py extract`, the PNGs from `kicad-cli pcb render` with a transparent
background (top, bottom, then the two 3D views). 'parts' adds the parts list. tools/pcb_export.py runs it in its
board step. Needs Pillow.
"""

import json
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcb_parts import find_board, font, legend, load_render  # noqa: E402

INK = (20, 20, 20)
# via rings by net: (colour, legend text, net name test)
KINDS = [((0, 190, 255), 'GND', lambda n: n == 'GND'),
         ((245, 130, 0), 'battery / firing current', lambda n: re.search(r'CELL|BAT|IGN|ARM|PYRO', n)),
         ((235, 40, 40), 'supply', lambda n: n.lstrip('/').startswith('+')),
         ((230, 0, 230), 'signals', lambda n: True)]
TITLE = {'rocket-fc-flight': 'Flight PCB', 'rocket-fc-ground': 'Ground station PCB'}


def kind(net):
    return next(i for i, (_, _, test) in enumerate(KINDS) if test(net))


def outline(d):
    """Notches cut into an end of the board: (x0, x1, depth line y, end y) from the outline's segments."""
    X0, Y0, X1, Y1 = d['bbox']
    on = lambda a, b: abs(a - b) < 0.05
    out = []
    for x0, y0, x1, y1 in d.get('edges', []):
        if on(y0, y1) and not on(y0, Y0) and not on(y0, Y1):          # a horizontal edge inside the box
            xa, xb = sorted((x0, x1))
            if on(xa, X0) or on(xb, X1):
                continue
            end = Y0 if y0 - Y0 < Y1 - y0 else Y1
            out.append((xa, xb, y0, end))
    return out


def iso(png, h):
    """A 3D render: cropped to the board and its shadow, on white, scaled to height h."""
    from PIL import Image
    im = Image.open(png).convert('RGBA')
    box = im.getchannel('A').point(lambda a: 255 if a > 40 else 0).getbbox()
    im = im.crop((max(0, box[0] - 20), max(0, box[1] - 20), min(im.width, box[2] + 20), min(im.height, box[3] + 20)))
    out = Image.new('RGB', im.size, (255, 255, 255))
    out.paste(im, (0, 0), im)
    return out.resize((int(out.width * h / out.height), h))


def draw(parts_json, top_png, bottom_png, iso_top_png, iso_bottom_png, out_png, name='rocket-fc-flight',
         with_parts=False):
    from PIL import Image, ImageDraw

    d = json.load(open(parts_json))
    e = d.get('edges')
    if e:                                     # the outline's own lines (the bbox includes their width)
        d['bbox'] = [min(min(s[0], s[2]) for s in e), min(min(s[1], s[3]) for s in e),
                     max(max(s[0], s[2]) for s in e), max(max(s[1], s[3]) for s in e)]
    X0, Y0, X1, Y1 = d['bbox']
    BW, BH = X1 - X0, Y1 - Y0
    holes = sorted((p['pos'] for p in d['parts'] if p['ref'].startswith('H')), key=lambda q: (q[1], q[0]))
    notches = outline(d)
    f_dim, f_t, f_s = font(30, True), font(40, True), font(26)

    def view(png, bottom):
        """The render on white with room round the board, and its drawing helpers (board mm -> pixels)."""
        im = load_render(png)
        ox, oy, k = find_board(im, BW, BH)
        m = 600
        big = Image.new('RGB', (im.width + 2 * m, im.height + 2 * m), (255, 255, 255))
        big.paste(im, (m, m))
        ox, oy = ox + m, oy + m
        mx = 30 * k                                                   # room for the dimensions (30 mm)
        box = (int(ox - mx), int(oy - 0.55 * mx), int(ox + BW * k + mx), int(oy + BH * k + 0.75 * mx))
        im = big.crop(box)
        ox, oy = ox - box[0], oy - box[1]
        if bottom:                                                    # seen from below: left and right swapped
            P = lambda x, y: (ox + (X1 - x) * k, oy + (y - Y0) * k)
        else:
            P = lambda x, y: (ox + (x - X0) * k, oy + (y - Y0) * k)
        dr = ImageDraw.Draw(im)
        for x, y, net in d['vias']:
            cx, cy = P(x, y)
            r = 1.45 * k
            dr.ellipse((cx - r, cy - r, cx + r, cy + r), outline=KINDS[kind(net)][0], width=6)

        def arrow(a, b):
            ang = math.atan2(b[1] - a[1], b[0] - a[0])
            for p, s in ((a, 1), (b, -1)):
                dr.polygon([p] + [(p[0] + s * 18 * math.cos(ang + t), p[1] + s * 18 * math.sin(ang + t))
                                  for t in (0.35, -0.35)], fill=INK)
            dr.line((a, b), fill=INK, width=3)

        def hdim(xa, xb, y, at, text):
            """Horizontal dimension from board x xa to xb, extension lines from board y y, dimension line at y at."""
            a, b = P(xa, at), P(xb, at)
            up = at < y
            for x in (xa, xb):
                e0, e1 = P(x, y), P(x, at)
                dr.line((e0[0], e0[1] + (-8 if up else 8), e1[0], e1[1] + (-10 if up else 10)), fill=INK, width=2)
            arrow(a, b)
            w = dr.textlength(text, font=f_dim)
            dr.text(((a[0] + b[0] - w) / 2, a[1] - 40 if up else a[1] + 6), text, font=f_dim, fill=INK)

        def vdim(ya, yb, x, at, text):
            """Vertical dimension from board y ya to yb, extension lines from board x x, dimension line at x at."""
            a, b = P(at, ya), P(at, yb)
            for y in (ya, yb):
                e0, e1 = P(x, y), P(at, y)
                dr.line((e0[0], e0[1], e1[0] + (10 if e1[0] > e0[0] else -10), e1[1]), fill=INK, width=2)
            arrow(a, b)
            w = dr.textlength(text, font=f_dim)
            lbl = Image.new('RGBA', (int(w) + 8, 40), (0, 0, 0, 0))
            ImageDraw.Draw(lbl).text((4, 0), text, font=f_dim, fill=INK)
            lbl = lbl.rotate(90, expand=True)
            px = a[0] - 44 if a[0] < ox + BW * k / 2 else a[0] + 6
            im.paste(lbl, (int(px), int((a[1] + b[1] - lbl.height) / 2)), lbl)
        return im, hdim, vdim

    # top: the outline (width, length, notches and the ears beside them)
    top, hdim, vdim = view(top_png, False)
    hdim(X0, X1, Y1, Y1 + 7, '%.1f' % BW)
    vdim(Y0, Y1, X1, X1 + 16, '%.1f overall' % BH if notches else '%.1f' % BH)
    for xa, xb, y, end in notches:
        out = end - 4 if end == Y0 else end + 4
        hdim(xa, xb, y, out, 'notch %.1f' % (xb - xa))
        if xa - X0 > 0.05:
            hdim(X0, xa, end, out, '%.1f' % (xa - X0))
        if X1 - xb > 0.05:
            hdim(xb, X1, end, out, '%.1f' % (X1 - xb))
        vdim(min(y, end), max(y, end), X0, X0 - 7, '%.1f' % abs(end - y))
        far = Y1 if end == Y0 else Y0
        vdim(min(y, far), max(y, far), X1, X1 + 7, '%.1f body' % abs(far - y))
    # bottom (seen from below): the mounting holes, from the edges and from each other
    bot, hdim, vdim = view(bottom_png, True)
    if holes:
        xs = sorted({round(h[0], 2) for h in holes})
        ys = sorted({round(h[1], 2) for h in holes})
        if len(xs) > 1:
            hdim(xs[0], xs[-1], ys[-1], Y1 + 7, 'holes %.1f' % (xs[-1] - xs[0]))
        if len(ys) > 1:
            vdim(ys[0], ys[-1], xs[0], X0 - 9, 'holes %.1f' % (ys[-1] - ys[0]))
        hdim(xs[-1], X1, ys[0], Y0 - 4, '%g' % round(X1 - xs[-1], 2))
        vdim(Y0, ys[0], xs[-1], X1 + 7, '%g' % round(ys[0] - Y0, 2))
        if len(ys) > 1:
            vdim(ys[-1], Y1, xs[-1], X1 + 7, '%g' % round(Y1 - ys[-1], 2))

    isos = [iso(iso_top_png, 900), iso(iso_bottom_png, 900)]
    vw = top.width + bot.width + 60
    ih = int(900 * vw / (sum(i.width for i in isos) + 40))
    isos = [i.resize((int(i.width * ih / i.height), ih)) for i in isos]
    leg = legend([p for p in d['parts'] if not p['ref'].startswith('H')], name,
                 any(p['side'] == 'B' for p in d['parts']), bg=(255, 255, 255), tags=False) if with_parts else None
    if leg:                                   # the list drawn for a smaller sheet: scaled up to read alongside
        leg = leg.resize((int(leg.width * 1.6), int(leg.height * 1.6)), Image.LANCZOS)
    vh = max(top.height, bot.height)
    W = 40 + vw + 40 + (leg.width + 40 if leg else 0)
    H = max(140 + vh + 90 + ih + 200, (leg.height + 100) if leg else 0)
    sheet = Image.new('RGB', (W, H), (255, 255, 255))
    dr = ImageDraw.Draw(sheet)
    size = '%g x %g mm' % (round(BW, 1), round(BH, 1))
    for xa, xb, y, end in notches[:1]:
        size = '%g x %g mm (%g mm with the ears)' % (round(BW, 1), round(abs(Y1 + Y0 - end - y), 1), round(BH, 1))
    dr.text((40, 30), '%s  -  %s, %g mm FR4, dimensions in mm' % (TITLE.get(name, name), size,
                                                                     d.get('thickness', 1.6)), font=f_t, fill=INK)
    x = 40
    for im, t in ((top, 'Top (outline)'), (bot, 'Bottom, seen from below (mounting holes)')):
        dr.text((x, 100), t, font=f_dim, fill=INK)
        sheet.paste(im, (x, 140))
        x += im.width + 60
    y = 140 + vh + 40
    dr.text((40, y), '3D: top and bottom', font=f_dim, fill=INK)
    x = 40
    for im in isos:
        sheet.paste(im, (x, y + 50))
        x += im.width + 40
    y += 50 + ih + 40
    vias = d['vias']
    if vias:
        dr.text((40, y), '%d wire vias (0.8 mm hole: a wire through, soldered on both faces), ringed by net:'
                % len(vias), font=f_dim, fill=INK)
        x = 40
        for i, (col, text, _) in enumerate(KINDS):
            n = sum(kind(net) == i for _, _, net in vias)
            if not n:
                continue
            nets = sorted({net.lstrip('/') for _, _, net in vias if kind(net) == i})
            t = '%s x%d (%s)' % (text, n, ', '.join(nets)) if len(nets) > 1 or nets[0] != text else '%s x%d' % (text, n)
            dr.ellipse((x, y + 60, x + 36, y + 96), outline=col, width=6)
            dr.text((x + 50, y + 60), t, font=f_s, fill=INK)
            x += 100 + dr.textlength(t, font=f_s)
    else:
        dr.text((40, y), 'No wire vias.', font=f_dim, fill=INK)
    if leg:
        sheet.paste(leg, (40 + vw + 40, 60))
    sheet.save(out_png)


if __name__ == '__main__':
    if len(sys.argv) in (7, 8, 9):
        draw(*sys.argv[1:8], with_parts=len(sys.argv) == 9 and sys.argv[8] == 'parts')
    else:
        sys.exit(__doc__)
