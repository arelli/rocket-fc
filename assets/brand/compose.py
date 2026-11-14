#!/usr/bin/env python3
"""Put the rocket-fc logo on top of any image (centred, frosted-glass card).

Requires Pillow:  pip install pillow

Examples (run from the repository root):
  python assets/brand/compose.py my_render.png
      -> my_render_logo.png, same size as the input
  python assets/brand/compose.py my_render.png --size 1280x640 --fit contain -o social.png
      -> GitHub social preview (Settings > General > Social preview)
  python assets/brand/compose.py photo.jpg --logo-width 0.35 --offset 0,120

The card is drawn from rocket-fc-logo.png (a 3x render of rocket-fc-logo.svg),
so the result looks the same on any computer, without fonts installed.
"""
import argparse
import os

from PIL import Image, ImageDraw, ImageFilter, ImageEnhance

HERE = os.path.dirname(os.path.abspath(__file__))
LOGO_PNG = os.path.join(HERE, 'rocket-fc-logo.png')
SVG_W, SVG_RADIUS = 667, 28  # rocket-fc-logo.svg width and corner radius (px)


def cover(img, w, h, fx, fy):
    """Scale to cover w x h, crop around the focus point (fractions 0..1)."""
    s = max(w / img.width, h / img.height)
    img = img.resize((round(img.width * s), round(img.height * s)), Image.LANCZOS)
    x = min(max(round(img.width * fx - w / 2), 0), img.width - w)
    y = min(max(round(img.height * fy - h / 2), 0), img.height - h)
    return img.crop((x, y, x + w, y + h))


def contain(img, w, h):
    """Scale to fit inside w x h; fill the rest with the colour of the top-left pixel."""
    s = min(w / img.width, h / img.height)
    img = img.resize((round(img.width * s), round(img.height * s)), Image.LANCZOS)
    out = Image.new('RGB', (w, h), img.getpixel((0, 0)))
    out.paste(img, ((w - img.width) // 2, (h - img.height) // 2))
    return out


def rounded_mask(size, box, radius, fill=255):
    m = Image.new('L', size, 0)
    ImageDraw.Draw(m).rounded_rectangle(box, radius=radius, fill=fill)
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('background', help='background image (png, jpg, ...)')
    ap.add_argument('-o', '--output', help='output file (default: <background>_logo.png)')
    ap.add_argument('--size', help='output size WxH, e.g. 1280x640 (default: the background size)')
    ap.add_argument('--fit', choices=['cover', 'contain'], default='cover',
                    help='with --size: cover = fill and crop (default), contain = whole image, padded '
                         'with its corner colour (good for renders on a plain background)')
    ap.add_argument('--focus', default='0.5,0.5', help='cover crop focus x,y as fractions (default 0.5,0.5)')
    ap.add_argument('--logo-width', type=float, default=0.46, help='logo width as a fraction of the image width')
    ap.add_argument('--offset', default='0,0', help='move the logo by dx,dy pixels from the centre')
    ap.add_argument('--blur', type=float, default=14, help='frosted-glass blur under the card (0 = none)')
    ap.add_argument('--no-shadow', action='store_true', help='no drop shadow under the card')
    a = ap.parse_args()

    bg = Image.open(a.background).convert('RGB')
    if a.size:
        w, h = (int(v) for v in a.size.lower().split('x'))
        if a.fit == 'contain':
            bg = contain(bg, w, h)
        else:
            fx, fy = (float(v) for v in a.focus.split(','))
            bg = cover(bg, w, h, fx, fy)
    W, H = bg.size

    logo = Image.open(LOGO_PNG).convert('RGBA')
    lw = round(W * a.logo_width)
    lh = round(logo.height * lw / logo.width)
    logo = logo.resize((lw, lh), Image.LANCZOS)
    k = lw / SVG_W  # px per SVG unit
    dx, dy = (int(v) for v in a.offset.split(','))
    x, y = (W - lw) // 2 + dx, (H - lh) // 2 + dy
    box = (x, y, x + lw - 1, y + lh - 1)
    radius = round(SVG_RADIUS * k)

    out = bg.convert('RGBA')
    if not a.no_shadow:  # like CSS box-shadow: 0 24px 70px rgba(0,0,0,.55)
        sh = rounded_mask((W, H), (x, y + 24 * k, x + lw, y + lh + 24 * k), radius, fill=140)
        sh = sh.filter(ImageFilter.GaussianBlur(35 * k))
        out = Image.composite(Image.new('RGBA', (W, H), (0, 0, 0, 255)), out, sh)
    if a.blur > 0:  # frosted glass: blurred, slightly saturated background inside the card
        frost = ImageEnhance.Color(out.filter(ImageFilter.GaussianBlur(a.blur * k))).enhance(1.2)
        out = Image.composite(frost, out, rounded_mask((W, H), box, radius))
    out.alpha_composite(logo, (x, y))

    dst = a.output or os.path.splitext(a.background)[0] + '_logo.png'
    out.convert('RGB').save(dst)
    print(f'{dst}: {W}x{H}, logo {lw}x{lh} at {x},{y}')


if __name__ == '__main__':
    main()
