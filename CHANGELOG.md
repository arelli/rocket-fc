# Changelog

All notable changes are listed here. Versions follow
[semantic versioning](https://semver.org/); the telemetry format has its own
version number (`tlm::kVersion`, docs/TELEMETRY.md).

## Unreleased

- Hardware: KiCad 10 project for the flight computer (`hardware/flight-pcb`).
  Its schematic is now the wiring master and gives `docs/schematics/flight.svg`
  (`python tools/pcb_export.py`, which also runs the electrical rules check).
  Project-local symbol library for the XIAO kit, sensor breakouts and GPS;
  design rules for a home-etched two-layer board. The board itself is in
  progress. `tools/schematics.py` now draws the ground station only.
- Hardware: footprints for every part of the flight PCB (XIAO kit, SparkFun
  MPU-9250 from SparkFun's board file, 4-pin GY-BMP280, L76K); through-hole
  resistors.
- Docs: L76K pins as printed on the V1.1 module (WUP at the D0 position,
  RST at D2, RX at D6, TX at D7; Seeed's V1.0 schematic had reset at D10). Barometer is the 4-pin
  GY-BMP280 board ("GYBMEP", 10 x 13 mm). Battery capacitor 470 uF 10 V
  (8 mm): it rides through millisecond dips, not the 1 s pyro pulse.
- Hardware: flight PCB placed, 30 x 78.2 mm (85.2 mm with two M3 ears at the
  USB end), parts on both faces with the tall ones on the centre line for the
  32 mm airframe. Unused header pins (XIAO 5V/D8-D10, MPU AD0/INT/AUX) have
  no pads; net classes 1.0 / 0.6 / 0.4 mm with 0.4 mm clearance. Routed on
  both faces with 27 wire vias (0.8 mm hole, 1.6 mm pad): DRC clean. The XIAO's USB-C sits at the board edge in a notch between
  the ears. Battery and main switch share one 4-pin side-entry JST-XH (J1);
  ARM and igniter screw terminals are centred, one per face; the GPS module
  moves to a 3-wire lead (J5). The pyro resistor becomes a 1 ohm 2 W metal-oxide
  part (the 5 W cement one still works hand-wired). No status LED on the PCB.
  The 4-pin barometer mounts sensor side down (its pin order runs the other
  way to the footprint with the sensor up).
- Hardware: laser files for a home-made board (`python tools/pcb_export.py
  laser`): negative copper images (bottom mirrored) as SVG and as pure
  black-and-white PNG at 12.5 lines/mm (0.08 mm, exact in the file) for
  LaserGRBL: a horizontal Passthrough pass then a vertical Line To Line
  pass, which clears the paint between lines and the sawtooth edges,
  a registration jig using the M3 holes, drill file and report. Copper TOP /
  BOT labels show whether the bottom came out mirrored correctly.
- Hardware: copper fattened after the first test etch (thin tracks and pad
  rings were lost to laser over-burn): every track, via and pad grown in
  place into the free room, routing unchanged; tracks 0.56 mm and wider,
  clearance 0.3 mm.
- Hardware: placement map for assembly (`tools/pcb_map.py`, written by the
  laser step): parts on both faces over the lasered copper, wire vias and
  both-face pins marked. The build guide lists the PCB materials and points
  to the board's making and assembly steps; LaserGRBL settings documented.
- Hardware: GND pour on both faces of the flight PCB (0.7 mm from other
  nets, keep-outs under Q1's tab and the screw heads, checked by
  `tools/pcb_check.py`). The laser only clears a band around the copper and
  the etch removes far less of it.
- Hardware: vector isolation G-code for the laser (`tools/pcb_isolation.py`,
  in the laser step): outlines around the copper instead of a raster, about
  15 minutes a face instead of 70; checks its own result (design copper
  intact, no nets joined).
- Hardware: order files for a PCB maker (`python tools/pcb_export.py fab`):
  Gerbers for copper, mask, silkscreen and outline, Excellon drill files with
  plated and unplated holes apart, and one zip to upload; the laser jig layer
  stays out. The board README has the order settings.
- Tools: `tools/pcb_check.py` checks what KiCad's DRC cannot on a board without
  plated holes (every joint can be soldered, with the assembly order; tracks
  meet pins only on faces where they are soldered; no copper under the MOSFET
  tab or the screw heads) and that every part fits the tube; `tools/pcb_export.py` runs it and exports
  placement prints and 3D renders.
- Hardware: KiCad 10 project for the ground station (`hardware/ground-pcb`):
  100 x 80 mm, single-sided (tracks and a GND pour on the bottom face, no
  wire vias: one laser job, no jig), M3 holes in the corners. The XIAO kit
  plugs into two 1x7 female headers (its BAT pads on a JST-XH lead, J5); the
  18650, ARM switch and igniter on 5.08 mm screw terminals, the power switch,
  button and MODE switch on JST-XH connectors; LEDs, buzzer and launch
  channel on the board. Pins unchanged (`pins::ground`). Wide copper for the
  laser: 0.4 mm clearance, tracks 0.8 / 1.2 / 2.0 mm, big pads, copper
  labels 2.5-3 mm with a 0.5-0.6 mm stroke. ERC, DRC and
  schematic parity clean; laser files as for the flight board for its one
  face (isolation G-code, raster SVG/PNG, drill file, placement map) and
  order files.
- Tools: `tools/pcb_export.py --board ground` runs the checks and exports for
  the ground station board (the flight board stays the default; its
  schematic SVG is `docs/schematics/ground-pcb.svg`). `tools/pcb_check.py`
  picks a profile from the board file name (tube fit for the flight board,
  tallest parts for the ground one) and now also checks that the
  connections hold with only the copper that is soldered: a pour reaching
  pins soldered only on the other face is left out and DRC must still find
  every net connected. `tools/pcb_map.py` and `tools/pcb_isolation.py` take
  the board name; a single-sided board gets only its copper face (no top
  files, no jig), and its placement map shows the parts over the copper
  seen through the board.
- Hardware: parts sheet for both boards (`hardware/*-pcb/parts.png`, shown in
  their READMEs): the 3D renders with every part tagged and what goes into
  each screw terminal, and a legend with
  values, what each part is and the resistor colour codes. Drawn by
  `tools/pcb_parts.py` in the board step of `tools/pcb_export.py`.
- Hardware: flight PCB with soldered wires instead of plugs: the cell and
  main switch (J1, the JST-XH 4-pin pad layout without the plug) and the arm
  link (J3, two pads, one in each corner of the far end); the igniter keeps
  its screw terminal (J4). The board is 6.6 mm shorter, 30 x 71.6 mm (78.6 mm
  with the ears): J4 and the holes H3, H4 moved up into the room J3's
  terminal left, R3 turned end for end so the ARM and IGN+ tracks reach
  their new places, still 27 wire vias. DRC, parity and `pcb_check` clean;
  laser and fab files regenerated. `tools/pcb_check.py`: a wire pad's joint
  goes into the assembly order like a pin's; `tools/pcb_parts.py` can tag
  each pad of a part on its own.
- UI: zoom on every plot (drag, Shift+drag to pan, Ctrl+wheel, double-click to
  reset; linked time axes), hover readout, CSV export of each plot (range
  shown), of the events and of the GPS frames, plots in their own window.
- UI: every panel can be collapsed and moved into its own window.
- UI: flight videos in their own windows, played in step with the replay; each
  window sets its video time at T0 (LAUNCH), remembered per video file.
- UI: replay of a loaded log in real time (or faster/slower), between two
  times, with a "Flight" preset (LAUNCH − 5 s to LANDED + 5 s).

## 0.1.0 (first public version, bench-tested, not yet flown)

### Flight computer
- Flight logic: launch / burnout / apogee / landing state machine on a
  2-state Kalman filter (barometer + accelerometer); single-shot deployment
  with apogee lockout, confirmation and minimum altitude; optional
  deployment delay and backup timer.
- Pyro safety: hardware arm link, gate pull-down, driver-level launch
  interlock, one shot per flight (also across resets), 2 s hard pulse cap,
  battery sag measured during every pulse (PYRO_SAG).
- Resume after an in-flight processor reset (RTC-RAM flight record).
- Drivers: MPU-9250/9255/6500 + AK8963, BMP280/BME280, SX1262 (RadioLib),
  optional L76K GPS (receive only, on D7).
- Attitude estimate (display and log only). "Set upright" (`imu upright`)
  finds and saves the IMU nose axis for any mounting.
- Flight log in the XIAO's flash (4.8 MB "flog" partition, sessions per
  boot, written only when a flash stall is harmless) and in PSRAM.
- Battery monitoring on the XIAO pads: telemetry, low / critical warnings,
  deep-sleep cutoff on the ground.
- LED states and buzzer codes: countdown, self-test, fault code,
  continuity, landed altitude beacon.
- Bench mode (default at boot, ends at launch): simulated flight, test fire,
  remote ejection test on the pad, WiFi log download.
- Optional manual ejection by radio in flight (`manual_eject`, off by
  default).

### Radio and telemetry
- LoRa telemetry (EU 869.525 MHz, SF7 / BW250 by default) with CRC-16,
  duty-cycle limiter and event priority; GPS frames.
- Uplink: heartbeat (pad beep period, real time), bench mode, ping, remote
  test fire and manual ejection with one-time codes keyed by `rf_key`.
- Rocket console over the radio (text frames, keyed code); the rocket's
  WiFi password is sent sealed (X25519 key exchange, `fc/crypto.h`).

### Ground station
- WiFi access point (WPA2, random password at first boot) serving the
  complete UI: telemetry, 3D attitude, gauges, plots, events, GPS recovery,
  battery, both kits' settings and consoles, onboard log download.
- Launch controller: ARM switch, igniter continuity, countdown, abort,
  rocket-in-PAD interlock in FLIGHT mode.
- MODE switch (BENCH / FLIGHT) the rocket follows, push button (ping,
  search mode, launch), buzzer, ON and CONNECTED LEDs, 18650 battery
  monitoring. Every panel part is optional; every control is also on the
  page.
- `tools/ground_station.py`: USB recorder and onboard log download.

### Software
- Every tunable value is a setting (`FC_SETTINGS`): stored in flash,
  applied at boot, validated, identified by a CRC-32 logged with the
  firmware commit.
- Hardware-independent core (`lib/fc_core`) with 17 unit-test suites,
  including complete simulated flights; CI builds both firmwares.
- Both kits warn while `rf_key` is the public default.
