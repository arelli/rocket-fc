# Ground station PCB (KiCad 10)

KiCad project of the ground station (second XIAO ESP32-S3 + Wio-SX1262 kit,
launch controller). The schematic is the wiring master of this board: pin
numbers follow `pins::ground` in
[include/board_pins.h](../../include/board_pins.h), and
[docs/HARDWARE.md](../../docs/HARDWARE.md#ground-station-second-kit)
explains each circuit. The hand-wired station drawn by `tools/schematics.py`
is the same circuit.

| File | Contents |
|---|---|
| `rocket-fc-ground.kicad_pro` | Project, design rules and net classes |
| `rocket-fc-ground.kicad_sch` | Schematic (one A3 sheet) |
| `rocket-fc-ground.kicad_pcb` | Board: 100 x 80 mm, all tracks on the bottom face |
| `lib/rocket-fc.kicad_sym` | Symbol: XIAO ESP32-S3 + Wio-SX1262 on female headers (all 14 pins) |
| `lib/rocket-fc.pretty/` | Its footprint |
| `lib/rocket-fc.3dshapes/` | Simplified 3D model of the kit (the flight board's, raised onto the sockets) |
| `out/` | Generated files (not committed) |

The library tables use `${KIPRJMOD}`, so the project opens on any computer
with no library setup.

**Status:** schematic done (ERC clean), board routed on one face with a
GND pour: DRC 0 problems, schematic parity 0, `tools/pcb_check.py` ok.
Laser and order files ready (below). Not built yet.

**Single-sided, no wire vias.** All the copper (tracks and a GND pour) is on
the bottom face; every part sits on the bare top face and is soldered
underneath. Use a single-sided blank: one laser job, no flipping, no
registration jig.

## Layout

`y` runs from the USB edge (0) to the terminal edge (80 mm), `x` from left to
right seen from the top.

| Zone | Parts |
|---|---|
| Top edge, centre | XIAO + Wio-SX1262 (U1) on two 1x7 female headers, USB-C 1 mm in from the edge |
| Top left | Buzzer BZ1, button connector J6, ON LED (LED1, R9) |
| Left edge | Launch divider R4, R5, ARM bypass R8; gate resistors R6, R7; IRLB8748 (Q1) standing |
| Under U1 | XIAO BAT lead connector J5, battery divider R1, R2, LINK LED (LED2, R10) |
| Right | MODE switch connector J7, 1000 uF capacitor C1; the rest is free room |
| Bottom | 1 ohm 5 W resistor R3 lying flat; screw terminals J1 igniter, J2 18650, J3 ARM switch, wires entering from the bottom edge |
| Corners | M3 holes H1-H4 (4 mm in from each edge) |

### Connectors

| Ref | Part | Pins (left to right, seen from the top) | Goes to |
|---|---|---|---|
| J1 | Screw terminal 5.08 mm | IGN+, IGN- | Launch leads (the docs' J1/J2), igniter |
| J2 | Screw terminal 5.08 mm | **-**, **+** | 18650 cell holder |
| J3 | Screw terminal 5.08 mm | CELL+, ARM | ARM switch SW2 (rated 5 A or more) |
| J4 | JST-XH 2, vertical | CELL+, BAT_SW | Power switch SW1 |
| J5 | JST-XH 2, vertical | **BAT-**, **BAT+** | The XIAO's BAT pads (underside), short lead |
| J6 | JST-XH 2, vertical | BUTTON, GND | Push button S1 |
| J7 | JST-XH 2, vertical | MODE, GND | MODE switch SW3 (open = BENCH) |

- **CELL+ is live** whenever a cell is in J2: J3 and J4 carry the cell
  voltage before their switches (the launch channel bypasses the power
  switch, as in the hand-wired station).
- The launch current runs CELL+ (J2) -> J3 -> ARM switch -> R3 -> J1 ->
  igniter -> J1 -> Q1 -> GND track straight back to J2, on 2 mm tracks.
- The panel parts (switches, button) stay off the board, in the box lid,
  on JST-XH leads; the LEDs and buzzer are on the board (see *Assembly*
  for LEDs in the lid instead).
- J5: the XIAO's BAT pads are on its underside, so they cannot reach the
  female headers. Solder a short two-wire lead to them, crimp a JST-XH
  plug on it and plug it into J5. This keeps the XIAO removable.

| Footprint | Source |
|---|---|
| XIAO + Wio-SX1262 (U1) | `lib/`: XIAO pattern, 2 x 7, 2.54 mm pitch, rows 15.24 mm apart, all 14 pads (the female headers need them; 3V3, 5V, D8-D10 are unconnected); oval 2.6 x 1.9 mm pads; pin names on the silkscreen |
| Everything else | KiCad standard library: resistors lying flat on a 10.16 mm pitch, 5 W cement resistor (25 x 9 mm, 27.94 mm), TO-220 standing, 1000 uF 10 mm radial (5 mm), 5 mm LEDs, 12 mm buzzer (7.6 mm), JST-XH vertical, Phoenix MKDS 5.08 mm terminals, M3 holes |

Pads are enlarged on the board itself for the laser (resistors 2.4 mm,
terminals 3.2 mm, R3 3.0 mm, C1 and the buzzer 2.8 mm, Q1 2.1 x 3.4 mm,
JST 2.0 x 2.8 mm, LEDs 2.0 x 2.6 mm), so they differ from the library
footprints (that DRC check is off): **Update Footprints from Library** would
shrink them back.

## Making the board (laser and etch)

Same method as the flight board ([hardware/flight-pcb](../flight-pcb/README.md#making-the-board-laser-and-etch)),
for one face. The GND pour fills the copper between the tracks (0.7 mm from
every other net), so the laser only clears a band round each track and pad.

`python tools/pcb_export.py --board ground laser` writes to `out/laser/`:

| File | Use |
|---|---|
| `…-bottom-isolation.nc` | **Vector isolation G-code** (method A, about 17 min): outlines round all copper, mirrored |
| `…-bottom.svg`, `…-bottom-0.08mm.png` | The copper as a raster image (method B), **negative** (black = laser fires, white = copper stays), mirrored. Every hole centre is a small black dot (the etched pit centres the drill); the M3 holes are small copper dots |
| `….drl`, `…-drl_map.pdf`, `…-drill-report.txt` | Drill file (mm), drill map and hole count per size |
| `…-placement-map.png` | Where every part goes (top face, with the copper seen through the board) and the copper face as lasered: print it for assembly |

The frame of every file is the board outline (100 x 80 mm); the G-code
starts at its lower-left corner (X 0-100, Y 0-80), the PNG comes out
100 x 80 mm from its DPI. Laser settings (`LASER_SPOT`, `LASER_BAND`,
`LASER_FEED`, `LASER_S`, `LASER_LPMM`), the M4 power mode and the two raster
passes (Passthrough horizontal, then Line To Line vertical at 12.5
lines/mm) work exactly as for the flight board.

The files are **mirrored** because they show the copper face as seen from
below, which is how it lies on the laser bed (copper up). **BOT** and the
connector labels in the copper read correctly on the finished board.

### Steps

1. **Blank:** single-sided FR4, 1.6 mm, at least 106 x 86 mm. Clean the
   copper (fine abrasive pad, then isopropyl alcohol) and spray thin coats of
   matte black paint. Find the power on a scrap first (flight README,
   step 1).
2. **Laser** the copper face, copper up (method A: the `.nc` file; method B:
   both raster passes without moving anything in between).
3. **Etch** (ferric chloride or sodium persulfate), strip the paint with
   acetone. **BOT** must read correctly; if it is mirror-writing, the file
   was flipped. Check every track for breaks and shorts with a meter, also
   against the GND pour next to it.
4. **Drill** from the etched pits, from the copper side, then cut and file
   to the outline (the thin copper line). Bits:

| Bit | Holes |
|---|---|
| 0.8 mm | Resistors R1, R2, R4-R10 |
| 0.9 mm | LEDs |
| 1.0 mm | Female headers (U1), JST-XH J4-J7, C1, buzzer |
| 1.1 mm | Q1 (IRLB8748) |
| 1.2 mm | R3 (1 ohm 5 W): check its leads, some cement resistors need 1.3 mm |
| 1.3 mm | Screw terminals J1, J2, J3 |
| 3.2 mm | M3 holes H1-H4 |

## Assembly

Every part goes on the top face and is soldered on the bottom; no joint is
under another part, so the order only follows height (low parts first):

1. Resistors (R1, R2, R4-R10 lying flat; R3 last of them, lying flat, 1 mm
   off the board so it cools).
2. LEDs: **flat side (cathode) on the square pad**. Green LED1 = ON, blue
   LED2 = LINK. For LEDs in the box lid instead, solder two wires per LED
   in their place (square pad = cathode).
3. JST-XH J4-J7 (latch side as printed on the silkscreen outline), buzzer
   (**+ on the square pad**), Q1 standing (metal tab towards the top edge,
   as on the outline), C1 (**+ on the square pad**, stripe on the other side).
4. Screw terminals J1-J3, wire openings towards the bottom edge.
5. Female headers: two 1x7, 2.54 mm. Plug them onto the XIAO's pins first,
   set the XIAO in place and solder the headers, then lift the XIAO off
   (keeps the strips straight and parallel).
6. XIAO BAT lead: two wires (about 6 cm) from the BAT pads on the XIAO
   underside to a 2-pin JST-XH plug. **BAT- is the pad nearest the USB
   connector**; it goes to J5 pin 1 (left, square pad). Before plugging the
   XIAO in, with a cell in J2 and the power switch on, check that J5 pin 2
   reads + and pin 1 reads - with a meter; then check the plug's wires the
   same way against the XIAO's pads. Reversing the cell destroys the XIAO.
7. Plug in the XIAO with the Wio-SX1262 (USB-C towards the top edge), the
   antennas (LoRa on the Wio, 2.4 GHz WiFi on the XIAO's own U.FL), the BAT
   lead, and the panel leads.

First power-up: no igniter in J1, ARM switch off, MODE in BENCH; then follow
[docs/BUILD.md](../../docs/BUILD.md).

## Ordering from a PCB maker

`python tools/pcb_export.py --board ground fab` writes `out/fab/` with the
same files as the flight board: `rocket-fc-ground-fab.zip` to upload
(Gerbers with Protel extensions, Excellon drill files in mm with plated and
unplated holes apart, job file), the unzipped Gerbers and the drill report
(plated 0.8-1.3 mm, 59 holes; unplated 3.2 mm, 4 M3 holes). Order settings
as in the [flight README](../flight-pcb/README.md#ordering-from-a-pcb-maker),
with **size 100 x 80 mm** (inside the cheapest 100 x 100 mm tier at JLCPCB
and similar). A made board has plated holes and solder mask: solder each pin
once, on the bottom. Its top copper has only the pads (no tracks).

## Checks

`python tools/pcb_export.py --board ground` (from the repository root) runs:

- **ERC** on the schematic.
- **DRC** with schematic parity (silkscreen and solder-mask rules off, as
  for the flight board).
- **`tools/pcb_check.py`** (profile `rocket-fc-ground`): every joint can be
  soldered, tracks meet pins only where those are soldered, no copper under
  the M3 screw heads, and the connections hold with only the copper that is
  soldered (the tracks plus the bottom pour connect every net).
  It also lists the tallest parts for the box depth.

Heights (estimates, measure before choosing the box):

| Part | Height above the board |
|---|---|
| XIAO + Wio-SX1262 on female headers, U.FL plugs | about 22 mm |
| C1 1000 uF standing | about 21 mm |
| Q1 standing | about 18.5 mm |
| Screw terminals | 10 mm |

## Board constraints

- 100 x 80 mm, room to spare (the right third is free); four M3 holes
  4 mm in from the edges.
- Off the board: 18650 holder (J2), power switch (J4), ARM switch (J3),
  igniter leads (J1), button (J6), MODE switch (J7). The XIAO plugs in.
- Made at home: laser-exposed resist, etching, no plated holes. All copper
  on the bottom face (single-sided blank).
- Design rules (project settings): clearance 0.4 mm between nets (0.3 mm
  minimum), tracks 0.6 mm minimum; net classes **Default** 0.8 mm,
  **Power** (BAT_SW, GND) 1.2 mm, **Pyro** (CELL+, ARM, IGN+, IGN-) 2.0 mm
  (1.5 mm at Q1's legs); vias as 0.8 mm holes with 2.0 mm pads (none used).
- **GND pour** on the bottom: zone clearance 0.7 mm, minimum width 0.6 mm,
  thermal reliefs 0.6 mm, pieces under 1 mm2 dropped, keep-outs round the
  M3 holes (3.2 mm radius). Copper labels on the bottom (BOT, connector
  polarity) for a board without silkscreen.

## Workflow

1. Open `rocket-fc-ground.kicad_pro` in KiCad 10 and edit. After a schematic
   change, **Tools -> Update PCB from Schematic**; after moving anything on
   the board, refill the zones (key B) before saving.
2. Run `python tools/pcb_export.py --board ground` from the repository root:
   the checks above, `docs/schematics/ground-pcb.svg`, and in `out/` a
   printable schematic, 1:1 placement prints and 3D renders. Add `laser` or
   `fab` for the laser or order files.
3. If the wiring changed, update `include/board_pins.h`,
   `docs/HARDWARE.md` and `tools/schematics.py` (the hand-wired drawing) in
   the same commit.
