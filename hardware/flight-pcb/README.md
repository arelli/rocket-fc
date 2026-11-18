# Flight computer PCB (KiCad 10)

KiCad project of the flight computer. The schematic is the **wiring master**:
pin numbers follow [include/board_pins.h](../../include/board_pins.h), and
[docs/HARDWARE.md](../../docs/HARDWARE.md) explains each circuit.

| File | Contents |
|---|---|
| `rocket-fc-flight.kicad_pro` | Project, design rules and net classes |
| `rocket-fc-flight.kicad_sch` | Schematic (one A3 sheet) |
| `rocket-fc-flight.kicad_pcb` | Board: 29 x 101.3 mm, two layers |
| `lib/rocket-fc.kicad_sym` | Symbols: XIAO ESP32-S3 + Wio-SX1262, SparkFun MPU-9250 breakout, 6-pin GY-BMP280, L76K |
| `lib/rocket-fc.pretty/` | Footprints of those modules and of the 1000 uF capacitor lying flat |
| `out/` | Generated files (not committed) |

The library tables use `${KIPRJMOD}`, so the project opens on any computer
with no library setup.

**Status:** schematic done (ERC clean), parts placed (DRC and placement
checks clean). Routing still to do.

## Layout

The board sits centred in the 32 mm airframe, so the tall parts are on the
centre line and only low parts (resistors lying flat) are near the edges.
`y` runs from the USB end (0) to the far end (101.3 mm).

| Zone (y, mm) | Top | Bottom |
|---|---|---|
| 0–9 | M3 hole H1 (corner), GPS lead pads J5 | Battery J1 and main switch J2: JST-XH side entry, plugs from the end |
| 9–37 | XIAO + Wio-SX1262 (USB-C 8.9 mm in from the end), resistors in the edge strips | SparkFun MPU-9250 |
| 37–71 | 1 Ω 5 W resistor along the centre line, resistors and discs in the edge strips | 1000 µF capacitor and the pyro MOSFET, lying flat side by side |
| 71–83 | GY-BMP280, status LED (lying, pointing to the far end) | Buzzer |
| 84–94 | Screw terminals: ARM link J3, igniter J4 (wires from the far end) | |
| 94–101 | M3 hole H2 | |

The L76K GNSS module is off the board, near the nose, on a 3-wire lead
soldered to J5 (3V3, GND, TX); its symbol and footprint stay in the library.

| Footprint | Source |
|---|---|
| XIAO + Wio-SX1262 (U1) | XIAO pattern: 2 x 7, 2.54 mm pitch, rows 15.24 mm apart, 21 x 17.8 mm; two wire pads for the BAT pads on the XIAO underside |
| SparkFun MPU-9250 (U2) | SparkFun's Eagle board file, v10 (17.0 x 17.8 mm); mounted chip side up; its standoff holes are not drilled (they would hit the XIAO pins) |
| GY-BMP280 6-pin (U3) | 2.54 mm header; **outline provisional** (no drawing found) |
| Everything else | KiCad standard library: resistors lying flat (7.62 mm), TO-220 and cement resistor lying flat, JST-XH side entry, 5.08 mm screw terminals, 12 mm buzzer, 5 mm LED lying flat |

## Assembly order

No plated holes: a part is soldered on the face opposite to it, and some
joints end up under a module. `tools/pcb_check.py` lists them; in practice:

1. Small parts in the edge strips, J5, J1, J2, then C1, Q1 (bottom) and
   R3 (top; its leads are soldered on the bottom, beside C1 and Q1).
2. The XIAO's two 7-pin header strips (top), soldered on the bottom and
   **trimmed flush**: the MPU board sits over these joints.
3. The MPU-9250 on its header (bottom), soldered on the top between the two
   XIAO header strips.
4. The buzzer (bottom), soldered on the top, then the barometer (top) over
   those joints.
5. The XIAO + Wio-SX1262 stack onto its header strips, and the two wires
   from the XIAO's BAT pads to BAT+ / BAT-.
6. Screw terminals, LED.

## Checks

`python tools/pcb_export.py` (from the repository root) runs:

- **ERC** on the schematic.
- **DRC** with schematic parity. Silkscreen and solder-mask rules are off
  (home-made board: neither). Holes inside another part's courtyard are
  allowed: that is how the board fits, and the next check covers it.
- **`tools/pcb_check.py`** (with KiCad's Python): every through-hole joint
  can be soldered (nothing lying flat covers it on the other face; joints
  under a module give the assembly order above), and every part fits the
  tube: 15.5 mm radius, its height (table in the script) at its distance
  from the centre line.

Measure before etching (the heights in `tools/pcb_check.py` are estimates):

| Part | Assumed | Limit where it sits |
|---|---|---|
| Screw terminals J3, J4 | 10.0 mm | 10.4 mm |
| XIAO + Wio-SX1262 stack on its headers | 9.0 mm | 11.9 mm |
| 1 Ω 5 W resistor | 9.5 mm | 14.0 mm |
| Buzzer | 9.5 mm | 13.5 mm |

## Board constraints

- Airframe inner diameter 32 mm: board centred, 29 mm wide; M3 holes at
  both ends for the sled.
- Off the board: 18350 cell in its own bay (J1), main switch (J2), arm link
  (J3), igniter (J4), GPS module (J5 lead).
- Made at home: laser-exposed resist and etching, two layers, no plated holes.
  Through-hole parts only (their leads also join the two sides).
  Design rules (project settings): tracks ≥ 0.4 mm (0.5 mm default),
  clearance ≥ 0.4 mm, copper ring ≥ 0.35 mm, vias as soldered wire
  (0.8 mm drill, 2 mm pad), kept few and never under a module.
- Net classes: **Pyro** (CELL+, ARM, IGN+, IGN-) 2.5 mm, **Power** (BAT_SW,
  +3V3, GND) 0.8 mm, **Default** 0.5 mm.

## Workflow

1. Open `rocket-fc-flight.kicad_pro` in KiCad 10 and edit. After a schematic
   change, update the board with **Tools → Update PCB from Schematic**.
2. Run `python tools/pcb_export.py` from the repository root: the checks
   above, `docs/schematics/flight.svg`, and in `out/` a printable schematic,
   1:1 placement prints (top, and bottom seen from below) and 3D renders.
   It exits with an error on any problem.
3. If the wiring changed, update `include/board_pins.h` and
   `docs/HARDWARE.md` in the same commit.

KiCad's backups, lock files, caches and per-user view settings (`*.kicad_prl`)
are git-ignored.
