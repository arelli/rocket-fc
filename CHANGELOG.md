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
