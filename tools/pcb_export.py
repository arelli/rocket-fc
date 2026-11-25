"""Checks and exports for the KiCad projects: the flight computer (hardware/flight-pcb, the default) and the ground
station (hardware/ground-pcb, with --board ground).

Usage:
    python tools/pcb_export.py            # check, docs, board
    python tools/pcb_export.py check      # ERC, DRC + schematic parity, tools/pcb_check.py; exit 1 on problems
    python tools/pcb_export.py docs       # docs/schematics/flight.svg, printable PDF in hardware/flight-pcb/out/
    python tools/pcb_export.py board      # 1:1 placement PDFs (top, bottom) and 3D renders in hardware/flight-pcb/out/
                                          # and the parts sheet (tagged renders, legend, resistor colour codes),
                                          # copied to hardware/flight-pcb/parts.png for the README, and the
                                          # dimensions sheet (outline, holes, wire vias, 3D views), without and
                                          # with the parts list: dims.png, dims-parts.jpg
    python tools/pcb_export.py laser      # laser copper images, isolation G-code, registration jig, drill file,
                                          # placement map in hardware/flight-pcb/out/laser/, copied with a zip of
                                          # them to hardware/flight-pcb/laser/ for the README
                                          # (PNG line spacing: LASER_LPMM lines/mm, default 12.5 = 0.08 mm)
    python tools/pcb_export.py fab        # Gerbers, drill files and a zip to order from a PCB maker in
                                          # hardware/flight-pcb/out/fab/, the zip copied to hardware/flight-pcb/fab/
    python tools/pcb_export.py --board ground [steps]   # the same for hardware/ground-pcb (its schematic SVG is
                                                        # docs/schematics/ground-pcb.svg)

Needs KiCad 10 (kicad-cli). Found on PATH, in KICAD_CLI, or in the default install folder. tools/pcb_check.py
runs with KiCad's own Python (next to kicad-cli, or KICAD_PYTHON). Until the board is routed, DRC's unrouted
connections are reported but not counted.
"""
import glob
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, 'docs', 'schematics')
# board: (folder in hardware/, KiCad project name, schematic SVG in docs/schematics/, its title, copper faces)
BOARDS = {
    'flight': ('flight-pcb', 'rocket-fc-flight', 'flight.svg', 'rocket-fc flight computer schematic',
               ('top', 'bottom')),
    'ground': ('ground-pcb', 'rocket-fc-ground', 'ground-pcb.svg', 'rocket-fc ground station PCB schematic',
               ('bottom',)),     # single-sided: copper on the bottom only, no registration jig
}
BOARD = PROJECT = NAME = SCH = PCB = OUT = None


def select(board):
    global BOARD, PROJECT, NAME, SCH, PCB, OUT
    BOARD = board
    PROJECT = os.path.join(ROOT, 'hardware', BOARDS[board][0])
    NAME = BOARDS[board][1]
    SCH = os.path.join(PROJECT, NAME + '.kicad_sch')
    PCB = os.path.join(PROJECT, NAME + '.kicad_pcb')
    OUT = os.path.join(PROJECT, 'out')  # generated files, not committed


def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, '/') + ('/' if os.path.isdir(path) else '')


def kicad_cli():
    env = os.environ.get('KICAD_CLI')
    if env:
        return env
    found = shutil.which('kicad-cli')
    if found:
        return found
    candidates = sorted(glob.glob(r'C:\Program Files\KiCad\*\bin\kicad-cli.exe'), reverse=True)
    candidates.append('/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli')
    for c in candidates:
        if os.path.exists(c):
            return c
    sys.exit('kicad-cli not found: install KiCad 10 or set KICAD_CLI')


def run(*args, quiet=False):
    print('>', 'kicad-cli', ' '.join(args[1:]))
    return subprocess.run(args, stdout=subprocess.DEVNULL if quiet else None).returncode


def kicad_python(cli):
    """KiCad's own Python (it has the pcbnew module), for tools/pcb_check.py."""
    env = os.environ.get('KICAD_PYTHON')
    if env:
        return env
    for name in ('python.exe', 'python3', 'python'):
        p = os.path.join(os.path.dirname(cli), name)
        if os.path.exists(p):
            return p
    mac = '/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3'
    return mac if os.path.exists(mac) else 'python3'


def routed():
    return '(segment' in open(PCB, encoding='utf8').read()


def check(cli):
    failed = False
    with tempfile.TemporaryDirectory() as tmp:
        report = os.path.join(tmp, 'erc.rpt')
        if run(cli, 'sch', 'erc', '--severity-all', '--exit-code-violations', '-o', report, SCH):
            print(open(report, encoding='utf8').read())
            failed = True
        if os.path.exists(PCB):
            report = os.path.join(tmp, 'drc.json')
            run(cli, 'pcb', 'drc', '--severity-all', '--schematic-parity', '--format', 'json', '-o', report, PCB)
            drc = json.load(open(report, encoding='utf8'))
            problems = drc['violations'] + drc['schematic_parity']
            if routed():
                problems += drc['unconnected_items']
            else:
                print('DRC: board not routed yet: %d unrouted connections ignored' % len(drc['unconnected_items']))
            for v in problems:
                where = '; '.join(i['description'] for i in v.get('items', []))
                print('DRC %s: %s  [%s]' % (v['severity'], v['description'], where))
            print('DRC: %d problem(s)' % len(problems))
            failed |= bool(problems)
            if subprocess.run([kicad_python(cli), os.path.join(ROOT, 'tools', 'pcb_check.py'), PCB]).returncode:
                failed = True
    print('check:', 'FAILED' if failed else 'ok')
    return not failed


def board(cli):
    """Placement prints (1:1, bottom seen from below) and 3D renders of the board, in out/."""
    if not os.path.exists(PCB):
        print('board: no board yet')
        return True
    os.makedirs(OUT, exist_ok=True)
    ok = run(cli, 'pcb', 'export', 'pdf', '--layers', 'F.Fab,F.Cu,Edge.Cuts', '--mode-single', '--black-and-white',
             '--sketch-pads-on-fab-layers', '-o', os.path.join(OUT, NAME + '-placement-top.pdf'), PCB) == 0
    ok &= run(cli, 'pcb', 'export', 'pdf', '--layers', 'B.Fab,B.Cu,Edge.Cuts', '--mode-single', '--black-and-white',
              '--sketch-pads-on-fab-layers', '--mirror', '-o', os.path.join(OUT, NAME + '-placement-bottom.pdf'),
              PCB) == 0
    # top and bottom straight on and the two 3D views of the dimensions sheet on a transparent background
    clear = ['--background', 'transparent', '-w', '1600', '-h', '1600']
    for name, extra in (('top', ['--side', 'top'] + clear), ('bottom', ['--side', 'bottom'] + clear),
                        ('3d', ['--rotate', '-60,0,30', '--perspective', '--background', 'opaque', '-w', '1600',
                                '-h', '1100']),
                        ('3d-top', ['--rotate', '-55,0,215', '--perspective', '--zoom', '0.75'] + clear),
                        ('3d-bottom', ['--side', 'bottom', '--rotate', '-55,0,35', '--perspective', '--zoom', '0.75'] + clear)):
        ok &= run(cli, 'pcb', 'render', *extra, '--quality', 'high',
                  '-o', os.path.join(OUT, '%s-%s.png' % (NAME, name)), PCB, quiet=True) == 0
    # parts sheet: the renders with every part tagged and a legend (resistor colour codes), also next to the README
    sheet = ''
    try:
        import PIL  # noqa: F401
        with tempfile.TemporaryDirectory() as tmp:
            parts = os.path.join(tmp, 'parts.json')
            if ok and subprocess.run([kicad_python(cli), os.path.join(ROOT, 'tools', 'pcb_map.py'), 'extract', PCB,
                                      parts], stdout=subprocess.DEVNULL).returncode == 0:
                sys.path.insert(0, os.path.join(ROOT, 'tools'))
                import pcb_parts
                png = os.path.join(OUT, NAME + '-parts.png')
                pcb_parts.draw(parts, os.path.join(OUT, NAME + '-top.png'), os.path.join(OUT, NAME + '-bottom.png'),
                               png, NAME)
                shutil.copyfile(png, os.path.join(os.path.dirname(PCB), 'parts.png'))
                # dimensions sheet (outline, holes, wire vias, 3D views), also with the parts list
                import pcb_dims
                renders = [os.path.join(OUT, '%s-%s.png' % (NAME, v)) for v in ('top', 'bottom', '3d-top', '3d-bottom')]
                for tag, with_parts in (('dims', False), ('dims-parts', True)):
                    png = os.path.join(OUT, '%s-%s.png' % (NAME, tag))
                    pcb_dims.draw(parts, *renders, png, NAME, with_parts)
                    if with_parts:                       # the big sheet as JPEG next to the README (a third the size)
                        from PIL import Image
                        Image.open(png).convert('RGB').save(os.path.join(os.path.dirname(PCB), tag + '.jpg'),
                                                            quality=90)
                    else:
                        shutil.copyfile(png, os.path.join(os.path.dirname(PCB), tag + '.png'))
                sheet = ', parts and dimensions sheets (also parts.png, dims.png, dims-parts.jpg in %s)' % rel(
                    os.path.dirname(PCB))
    except ImportError:
        sheet = ' (pip install pillow for the parts and dimensions sheets)'
    print('board: placement PDFs and renders%s in %s' % (sheet, rel(OUT)))
    return ok


def laser(cli):
    """Files for a home-made board, in out/laser/: copper images for a laser (paint/resist ablation) of each copper
    face, the registration jig (two-sided boards only), the drill file and table. All SVGs share the board outline's
    bounding box, so they line up when placed at the same position in the laser software."""
    if not routed():
        print('laser: board not routed yet')
        return False
    dst = os.path.join(OUT, 'laser')
    os.makedirs(dst, exist_ok=True)
    svg = ['pcb', 'export', 'svg', '--mode-single', '--black-and-white', '--page-size-mode', '2',
           '--exclude-drawing-sheet']
    faces = BOARDS[BOARD][4]
    files = {
        # negative: black = laser fires (paint removed, copper etched), white = copper; small drill marks
        # leave an etched pit at each hole centre that guides the drill
        'top': ['--layers', 'F.Cu,Edge.Cuts', '--negative', '--drill-shape-opt', '1'],
        'bottom': ['--layers', 'B.Cu,Edge.Cuts', '--negative', '--drill-shape-opt', '1', '--mirror'],
        # registration: two 3 mm pin holes (H1 and H4 positions) cut into a sheet fixed to the laser bed
        'jig': ['--layers', 'User.1,Edge.Cuts', '--drill-shape-opt', '0'],
    }
    if len(faces) < 2:                                   # one face: no jig; drop files of an earlier two-face export
        files = {k: v for k, v in files.items() if k in faces}
        for old in glob.glob(os.path.join(dst, NAME + '-*')):
            if any(os.path.basename(old).startswith('%s-%s' % (NAME, k)) for k in ('top', 'jig')):
                os.remove(old)
    ok = True
    # copper images from a copy of the board filled with copper (no net) wherever there is none further than the
    # isolation band from it (keep-outs, the USB notch, outside the outline): only that band is burned
    tmp = tempfile.mkdtemp()
    filled = os.path.join(tmp, NAME + '.kicad_pcb')
    ok &= subprocess.run([kicad_python(cli), os.path.join(ROOT, 'tools', 'pcb_isolation.py'), '--fill', PCB, filled],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    for name, extra in files.items():
        out = os.path.join(dst, '%s-%s.svg' % (NAME, name))
        ok &= run(cli, *svg, *extra, '-o', out, PCB if name == 'jig' else filled, quiet=True) == 0
    shutil.rmtree(tmp, ignore_errors=True)
    ok &= run(cli, 'pcb', 'export', 'drill', '--format', 'excellon', '--excellon-units', 'mm',
              '--generate-map', '--map-format', 'pdf', '--generate-report',
              '--report-path', os.path.join(dst, NAME + '-drill-report.txt'), '-o', dst + os.sep, PCB,
              quiet=True) == 0
    # vector isolation G-code (top, bottom mirrored): outlines around the copper instead of a full raster
    iso = subprocess.run([kicad_python(cli), os.path.join(ROOT, 'tools', 'pcb_isolation.py'), PCB, dst, NAME,
                          ','.join(faces)],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in iso.stdout.splitlines():
        if line.startswith('isolation'):
            print(line)
    ok &= iso.returncode == 0
    # PNGs for raster software. LaserGRBL engraves one line per pixel row (Passthrough: horizontal; Line To Line:
    # also vertical, for a cross pass), so the resolution is the line spacing: LASER_LPMM lines per mm, default
    # 12.5 (0.08 mm). Written exactly into the PNG, so Line To Line at the same lines/mm does not resample. Pure
    # black and white, no grey edge pixels to be re-interpreted. Needs PyMuPDF.
    lpmm = float(os.environ.get('LASER_LPMM', '12.5'))
    tag = ('%.3f' % (1 / lpmm)).rstrip('0')
    try:
        import pymupdf
        for old in glob.glob(os.path.join(dst, NAME + '-*dpi.png')) + glob.glob(os.path.join(dst, NAME + '-*mm.png')):
            os.remove(old)
        zoom = lpmm * 25.4 / 72
        for name in faces:
            pix = pymupdf.open(os.path.join(dst, '%s-%s.svg' % (NAME, name)))[0].get_pixmap(
                matrix=pymupdf.Matrix(zoom, zoom), colorspace=pymupdf.csGRAY)
            bw = pymupdf.Pixmap(pymupdf.csGRAY, pix.width, pix.height,
                                bytes(255 if v >= 128 else 0 for v in pix.samples), False)
            png = os.path.join(dst, '%s-%s-%smm.png' % (NAME, name, tag))
            bw.save(png)
            set_png_resolution(png, round(lpmm * 1000))
        pngs = ' and PNGs at %g lines/mm (%s mm)' % (lpmm, tag)
        # placement map: parts, wire vias and both-face pins over the copper (tools/pcb_map.py)
        with tempfile.TemporaryDirectory() as tmp:
            parts = os.path.join(tmp, 'parts.json')
            top_svg = None
            if 'top' not in faces:                       # one face: the top panel shows the copper seen through
                top_svg = os.path.join(tmp, 'see-through.svg')
                run(cli, *svg, '--layers', 'B.Cu,Edge.Cuts', '--negative', '--drill-shape-opt', '1', '-o', top_svg,
                    PCB, quiet=True)
            if subprocess.run([kicad_python(cli), os.path.join(ROOT, 'tools', 'pcb_map.py'), 'extract', PCB, parts],
                              stdout=subprocess.DEVNULL).returncode == 0:
                sys.path.insert(0, os.path.join(ROOT, 'tools'))
                import pcb_map
                pcb_map.draw(parts, dst, os.path.join(dst, NAME + '-placement-map.png'), NAME, top_svg)
                pngs += ', placement map'
            else:
                ok = False
    except ImportError:
        pngs = ' (pip install pymupdf for the PNGs and the placement map)'
    what = 'top, bottom (mirrored) and jig SVGs' if len(faces) == 2 else 'bottom SVG (mirrored, single-sided)'
    print('laser: %s%s; isolation G-code, drill file and report in %s' % (what, pngs, rel(dst)))
    # the same files and a zip of them next to the README, kept in the repository
    keep = os.path.join(PROJECT, 'laser')
    shutil.rmtree(keep, ignore_errors=True)
    shutil.copytree(dst, keep)
    import zipfile
    with zipfile.ZipFile(os.path.join(keep, NAME + '-laser.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
        for n in sorted(os.listdir(dst)):
            z.write(os.path.join(dst, n), n)
    print('laser: copied with %s-laser.zip to %s' % (NAME, rel(keep)))
    return ok


def set_png_resolution(path, px_per_m):
    """Write the exact resolution into a PNG (pHYs chunk, pixels per metre: 12500 = 12.5 lines/mm)."""
    data = open(path, 'rb').read()
    chunks, pos = [], 8
    while pos < len(data):
        n = struct.unpack('>I', data[pos:pos + 4])[0]
        chunks.append((data[pos + 4:pos + 8], data[pos:pos + 12 + n]))
        pos += 12 + n
    body = b'pHYs' + struct.pack('>IIB', px_per_m, px_per_m, 1)
    phys = struct.pack('>I', 9) + body + struct.pack('>I', zlib.crc32(body))
    out = [c for t, c in chunks if t != b'pHYs']
    out.insert(1, phys)                                 # right after IHDR
    with open(path, 'wb') as fh:
        fh.write(data[:8] + b''.join(out))


FAB_LAYERS = 'F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,Edge.Cuts'


def fab(cli):
    """Order files for a PCB maker, in out/fab/: Gerbers (Protel extensions, no X2 or netlist attributes, silkscreen
    clipped at the mask openings), Excellon drill files in mm with PTH and NPTH separate, and one zip to upload.
    Only the layers in FAB_LAYERS go in: the laser jig (User.1), fab, courtyard and comment layers stay out."""
    if not routed():
        print('fab: board not routed yet')
        return False
    dst = os.path.join(OUT, 'fab')
    files = os.path.join(dst, 'gerbers')
    shutil.rmtree(dst, ignore_errors=True)
    os.makedirs(files)
    ok = run(cli, 'pcb', 'export', 'gerbers', '--layers', FAB_LAYERS, '--no-x2', '--no-netlist',
             '--subtract-soldermask', '-o', files + os.sep, PCB, quiet=True) == 0
    ok &= run(cli, 'pcb', 'export', 'drill', '--format', 'excellon', '--excellon-units', 'mm',
              '--excellon-zeros-format', 'decimal', '--drill-origin', 'absolute', '--excellon-separate-th',
              '--generate-report', '--report-path', os.path.join(dst, NAME + '-drill-report.txt'),
              '-o', files + os.sep, PCB, quiet=True) == 0
    if not ok:
        return False
    names = sorted(os.listdir(files))
    archive = os.path.join(dst, NAME + '-fab.zip')
    import zipfile
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for n in names:
            z.write(os.path.join(files, n), n)
    print('fab: %s (%s)' % (os.path.relpath(archive, ROOT), ', '.join(names)))
    keep = os.path.join(PROJECT, 'fab')                  # the zip next to the README, kept in the repository
    os.makedirs(keep, exist_ok=True)
    shutil.copyfile(archive, os.path.join(keep, os.path.basename(archive)))
    print('fab: copied to %s' % rel(keep))
    return True


def write_if_changed(path, text):
    old = open(path, encoding='utf8').read() if os.path.exists(path) else None
    if old != text:
        with open(path, 'w', encoding='utf8', newline='\n') as fh:
            fh.write(text)
    return old != text


def docs(cli):
    """Schematic SVG for the docs (committed, no timestamp) and a PDF for printing (out/, not committed)."""
    os.makedirs(OUT, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        if run(cli, 'sch', 'export', 'svg', '-o', tmp, SCH):
            return False
        svg = open(os.path.join(tmp, NAME + '.svg'), encoding='utf8').read()
        svg = re.sub(r'<title>.*?</title>', '<title>%s</title>' % BOARDS[BOARD][3], svg, count=1)
        changed = write_if_changed(os.path.join(DOCS, BOARDS[BOARD][2]), svg)
    if run(cli, 'sch', 'export', 'pdf', '-o', os.path.join(OUT, NAME + '-schematic.pdf'), SCH):
        return False
    print('docs: docs/schematics/%s' % BOARDS[BOARD][2], '(updated)' if changed else '(unchanged)')
    print('      %s%s-schematic.pdf' % (rel(OUT), NAME))
    return True


def main():
    args = sys.argv[1:]
    which = 'flight'
    for flag in ('--board', '-b'):
        if flag in args:
            i = args.index(flag)
            if i + 1 >= len(args) or args[i + 1] not in BOARDS:
                sys.exit(__doc__)
            which = args[i + 1]
            del args[i:i + 2]
    select(which)
    steps = args or ['check', 'docs', 'board']
    cli = kicad_cli()
    for step in steps:
        if step not in ('check', 'docs', 'board', 'laser', 'fab'):
            sys.exit(__doc__)
        if not {'check': check, 'docs': docs, 'board': board, 'laser': laser, 'fab': fab}[step](cli):
            sys.exit(1)


if __name__ == '__main__':
    main()
