"""Checks and exports for the flight-computer KiCad project (hardware/flight-pcb).

Usage:
    python tools/pcb_export.py            # check, docs, board
    python tools/pcb_export.py check      # ERC, DRC + schematic parity, tools/pcb_check.py; exit 1 on problems
    python tools/pcb_export.py docs       # docs/schematics/flight.svg, printable PDF in hardware/flight-pcb/out/
    python tools/pcb_export.py board      # 1:1 placement PDFs (top, bottom) and 3D renders in hardware/flight-pcb/out/
    python tools/pcb_export.py laser      # laser copper images, registration jig, drill file, placement map in
                                          # hardware/flight-pcb/out/laser/
                                          # (PNG line spacing: LASER_DPI, default 254 dpi = 0.1 mm)

Needs KiCad 10 (kicad-cli). Found on PATH, in KICAD_CLI, or in the default install folder. tools/pcb_check.py
runs with KiCad's own Python (next to kicad-cli, or KICAD_PYTHON). Until the board is routed, DRC's unrouted
connections are reported but not counted.
"""
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(ROOT, 'hardware', 'flight-pcb')
NAME = 'rocket-fc-flight'
SCH = os.path.join(PROJECT, NAME + '.kicad_sch')
PCB = os.path.join(PROJECT, NAME + '.kicad_pcb')
DOCS = os.path.join(ROOT, 'docs', 'schematics')
OUT = os.path.join(PROJECT, 'out')  # generated files, not committed


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
    for name, extra in (('top', ['--side', 'top']), ('bottom', ['--side', 'bottom']),
                        ('3d', ['--rotate', '-60,0,30', '--perspective'])):
        ok &= run(cli, 'pcb', 'render', *extra, '--quality', 'high', '--background', 'opaque', '-w', '1600',
                  '-h', '1600' if name != '3d' else '1100', '-o', os.path.join(OUT, '%s-%s.png' % (NAME, name)),
                  PCB, quiet=True) == 0
    print('board: placement PDFs and renders in hardware/flight-pcb/out/')
    return ok


def laser(cli):
    """Files for a home-made board, in out/laser/: copper images for a laser (paint/resist ablation), the
    registration jig, the drill file and table. All SVGs share the board outline's bounding box, so they line up
    when placed at the same position in the laser software."""
    if not routed():
        print('laser: board not routed yet')
        return False
    dst = os.path.join(OUT, 'laser')
    os.makedirs(dst, exist_ok=True)
    svg = ['pcb', 'export', 'svg', '--mode-single', '--black-and-white', '--page-size-mode', '2',
           '--exclude-drawing-sheet']
    files = {
        # negative: black = laser fires (paint removed, copper etched), white = copper; small drill marks
        # leave an etched pit at each hole centre that guides the drill
        'top': ['--layers', 'F.Cu,Edge.Cuts', '--negative', '--drill-shape-opt', '1'],
        'bottom': ['--layers', 'B.Cu,Edge.Cuts', '--negative', '--drill-shape-opt', '1', '--mirror'],
        # registration: two 3 mm pin holes (H1 and H4 positions) cut into a sheet fixed to the laser bed
        'jig': ['--layers', 'User.1,Edge.Cuts', '--drill-shape-opt', '0'],
    }
    ok = True
    for name, extra in files.items():
        out = os.path.join(dst, '%s-%s.svg' % (NAME, name))
        ok &= run(cli, *svg, *extra, '-o', out, PCB, quiet=True) == 0
    ok &= run(cli, 'pcb', 'export', 'drill', '--format', 'excellon', '--excellon-units', 'mm',
              '--generate-map', '--map-format', 'pdf', '--generate-report',
              '--report-path', os.path.join(dst, NAME + '-drill-report.txt'), '-o', dst + os.sep, PCB,
              quiet=True) == 0
    # PNGs for raster software (LaserGRBL "Passthrough": one engraved line per pixel row, so the DPI is the line
    # spacing; 254 dpi = 0.1 mm). Pure black and white, no grey edge pixels to be re-interpreted. Needs PyMuPDF.
    dpi = int(os.environ.get('LASER_DPI', '254'))
    try:
        import pymupdf
        for name in ('top', 'bottom'):
            pix = pymupdf.open(os.path.join(dst, '%s-%s.svg' % (NAME, name)))[0].get_pixmap(
                dpi=dpi, colorspace=pymupdf.csGRAY)
            bw = pymupdf.Pixmap(pymupdf.csGRAY, pix.width, pix.height,
                                bytes(255 if v >= 128 else 0 for v in pix.samples), False)
            bw.set_dpi(dpi, dpi)
            bw.save(os.path.join(dst, '%s-%s-%ddpi.png' % (NAME, name, dpi)))
        pngs = ' and %d dpi PNGs' % dpi
        # placement map: parts, wire vias and both-face pins over the copper (tools/pcb_map.py)
        with tempfile.TemporaryDirectory() as tmp:
            parts = os.path.join(tmp, 'parts.json')
            if subprocess.run([kicad_python(cli), os.path.join(ROOT, 'tools', 'pcb_map.py'), 'extract', PCB, parts],
                              stdout=subprocess.DEVNULL).returncode == 0:
                sys.path.insert(0, os.path.join(ROOT, 'tools'))
                import pcb_map
                pcb_map.draw(parts, dst, os.path.join(dst, NAME + '-placement-map.png'))
                pngs += ', placement map'
            else:
                ok = False
    except ImportError:
        pngs = ' (pip install pymupdf for the PNGs and the placement map)'
    print('laser: top, bottom (mirrored) and jig SVGs%s; drill file and report in hardware/flight-pcb/out/laser/'
          % pngs)
    return ok


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
        svg = re.sub(r'<title>.*?</title>', '<title>rocket-fc flight computer schematic</title>', svg, count=1)
        changed = write_if_changed(os.path.join(DOCS, 'flight.svg'), svg)
    if run(cli, 'sch', 'export', 'pdf', '-o', os.path.join(OUT, NAME + '-schematic.pdf'), SCH):
        return False
    print('docs: docs/schematics/flight.svg', '(updated)' if changed else '(unchanged)')
    print('      hardware/flight-pcb/out/%s-schematic.pdf' % NAME)
    return True


def main():
    steps = sys.argv[1:] or ['check', 'docs', 'board']
    cli = kicad_cli()
    for step in steps:
        if step not in ('check', 'docs', 'board', 'laser'):
            sys.exit(__doc__)
        if not {'check': check, 'docs': docs, 'board': board, 'laser': laser}[step](cli):
            sys.exit(1)


if __name__ == '__main__':
    main()
