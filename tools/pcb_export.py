"""Checks and exports for the flight-computer KiCad project (hardware/flight-pcb).

Usage:
    python tools/pcb_export.py            # check, then export the docs
    python tools/pcb_export.py check      # ERC (and DRC once the board exists); exit 1 on violations
    python tools/pcb_export.py docs       # docs/schematics/flight.svg, printable PDF in hardware/flight-pcb/out/

Needs KiCad 10 (kicad-cli). Found on PATH, in KICAD_CLI, or in the default install folder.
"""
import glob
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


def run(*args):
    print('>', 'kicad-cli', ' '.join(args[1:]))
    return subprocess.run(args).returncode


def check(cli):
    failed = False
    with tempfile.TemporaryDirectory() as tmp:
        report = os.path.join(tmp, 'erc.rpt')
        if run(cli, 'sch', 'erc', '--severity-all', '--exit-code-violations', '-o', report, SCH):
            print(open(report, encoding='utf8').read())
            failed = True
        if os.path.exists(PCB):
            report = os.path.join(tmp, 'drc.rpt')
            if run(cli, 'pcb', 'drc', '--severity-all', '--schematic-parity', '--exit-code-violations',
                   '-o', report, PCB):
                print(open(report, encoding='utf8').read())
                failed = True
    print('check:', 'FAILED' if failed else 'ok')
    return not failed


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
    steps = sys.argv[1:] or ['check', 'docs']
    cli = kicad_cli()
    for step in steps:
        if step not in ('check', 'docs'):
            sys.exit(__doc__)
        if not {'check': check, 'docs': docs}[step](cli):
            sys.exit(1)


if __name__ == '__main__':
    main()
