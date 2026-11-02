# Changelog

All notable changes are listed here. Versions follow
[semantic versioning](https://semver.org/); the telemetry format has its own
version number (`tlm::kVersion`, docs/TELEMETRY.md).

## Unreleased

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
