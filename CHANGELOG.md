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
