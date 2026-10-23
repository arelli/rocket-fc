# Changelog

## Unreleased

First version: not yet flown.

- Flight logic: launch / burnout / apogee / landing state machine, 2-state
  Kalman filter, single-shot deployment with lockout, confirmation and
  minimum altitude, optional backup timer.
- Pyro: hardware arm link, gate pull-down, driver-level launch interlock,
  single shot, 2 s hard pulse cap.
- Resume after an in-flight processor reset (RTC-RAM flight record).
- MPU-9250/9255/6500 + AK8963, BMP280/BME280 drivers.
- LoRa telemetry (EU 869.525 MHz, SF7/BW250), binary frames with CRC,
  10 % duty-cycle limiter, event priority.
- Onboard 2 MB PSRAM flight log; `dump` over USB; `LogSink` interface for SD.
- Battery on the XIAO pads: voltage telemetry, low/critical warnings,
  deep-sleep cutoff on the ground.
- LED states and buzzer codes (countdown, self-test, fault code,
  continuity, altitude beacon).
- USB console; bench build with simulated flight and test fire.
- Ground station firmware and `tools/ground_station.py`.
- Native unit tests.
- Attitude estimation (display/log only) and 3D view; ground station UI.
- Ground station heartbeat: pad beeps only while heard, period set from the UI.
- Real time: the rocket's clock is set from the laptop via the ground station
  (TIME_SYNC event), so logs carry real timestamps.
- Remote test fire by radio for ejection charge ground tests (bench build only,
  two-step arm/fire with one-time code, PAD + continuity required).
- UI: warning when heartbeats are off (remote ARM needs them); battery panel
  with charging indicator (USB-host flag in telemetry + voltage trend) and
  voltage graph.
- Settings: every tunable value (thresholds, deploy delay, backup timer, pulse
  length, self-test checks, filters, battery, telemetry rates, buzzer, LED,
  radio) is a setting stored in flash, edited from the UI's Settings panel or
  with `param`, applied at boot, identified by a CRC (CONFIG event). Radio
  settings are kept equal on both kits; leaving the EU band needs
  `radio_unlock`. Optional self-test faults 4-6.
- Deployment delay after apogee (`deploy_delay_ms`), committed and resumed
  across resets.
- One firmware: the bench build is replaced by bench mode (RAM only, off at
  every boot and at launch), switched over USB or by radio; UI banner.
- Battery voltage sag measured during every pyro pulse (PYRO_SAG event).
- Configurable IMU low-pass, barometer filter/oversampling, SX1262 RX boost,
  LED brightness.
- Optional GPS (L76K on D7, receive only): NMEA parser, GPS frames on the
  radio and in the log, link bits, GPS event, self-test warning or fault
  (gps_mode), clock from GPS time; UI GPS and recovery panel with distance
  and bearing from the pad and a track plot.
- Persistent flight log in the XIAO's flash (4.8 MB "flog" partition, new
  partition table; settings kept): sessions per boot, written only when a
  flash stall is harmless (never between launch and deployment), sectors
  erased ahead on the pad. Console `flog`; UI session list and download.
  Replaces the SD card plan (D7 is the GPS input).
- Ground station WiFi access point (WPA2, random password on first boot):
  serves the UI (embedded at build time) at http://192.168.4.1 /
  rocketfc.local with a captive portal; Server-Sent Events + POST bridge the
  same line protocol as USB, in a separate task so the radio loop never
  waits. UI auto-detects it; WiFi settings box. USB keeps working alongside.
- UI: GPS indicator next to the sensor flags; GPS, Battery, Remote test fire
  and Settings panels collapsible (collapsed at start) and can open in their
  own window; Google satellite map of the last GPS fix (panel or window).
- Ground station panel (optional): push button (short = ping, 1.5 s hold =
  search mode, 3 s hold = manual ejection when enabled for 60 s), buzzer,
  ON and "rocket connected" LEDs, battery voltage/percentage on D0 (same
  divider as the flight kit). `ping`, `search`, `eject` commands; `# panel`,
  `# battery`, `# ping`, `# eject` status lines. Station settings `gs_*`
  (flag G, ground station only).
- Connectivity check: uplink kind 6 (ping) → PONG event; the rocket beeps,
  the station beeps 0.5 s after the answer.
- Manual parachute ejection by radio (uplink kinds 7/8, EJECT_ARMED and
  MANUAL_EJECT events, refusal reasons 10-12): after launch only with the
  new flight-safety setting `manual_eject` (link bit 6, self-test warning
  bit 7, boot warning); on the pad only in bench mode as a test pulse. The
  flight logic never fires after a manual ejection (tested, also across a
  reset). UI: Manual ejection panel, MANUAL EJECT pill, ground station box
  (battery, ping, search mode).
- Buzzers (both kits) wired straight to their pin, as tested on the flight
  kit: no MOSFET; the firmware sets the pin's strongest drive (~40 mA).
- Ground station as launch controller: 18650 power, ARM switch in the
  igniter circuit, launch MOSFET channel with ARM/continuity sensing on one
  ADC pin, countdown (gs_launch_countdown_s) and pulse (gs_launch_pulse_ms),
  rocket-in-PAD interlock in FLIGHT mode (gs_launch_need_pad). Tested logic:
  fc/launch.h. MODE switch (FLIGHT/BENCH): the rocket's bench mode follows
  it. Ground pins changed: CONNECTED LED D4 → D6; D4 launch sense, D5 launch
  gate, D7 MODE. Station button ejection removed (page only).
- Rocket console over the radio (TextCmd/TextOut frames, keyed code): the
  station's `r <command>`; compact settings (V lines) with definitions from
  the station (`rdefs`). Rocket settings are edited from the WiFi page.
- Rocket WiFi client for log download (`wifi join`, only landed/FAULT/bench),
  relayed by the station at /rocket/...
- New WiFi-only UI (much simpler): launch, flight, recovery, events,
  settings, logs, console. The local file / USB viewer is gone.
- Rocket WiFi join: the station's password is sent encrypted (fresh X25519
  key exchange per request, key bound to rf_key, tagged; fc/crypto.h with
  RFC 7748 / FIPS 180 test vectors). Text commands up to 200 characters.
- UI: confirmation again before enabling manual_eject (and radio_unlock).
