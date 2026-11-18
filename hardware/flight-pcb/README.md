# Flight computer PCB (KiCad 10)

KiCad project of the flight computer. The schematic is the **wiring master**:
pin numbers follow [include/board_pins.h](../../include/board_pins.h), and
[docs/HARDWARE.md](../../docs/HARDWARE.md) explains each circuit.

| File | Contents |
|---|---|
| `rocket-fc-flight.kicad_pro` | Project, design rules and net classes |
| `rocket-fc-flight.kicad_sch` | Schematic (one A3 sheet) |
| `rocket-fc-flight.kicad_pcb` | Board: 30 x 90.1 mm (97.1 mm with the mounting ears), two layers |
| `lib/rocket-fc.kicad_sym` | Symbols: XIAO ESP32-S3 + Wio-SX1262, SparkFun MPU-9250 breakout, 6-pin GY-BMP280, L76K |
| `lib/rocket-fc.pretty/` | Footprints of those modules and of the 1000 uF capacitor lying flat |
| `out/` | Generated files (not committed) |

The library tables use `${KIPRJMOD}`, so the project opens on any computer
with no library setup.

**Status:** schematic done (ERC clean), board routed: DRC 0 problems,
schematic parity 0, `tools/pcb_check.py` ok. Laser exports and a test etch
still to do.

Routing uses both faces and **35 wire vias**: a hole with a short wire
soldered on both sides wherever a track changes face (no plated holes).

## Layout

The board sits centred in the 32 mm airframe, so the tall parts are on the
centre line and only low parts (resistors lying flat) are near the edges.
`y` runs from the XIAO's USB face (0) to the far end (90.1 mm).

| Zone (y, mm) | Top | Bottom |
|---|---|---|
| −7–0 | Two ears with M3 holes H1, H2; the notch between them is free for the USB-C plug | Power plug J1 enters through the same notch |
| 0–34 | XIAO with the Wio-SX1262 on top, USB-C flush with the notch; continuity divider (R4, R5, C3) on the left edge, battery divider (R1, R2, C2) on the right; BAT+ / BAT− wire pads | J1: JST-XH 4-pin side entry (1–2 cell, 3–4 main switch); SparkFun MPU-9250 |
| 34–60 | 1000 µF lying flat on the centre line (pins at the far end), gate resistors R6, R7 on the right edge | IRLB8748 (left) and 1 Ω 2 W resistor (right) lying flat, pins at the far end |
| 60–73 | GY-BMP280, GPS lead pads J5 on the right edge | Buzzer |
| 73–90 | Screw terminal J3 (ARM link), centred | Screw terminal J4 (igniter), centred, staggered behind J3 |
| 83–90 | M3 holes H3, H4 in the corners | |

- **USB:** the XIAO's USB-C face is at the board edge, in a 14 mm wide notch
  between the two ears, so the plug goes straight in with the board on its
  sled.
- **Screw terminals:** one per face, both on the centre line (where the tube
  is tallest), wires entering from the far end. J4 is 6.6 mm further along
  than J3 so that neither covers the other's solder joints.
- **No status LED** (the buzzer stays): see docs/HARDWARE.md.
- The L76K GNSS module is off the board, near the nose, on a 3-wire lead
  soldered to J5 (3V3, GND, TX); its symbol and footprint stay in the library.

| Footprint | Source |
|---|---|
| XIAO + Wio-SX1262 (U1) | XIAO pattern: 2 x 7, 2.54 mm pitch, rows 15.24 mm apart, 21 x 17.8 mm; **5V, D8, D9, D10 have no pad** (pull those pins out of the header strip: the radio uses them through its own connector); two wire pads for the BAT pads on the XIAO underside, past the MPU |
| SparkFun MPU-9250 (U2) | SparkFun's Eagle board file, v10 (17.0 x 17.8 mm); mounted chip side up; **AD0, INT, AUXCL, AUXDA have no pad** (pull those pins); its standoff holes are not drilled (they would hit the XIAO pins) |
| GY-BMP280 6-pin (U3) | 2.54 mm header; **outline provisional** (no drawing found) |
| Everything else | KiCad standard library: resistors lying flat (7.62 mm), 1 Ω 2 W (DIN0414, 15.24 mm), TO-220 lying flat, JST-XH 4-pin side entry, 5.08 mm screw terminals, 12 mm buzzer, solder-wire pads for the GPS lead. The two 100 nF discs sit at the board edge: **bend them flat** (3.5 mm high at most) |

## Assembly order

No plated holes: a part is soldered on the face opposite to it, and some
joints end up under a part. `tools/pcb_check.py` lists them; in practice:

0. **All 35 wire vias first**: a bare wire (e.g. a resistor lead offcut)
   through each 0.8 mm via hole, soldered on both faces and trimmed flush.
   Several sit under parts (the XIAO, MPU, power plug, buzzer, C1, R3).
1. Small parts in the edge strips, then C1 (top) and Q1, R3 (bottom).
2. J1, the power plug (bottom): its pins are soldered on top, under where
   the XIAO goes.
3. The XIAO's header strips (top, **low male headers, not sockets**: the
   stack must stay under 11.9 mm; 5V, D8, D9, D10 pulled), soldered on the
   bottom and **trimmed flush**: the MPU board sits over these joints.
4. The MPU-9250 on its headers (bottom), soldered on the top between the
   XIAO header strips.
5. The buzzer (bottom), soldered on the top, then the barometer (top) over
   those joints.
6. The XIAO with the Wio-SX1262 onto its header strips, and the two wires
   from the XIAO's BAT pads to BAT+ / BAT− (past the MPU, both faces free).
7. Screw terminals, the GPS lead.

Pins that `tools/pcb_check.py` lists under "solder on both faces" carry
copper on both sides and need a joint on each.

## Checks

`python tools/pcb_export.py` (from the repository root) runs:

- **ERC** on the schematic.
- **DRC** with schematic parity. Silkscreen and solder-mask rules are off
  (home-made board: neither). Holes inside another part's courtyard are
  allowed: that is how the board fits, and the next check covers it.
- **`tools/pcb_check.py`** (with KiCad's Python), the rules of a board without
  plated holes:
  - every through-hole joint can be soldered (on the face opposite its part,
    or on its own face when the pin is exposed there, e.g. a lying resistor);
    joints under a module give the assembly order above;
  - every track meets a pin only on a face where that pin is soldered, and
    wire vias sit where both faces are reachable;
  - no foreign copper under the IRLB8748's metal tab (it is the drain) or
    under the M3 screw heads;
  - every part fits the tube: 15.5 mm radius, its height (table in the
    script) at its distance from the centre line.

Measure before etching (the heights in `tools/pcb_check.py` are estimates):

| Part | Assumed | Limit where it sits |
|---|---|---|
| XIAO + Wio-SX1262 stack on its headers | 10.0 mm | 11.9 mm |
| Screw terminals J3, J4 | 10.0 mm | 13.8 mm |
| Buzzer | 9.5 mm | 13.5 mm |

## Board constraints

- Airframe inner diameter 32 mm: board centred, 30 mm wide; four M3 holes
  for the sled (two ears at the USB end, two corners at the far end).
- Off the board: 18350 cell and main switch (J1), arm link (J3), igniter
  (J4), GPS module (J5 lead).
- Made at home: laser-exposed resist and etching, two layers, no plated holes.
  Through-hole parts only (their leads also join the two sides).
  Design rules (project settings): tracks ≥ 0.4 mm, clearance 0.4 mm,
  copper ring ≥ 0.35 mm, vias as soldered wire (0.8 mm drill, 1.6 mm pad).
  A wire via may sit under a part (soldered and trimmed before the part is
  fitted), never under the MOSFET's metal tab.
- Net classes (default widths): **Pyro** (CELL+, ARM, IGN+, IGN-) 1.0 mm,
  **Power** (BAT_SW, +3V3, GND) 0.6 mm, **Default** 0.4 mm. A 1.0 mm track
  drops about 0.1 V over the board at the 2.7 A firing current.

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
