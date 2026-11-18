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
  MPU-9250 from SparkFun's board file, 6-pin GY-BMP280, L76K, capacitor lying
  flat); through-hole resistors.
- Docs: L76K pins corrected from Seeed's schematic (wake-up at the D0 position,
  reset at D10, RX at D6; the D2 position is unused). Barometer is the 6-pin
  GY-BMP280 board (CSB to 3V3, SDO to GND).
- Hardware: flight PCB placed, 29 x 90.1 mm (97.1 mm with two M3 ears at the
  USB end), parts on both faces with the tall ones on the centre line for the
  32 mm airframe. The XIAO's USB-C sits at the board edge in a notch between
  the ears. Battery and main switch share one 4-pin side-entry JST-XH (J1);
  ARM and igniter screw terminals are centred, one per face; the GPS module
  moves to a 3-wire lead (J5). The pyro resistor becomes a 1 ohm 2 W metal-oxide
  part (the 5 W cement one still works hand-wired). No status LED on the PCB.
  Not routed yet.
- Tools: `tools/pcb_check.py` checks what KiCad's DRC cannot on a board without
  plated holes (every joint can be soldered, with the assembly order) and that
  every part fits the tube; `tools/pcb_export.py` runs it and exports
  placement prints and 3D renders.
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
