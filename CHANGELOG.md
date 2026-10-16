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
