"""Parts sheet of a PCB: the 3D renders with every part tagged, resistor colour bands painted on, and a legend
(reference, value, what it is, resistor colour codes).

    python tools/pcb_parts.py parts.json top.png bottom.png out.png [name]    # needs Pillow

parts.json comes from `tools/pcb_map.py extract`, the PNGs from `kicad-cli pcb render --side top/bottom`;
tools/pcb_export.py runs it in its board step. name is the KiCad project name (default rocket-fc-flight).
"""

import json
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcb_map import NAMES  # noqa: E402  (part descriptions per board)

DIGITS = ['black', 'brown', 'red', 'orange', 'yellow', 'green', 'blue', 'violet', 'grey', 'white']
MULT = {-2: 'silver', -1: 'gold', **{i: c for i, c in enumerate(DIGITS)}}
RGB = {'black': (20, 20, 20), 'brown': (120, 62, 20), 'red': (215, 30, 30), 'orange': (245, 130, 20),
       'yellow': (250, 220, 30), 'green': (30, 160, 60), 'blue': (30, 80, 220), 'violet': (140, 60, 200),
       'grey': (140, 140, 140), 'white': (245, 245, 245), 'gold': (205, 165, 50), 'silver': (190, 190, 200)}
ORDER = ['U', 'Q', 'R', 'C', 'LED', 'BZ', 'J']           # legend order by reference prefix
# what goes into each screw terminal or wire pad: (tag on the render, sign per pin or None, legend line);
# tag None: a tag over each pad, its text from the pin's net
WIRES = {'rocket-fc-flight': {
    'J1': ('CELL / SWITCH', None, 'Cell and main switch, soldered wires: pad 1 cell −, 2 cell +, 3–4 main switch'),
    'J3': (None, {'/CELL+': 'ARM LINK +', '/ARM': 'ARM LINK'}, 'ARM link (key switch or jumper): one wire soldered to each corner pad'),
    'J4': ('IGNITER', None, 'Igniter: one lead in each screw, either way round'),
}, 'rocket-fc-ground': {
    'J1': ('IGNITER', None, 'Igniter cable: one lead in each screw, either way round'),
    'J2': ('18650', {'GND': '−', '/CELL+': '+'}, '18650 cell: − into the GND screw, + into the other (as marked)'),
    'J3': ('ARM SWITCH', None, 'ARM switch (5 A or more): one wire in each screw'),
}}


def ohms(value):
    """'100k' -> 100000, '330R' -> 330, '1R 5W' -> 1, '100' -> 100; None if not a resistance."""
    m = re.match(r'^\s*(\d+(?:\.\d+)?)\s*([kKmMR]?)(\d*)', value)
    if not m:
        return None
    v = float(m.group(1) + ('.' + m.group(3) if m.group(3) else ''))
    return v * {'k': 1e3, 'K': 1e3, 'm': 1e6, 'M': 1e6}.get(m.group(2), 1)


def watts(value):
    m = re.search(r'(\d+(?:\.\d+)?)\s*W\b', value)
    return float(m.group(1)) if m else 0


def bands(r, n):
    """Colour bands for r ohms: n = 4 (two digits, 5 %) or 5 (three digits, 1 %)."""
    digits = n - 2
    e = math.floor(math.log10(r)) - digits + 1
    sig = round(r / 10 ** e)
    if sig >= 10 ** digits:
        sig //= 10
        e += 1
    return [DIGITS[int(c)] for c in str(sig).zfill(digits)] + [MULT[e], 'gold' if n == 4 else 'brown']


def nice(ref, value):
    """Value with units for the legend."""
    if ref.startswith('R') and ohms(value) is not None:
        r = ohms(value)
        s = ('%g MΩ' % (r / 1e6)) if r >= 1e6 else ('%g kΩ' % (r / 1e3)) if r >= 1e3 else '%g Ω' % r
        return s + (' %g W' % watts(value) if watts(value) else '')
    if ref.startswith('C'):
        m = re.match(r'^(\d+(?:\.\d+)?)\s*([pnuµ])F?\s*(.*)$', value)
        if m:
            return '%s %sF%s' % (m.group(1), 'µ' if m.group(2) in 'uµ' else m.group(2),
                                 (' ' + m.group(3).replace('V', ' V')) if m.group(3) else '')
    return value.replace('_', ' ')


def describe(ref, names):
    """Part description without the value it starts with ('100k battery divider' -> 'battery divider')."""
    t = names.get(ref, '')
    if ref.startswith('R'):
        t = re.sub(r'^[\d.]+\s*(k|ohm|R)?\s*([\d.]+\s*W\s*)?', '', t)
    return t


def font(size, bold=False):
    from PIL import ImageFont
    for f in (('arialbd.ttf' if bold else 'arial.ttf'),
              ('DejaVuSans-Bold.ttf' if bold else 'DejaVuSans.ttf')):
        for d in ('C:/Windows/Fonts', '/usr/share/fonts/truetype/dejavu', '/Library/Fonts', ''):
            try:
                return ImageFont.truetype(os.path.join(d, f) if d else f, size)
            except OSError:
                pass
    return ImageFont.load_default(size)


def load_render(png):
    """A kicad-cli render with a transparent background on white; the shadow (part transparent) is dropped."""
    from PIL import Image
    im = Image.open(png)
    if im.mode != 'RGBA':
        return im.convert('RGB')
    out = Image.new('RGB', im.size, (255, 255, 255))
    out.paste(im, (0, 0), im.getchannel('A').point(lambda a: 255 if a > 250 else 0))
    return out


def find_board(im, bw, bh):
    """Pixel box of the board in a render: the rows and columns that are mostly solder-mask green."""
    w, h = im.size
    px = im.load()
    green = lambda p: p[1] > p[0] + 12 and p[1] > p[2] + 4
    step = 2
    cols = [sum(green(px[x, y]) for y in range(0, h, step)) for x in range(w)]
    rows = [sum(green(px[x, y]) for x in range(0, w, step)) for y in range(h)]
    xs = [x for x, c in enumerate(cols) if c > 0.3 * max(cols)]
    ys = [y for y, c in enumerate(rows) if c > 0.3 * max(rows)]
    x0, x1, y0, y1 = xs[0], xs[-1], ys[0], ys[-1]
    k = ((x1 - x0) / bw + (y1 - y0) / bh) / 2               # pixels per mm
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    return cx - bw * k / 2, cy - bh * k / 2, k


def draw(parts_json, top_png, bottom_png, out_png, name='rocket-fc-flight'):
    from PIL import Image, ImageDraw

    d = json.load(open(parts_json))
    X0, Y0, X1, Y1 = d['bbox']
    BW, BH = X1 - X0, Y1 - Y0
    parts = [p for p in d['parts'] if not p['ref'].startswith('H')]
    sides = ['F'] + (['B'] if any(p['side'] == 'B' for p in parts) else [])
    f_tag = font(30, True)

    panels = []
    for side in sides:
        im = load_render(top_png if side == 'F' else bottom_png)
        ox, oy, k = find_board(im, BW, BH)
        pad = 140                                        # room round the render for tags outside the board
        big = Image.new('RGB', (im.width + 2 * pad, im.height + 2 * pad), im.getpixel((2, 2)))
        big.paste(im, (pad, pad))
        im, ox, oy = big, ox + pad, oy + pad
        # board mm -> render pixels; the bottom render is seen from below (left and right swapped)
        if side == 'F':
            P = lambda x, y, ox=ox, oy=oy, k=k: (ox + (x - X0) * k, oy + (y - Y0) * k)
        else:
            P = lambda x, y, ox=ox, oy=oy, k=k: (ox + (X1 - x) * k, oy + (y - Y0) * k)
        dr = ImageDraw.Draw(im)
        mine = [p for p in parts if p['side'] == side]
        # colour bands on the resistor bodies (4-band code; a 5 W or bigger part has its value printed)
        for p in mine:
            r = ohms(p['value']) if p['ref'].startswith('R') else None
            m = re.search(r'_L([\d.]+)mm_D([\d.]+)mm', p['fp'])
            if r is None or not m or watts(p['value']) >= 5 or len(p['pads']) != 2:
                continue
            L, D = float(m.group(1)), float(m.group(2))
            (_, ax, ay, *_), (_, bx, by, *_) = p['pads']
            ux, uy = (bx - ax), (by - ay)
            n = math.hypot(ux, uy)
            ux, uy = ux / n, uy / n
            mx, my = (ax + bx) / 2, (ay + by) / 2
            for off, col in zip((-0.32, -0.18, -0.04, 0.3), bands(r, 4)):
                cx, cy = mx + ux * off * L, my + uy * off * L
                half_w, half_l = 0.045 * L, 0.5 * D
                pts = [P(cx + ux * a + -uy * b, cy + uy * a + ux * b)
                       for a, b in ((-half_w, -half_l), (half_w, -half_l), (half_w, half_l), (-half_w, half_l))]
                dr.polygon(pts, fill=RGB[col])
        # tags: reference (and value for resistors), placed beside the part where they hit the least
        boxes = []
        for p in mine:
            x0, y0, x1, y1 = p['bbox']
            a, b = P(x0, y0), P(x1, y1)
            boxes.append((min(a[0], b[0]), min(a[1], b[1]), max(a[0], b[0]), max(a[1], b[1])))
        placed = []
        wires = WIRES.get(name, {})

        def hit(t):
            s = 0
            for o in placed + boxes:
                ix = min(t[2], o[2]) - max(t[0], o[0])
                iy = min(t[3], o[3]) - max(t[1], o[1])
                if ix > 0 and iy > 0:
                    s += ix * iy * (4 if o in placed else 1)
            if t[0] < 0 or t[1] < 0 or t[2] > im.width or t[3] > im.height:
                s += 1e9
            return s
        # screw terminals first: what to connect, on the outer side of the block (where the wires go in)
        for p, (bx0, by0, bx1, by1) in zip(mine, boxes):
            if p['ref'] not in wires:
                continue
            label, signs, _ = wires[p['ref']]
            pins = sorted((P(x, y) + (net,) for _, x, y, net in p['pads']), key=lambda q: q[0])
            if label is None:                            # separate pads: one tag over each, from its net
                for px, py, net in pins:
                    t = signs[net]
                    w, h = dr.textlength(t, font=f_tag) + 20, 42
                    x, y = px - w / 2, py - 34 - h
                    placed.append((x, y, x + w, y + h))
                    dr.rounded_rectangle((x, y, x + w, y + h), 8, fill=(255, 225, 90), outline=(0, 0, 0), width=3)
                    dr.text((x + 10, y + 4), t, font=f_tag, fill=(0, 0, 0))
                continue
            if signs:
                label = '%s  %s  %s' % (signs.get(pins[0][2], ''), label, signs.get(pins[-1][2], ''))
            w, h = dr.textlength(label, font=f_tag) + 20, 42
            cx, cy = (bx0 + bx1) / 2, (by0 + by1) / 2
            gx, gy = (cx - (ox + BW * k / 2)) / BW, (cy - (oy + BH * k / 2)) / BH
            if abs(gy) >= abs(gx):
                x, y = cx - w / 2, (by1 + 8 if gy > 0 else by0 - 8 - h)
                step = (0, (h + 6) * (1 if gy > 0 else -1))
            else:
                x, y = (bx1 + 8 if gx > 0 else bx0 - 8 - w), cy - h / 2
                step = ((w + 6) * (1 if gx > 0 else -1), 0)
            # side by side terminals: move a tag that would touch its neighbour's one row further out
            while any(min(x + w, t[2]) - max(x, t[0]) > -6 and min(y + h, t[3]) - max(y, t[1]) > -6 for t in placed):
                x, y = x + step[0], y + step[1]
            placed.append((x, y, x + w, y + h))
            dr.rounded_rectangle((x, y, x + w, y + h), 8, fill=(255, 225, 90), outline=(0, 0, 0), width=3)
            dr.text((x + 10, y + 4), label, font=f_tag, fill=(0, 0, 0))
        for p, (bx0, by0, bx1, by1) in sorted(zip(mine, boxes), key=lambda q: (q[1][2] - q[1][0]) * (q[1][3] - q[1][1])):
            text = p['ref'] + (' ' + nice(p['ref'], p['value']) if p['ref'].startswith('R') else '')
            w, h = dr.textlength(text, font=f_tag) + 16, 40
            cx, cy = (bx0 + bx1) / 2, (by0 + by1) / 2
            cands = [(bx1 + 6, cy - h / 2), (bx0 - 6 - w, cy - h / 2), (cx - w / 2, by0 - 6 - h),
                     (cx - w / 2, by1 + 6), (cx - w / 2, cy - h / 2)]
            best = min(((hit((x, y, x + w, y + h)), i, x, y) for i, (x, y) in enumerate(cands)))
            _, _, x, y = best
            placed.append((x, y, x + w, y + h))
            dr.rounded_rectangle((x, y, x + w, y + h), 8, fill=(255, 255, 255), outline=(0, 0, 0), width=2)
            dr.text((x + 8, y + 3), text, font=f_tag, fill=(0, 0, 0))
        # crop to the board and every tag, with a margin
        m = 30
        cx0 = min([ox] + [t[0] for t in placed]) - m
        cy0 = min([oy] + [t[1] for t in placed]) - m
        cx1 = max([ox + BW * k] + [t[2] for t in placed]) + m
        cy1 = max([oy + BH * k] + [t[3] for t in placed]) + m
        crop = im.crop((max(0, int(cx0)), max(0, int(cy0)), min(im.width, int(cx1)), min(im.height, int(cy1))))
        panels.append(('TOP face' if side == 'F' else 'BOTTOM face (seen from below)', crop))

    leg = legend(parts, name, len(sides) == 2)
    ph = max(c.height for _, c in panels) + 60
    W = sum(c.width for _, c in panels) + 30 * (len(panels) - 1) + leg.width
    H = max(ph, leg.height)
    out = Image.new('RGB', (W, H), (255, 255, 255))
    dr = ImageDraw.Draw(out)
    x = 0
    for title, c in panels:
        dr.text((x + 20, 12), title, font=f_tag, fill=(0, 0, 0))
        out.paste(c, (x, 60))
        x += c.width + 30
    out.paste(leg, (x, 0))
    out.save(out_png)


def legend(parts, name, two_sided, bg=(255, 255, 255), tags=True):
    """The parts list as an image: reference, value, what it is, resistor colour codes, what goes into each
    screw terminal or wire pad. tags: the renders beside it are tagged (yellow tags, colour bands)."""
    from PIL import Image, ImageDraw
    names = NAMES.get(name, {})
    f_tag, f_row, f_small, f_title = font(30, True), font(28), font(24), font(40, True)
    sides = ['F', 'B'] if two_sided else ['F']

    def key(p):
        pre = re.match(r'[A-Z]+', p['ref']).group(0)
        return (ORDER.index(pre) if pre in ORDER else len(ORDER), int(re.sub(r'\D', '', p['ref']) or 0))
    rows = sorted(parts, key=key)
    any_r = any(p['ref'].startswith('R') for p in rows)
    LW = 1500 if any_r else 1100
    RH = 54
    W, H = LW, 400 + RH * len(rows) + 60 * 8 + 40 * len(WIRES.get(name, {}))   # cropped to the content at the end
    out = Image.new('RGB', (W, H), bg)
    dr = ImageDraw.Draw(out)
    lx, y = 20, 30
    dr.text((lx, y), {'rocket-fc-flight': 'Flight computer', 'rocket-fc-ground': 'Ground station'}.get(name, name)
            + ': parts', font=f_title, fill=(0, 0, 0))
    y += 70
    cv, cd, cb4, cb5 = lx + 100, lx + 330, lx + 1010, lx + 1200
    dr.text((lx, y), 'Ref', font=f_tag, fill=(0, 0, 0))
    dr.text((cv, y), 'Value', font=f_tag, fill=(0, 0, 0))
    dr.text((cd, y), 'What', font=f_tag, fill=(0, 0, 0))
    if any_r:
        dr.text((cb4, y + 4), '4-band 5 %', font=f_small, fill=(60, 60, 60))
        dr.text((cb5, y + 4), '5-band 1 %', font=f_small, fill=(60, 60, 60))
    y += 50

    def swatches(x, y, cols):
        for i, c in enumerate(cols):
            dr.rectangle((x + i * 32, y + 4, x + i * 32 + 24, y + 40), fill=RGB[c], outline=(0, 0, 0))
    for p in rows:
        ref, val = p['ref'], p['value']
        dr.text((lx, y + 6), ref, font=f_tag, fill=(0, 0, 0))
        v = nice(ref, val)
        while dr.textlength(v, font=f_row) > cd - cv - 14:
            v = v[:-2] + '…'
        dr.text((cv, y + 8), v, font=f_row, fill=(0, 0, 0))
        t = describe(ref, names) + ('' if p['side'] == 'F' or len(sides) == 1 else '  [bottom]')
        while dr.textlength(t, font=f_small) > (cb4 if any_r else lx + LW) - cd - 20:
            t = t[:-2] + '…'
        dr.text((cd, y + 10), t, font=f_small, fill=(40, 40, 40))
        r = ohms(val) if ref.startswith('R') else None
        if r is not None:
            if watts(val) >= 5:
                dr.text((cb4, y + 10), 'value printed on the body', font=f_small, fill=(40, 40, 40))
            else:
                swatches(cb4, y, bands(r, 4))
                swatches(cb5, y, bands(r, 5))
        y += RH
    wires = WIRES.get(name, {})
    if wires:
        y += 20
        dr.text((lx, y), 'Connections (yellow tags):' if tags else 'Connections:', font=f_tag, fill=(0, 0, 0))
        y += 48
        for ref, (_, _, line) in sorted(wires.items()):
            dr.text((lx, y), '%s  %s' % (ref, line), font=f_small, fill=(0, 0, 0))
            y += 36
    if any_r:
        y += 20
        dr.text((lx, y), 'Colour codes (read from the band nearest an end):', font=f_tag, fill=(0, 0, 0))
        y += 48
        seen = []
        for p in rows:
            r = ohms(p['value']) if p['ref'].startswith('R') else None
            if r is None or watts(p['value']) >= 5 or r in seen:
                continue
            seen.append(r)
            dr.text((lx, y), '%s:  %s   |   %s' % (nice('R', '%g' % r), '-'.join(bands(r, 4)), '-'.join(bands(r, 5))),
                    font=f_small, fill=(0, 0, 0))
            y += 36
        dr.text((lx, y + 8), ('The render shows the 4-band code. ' if tags else '') + 'Resistors go either way round.',
                font=f_small, fill=(60, 60, 60))
    return out.crop((0, 0, W, y + 70))


if __name__ == '__main__':
    if len(sys.argv) in (5, 6):
        draw(*sys.argv[1:])
    else:
        sys.exit(__doc__)
