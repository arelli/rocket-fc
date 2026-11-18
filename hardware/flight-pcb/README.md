# Flight computer PCB (KiCad 10)

KiCad project of the flight computer. The schematic is the **wiring master**:
pin numbers follow [include/board_pins.h](../../include/board_pins.h), and
[docs/HARDWARE.md](../../docs/HARDWARE.md) explains each circuit.

| File | Contents |
|---|---|
| `rocket-fc-flight.kicad_pro` | Project, design rules and net classes |
| `rocket-fc-flight.kicad_sch` | Schematic (one A3 sheet) |
| `rocket-fc-flight.kicad_pcb` | Board: 30 x 78.2 mm (85.2 mm with the mounting ears), two layers |
| `lib/rocket-fc.kicad_sym` | Symbols: XIAO ESP32-S3 + Wio-SX1262, SparkFun MPU-9250 breakout, 4-pin GY-BMP280, L76K |
| `lib/rocket-fc.pretty/` | Footprints of those modules |
| `lib/rocket-fc.3dshapes/` | Simplified 3D models of those (boxes and cylinders, for the renders and the tube check by eye) |
| `out/` | Generated files (not committed) |

The library tables use `${KIPRJMOD}`, so the project opens on any computer
with no library setup.

**Status:** schematic done (ERC clean), board routed: DRC 0 problems,
schematic parity 0, `tools/pcb_check.py` ok. Laser files ready (below); a
test etch still to do.

Routing uses both faces and **27 wire vias**: a hole with a short wire
soldered on both sides wherever a track changes face (no plated holes).

## Layout

The board sits centred in the 32 mm airframe, so the tall parts are on the
centre line and only low parts (resistors lying flat) are near the edges.
`y` runs from the XIAO's USB face (0) to the far end (78.2 mm).

| Zone (y, mm) | Top | Bottom |
|---|---|---|
| −7–0 | Two ears with M3 holes H1, H2; the notch between them is free for the USB-C plug | Power plug J1 enters through the same notch |
| 0–34 | XIAO with the Wio-SX1262 on top, USB-C flush with the notch; continuity divider (R4, R5, C3) on the left edge, battery divider (R1, R2, C2) on the right; BAT+ / BAT− wire pads | J1: JST-XH 4-pin side entry (1–2 cell, 3–4 main switch); SparkFun MPU-9250 |
| 25–56 | Buzzer centred (34–46), gate resistors R6, R7 and the GPS lead pads J5 on the right edge | IRLB8748 (left) and 1 Ω 2 W resistor (right) lying flat (32–53); 470 µF standing between them (45–54) |
| 46–61 | 4-pin GY-BMP280, sensor side down, header at its far edge | |
| 61–78 | Screw terminal J3 (ARM link), centred | Screw terminal J4 (igniter), centred, staggered behind J3 |
| 71–78 | M3 holes H3, H4 in the corners | |

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
| XIAO + Wio-SX1262 (U1) | XIAO pattern: 2 x 7, 2.54 mm pitch, rows 15.24 mm apart, 21 x 17.8 mm; **5V, D8, D9, D10 have no pad** (pull those pins out of the header strip before soldering it: the radio uses them through its own connector); two wire pads for the BAT pads on the XIAO underside, past the MPU |
| SparkFun MPU-9250 (U2) | SparkFun's Eagle board file, v10 (17.0 x 17.8 mm); chip side away from the board (its labelled side faces the PCB); **AD0, INT, AUXCL, AUXDA have no pad** (pull those pins); its standoff holes are not drilled (they would hit the XIAO pins) |
| GY-BMP280 4-pin (U3) | 10 x 13 mm, 2.54 mm header along a 10 mm edge. **Mounted sensor side down** (labels up, VIN in the square pad): with the sensor up, the real board's pin order (VIN GND SCL SDA) runs the other way to this footprint. The BMP280 then hangs about 1.5 mm above C1's joints |
| C1 470 µF 10 V | KiCad CP_Radial_D8.0mm_P3.50mm, standing on the bottom (12.5 mm tall at most) |
| Everything else | KiCad standard library: resistors lying flat (7.62 mm), 1 Ω 2 W (DIN0414, 15.24 mm), TO-220 lying flat, JST-XH 4-pin side entry, 5.08 mm screw terminals, 12 mm buzzer, solder-wire pads for the GPS lead. The two 100 nF discs sit at the board edge: **bend them flat** (3.5 mm high at most) |

## Making the board (laser and etch)

`python tools/pcb_export.py laser` writes to `out/laser/`:

| File | Use |
|---|---|
| `…-top.svg`, `…-top-1000dpi.png` | Top copper, **negative**: black = laser fires (paint off, copper etched), white = copper stays. Every hole centre is a small black dot, so the etch leaves a pit that centres the drill; the M3 holes are small copper dots |
| `…-bottom.svg`, `…-bottom-1000dpi.png` | Bottom copper, the same, **mirrored** (as seen from below) |
| `…-jig.svg` | Registration jig: two 3 mm pin holes at the H1 and H4 positions |
| `….drl`, `…-drl_map.pdf`, `…-drill-report.txt` | Drill file (mm), drill map and hole count per size |

All three SVGs have the same size (the board outline's bounding box), so
they line up when placed at the same position in the laser software (in
LightBurn: one project, one layer each, align their centres; set the jig's
outline to a layer that does not output). The PNGs are for raster-only
software such as LaserGRBL.

1. **Blank:** double-sided FR4, 1.6 mm, at least 36 x 92 mm. Clean both
   faces (fine abrasive pad, then isopropyl alcohol) and spray thin coats of
   matte black paint on both. Try the laser settings on a scrap painted the
   same way first: the paint must come off cleanly down to bright copper (a
   second pass usually clears residue).
2. **Top:** laser `top` onto the blank.
3. **Drill the four M3 holes** (3.2 mm) on their marks: they register the
   second face.
4. **Jig:** fix a sheet of cardboard or MDF to the bed and, without moving
   it afterwards, cut `jig` into it. Push two 3 mm pins into the holes (drill
   bits shank-down, or M3 screws from below), standing no higher than the
   board.
5. **Bottom:** flip the board **left to right** (about its long axis) onto
   the pins: H2 and H3 now sit on them. Laser `bottom` at the same position
   as the jig.
6. **Etch** both faces (ferric chloride or sodium persulfate), strip the
   paint with acetone. **TOP and BOT must read correctly** on their faces; if
   BOT is mirror-writing, the bottom image was not mirrored. Check every
   track for breaks and shorts with a meter before drilling.
7. **Drill** from the etched pits, then cut and file to the outline (the
   thin copper line on each face). Bits:

| Bit | Holes |
|---|---|
| 0.8 mm | 27 wire vias, resistors, 100 nF, C1, GPS lead pads (J5) |
| 1.0 mm | XIAO, MPU-9250 and barometer headers, BAT pads, buzzer, power plug J1 |
| 1.2 mm | IRLB8748 (Q1) leads, 1 Ω 2 W (R3) |
| 1.3 mm | Screw terminals J3, J4 |
| 3.2 mm | M3 holes H1–H4 (step 3), Q1's tab hole |

## Assembly order

No plated holes: a part is soldered on the face opposite to it, and some
joints end up under a part. `tools/pcb_check.py` lists them; in practice:

0. **All 27 wire vias first**: a bare wire (e.g. a resistor lead offcut)
   through each 0.8 mm via hole, soldered on both faces and trimmed flush.
   Several sit under parts (the XIAO, MPU, power plug, buzzer, barometer,
   J3, R3, R7).
1. Small parts in the edge strips, then Q1, R3 and C1 (bottom). **Trim C1's
   joints flush** on the top and put Kapton tape over them: the barometer's
   sensor hangs over them.
2. J1, the power plug (bottom): its pins are soldered on top, under where
   the XIAO goes.
3. The XIAO's header strips (top, **low male headers, not sockets**: the
   stack must stay under 11.9 mm; 5V, D8, D9, D10 pulled), soldered on the
   bottom and **trimmed flush**: the MPU board sits over these joints.
   The strips go on without the XIAO (it is soldered onto them in step 5),
   so a XIAO that already has header pins must lose them first.
4. The MPU-9250 on its headers (bottom), soldered on the top between the
   XIAO header strips.
5. The XIAO with the Wio-SX1262 onto its header strips, soldered from above,
   and the two wires from the XIAO's BAT pads to BAT+ / BAT− (past the MPU,
   both faces free).
6. The buzzer (top), soldered on the bottom; then the barometer (top),
   **sensor side down**, labels up, VIN in the square pad.
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
| C1 470 µF standing (bottom) | 12.5 mm | 13.9 mm |

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
   It exits with an error on any problem. `python tools/pcb_export.py laser`
   makes the laser files (above).
3. If the wiring changed, update `include/board_pins.h` and
   `docs/HARDWARE.md` in the same commit.

KiCad's backups, lock files, caches and per-user view settings (`*.kicad_prl`)
are git-ignored.
