# Flight computer PCB (KiCad 10)

KiCad project of the flight computer. The schematic is the **wiring master**:
pin numbers follow [include/board_pins.h](../../include/board_pins.h), and
[docs/HARDWARE.md](../../docs/HARDWARE.md) explains each circuit.

| File | Contents |
|---|---|
| `rocket-fc-flight.kicad_pro` | Project, design rules and net classes |
| `rocket-fc-flight.kicad_sch` | Schematic (one A3 sheet) |
| `lib/rocket-fc.kicad_sym` | Symbols: XIAO ESP32-S3 + Wio-SX1262, MPU-9250 and GY-BMP280 breakouts, L76K |
| `lib/rocket-fc.pretty/` | Footprints of those modules (drawn from measured parts) |
| `out/` | Generated files (not committed) |

The library tables use `${KIPRJMOD}`, so the project opens on any computer
with no library setup.

**Status:** schematic done (ERC clean). Module footprints, board outline,
placement and routing still to do.

## Board constraints

- Airframe inner diameter 32 mm: the board is centred in the tube, about
  28–29 mm wide and 70–90 mm long, with parts on both sides.
- Off the board, each on a connector: 18350 cell in its own bay (J1), main
  switch (J2), arm link (J3), igniter (J4).
- Made at home: laser-exposed resist and etching, two layers, no plated holes.
  Design rules (project settings): tracks ≥ 0.4 mm (0.5 mm default),
  clearance ≥ 0.4 mm, vias as soldered wire (0.8 mm drill, 2 mm pad), kept few
  and never under a module.
- Net classes: **Pyro** (CELL+, ARM, IGN+, IGN-) 2.5 mm, **Power** (BAT_SW,
  +3V3, GND) 0.8 mm, **Default** 0.5 mm.

## Workflow

1. Open `rocket-fc-flight.kicad_pro` in KiCad 10 and edit.
2. Run `python tools/pcb_export.py` from the repository root: ERC (and DRC
   once the board exists), then `docs/schematics/flight.svg` and a printable
   PDF in `out/`. It exits with an error on any violation.
3. If the wiring changed, update `include/board_pins.h` and
   `docs/HARDWARE.md` in the same commit.

KiCad's backups, lock files, caches and per-user view settings (`*.kicad_prl`)
are git-ignored.
