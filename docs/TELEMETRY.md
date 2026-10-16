# Telemetry, radio and logs

One binary frame format is used for the radio link and the onboard log, so a
single decoder ([lib/fc_core/src/fc/telemetry.cpp](../lib/fc_core/src/fc/telemetry.cpp))
reads both. Keep this document and `telemetry.h` in sync; any incompatible
change must increment `kVersion`.

## Radio

| Parameter | Value | Where |
|---|---|---|
| Frequency | 869.525 MHz (EU 869.40–869.65 MHz band: 500 mW ERP, 10 % duty cycle) | `include/radio_config.h` |
| Modulation | LoRa SF7, BW 250 kHz, CR 4/5, 8-symbol preamble, explicit header, CRC on | |
| Sync word | 0x12 (private; not LoRaWAN, not Meshtastic) | |
| Power | 20 dBm | |
| Airtime | telemetry frame 46.2 ms, event frame 25.7 ms | `fc::lora_airtime_us` |

Both kits must be built from the same `radio_config.h`. Outside the EU change
the frequency and duty limits to your local rules.

**Rates:** telemetry every 1 s on the pad (4.6 % duty), every 200 ms in flight
(23 %), every 5 s after landing (0.9 %). Events are sent before telemetry.

**Duty cycle:** `fc::DutyCycleLimiter` is a token bucket refilled at 8 % with
a 72 s burst allowance. Any one-hour window therefore contains at most
72 s + 8 % × 3600 s = 360 s of transmission, the 10 % limit, while the
flight itself can use the 5 Hz rate for about 8 minutes from a full bucket.
When the budget is exhausted, telemetry is skipped (counted as "denied" in
`status`) and queued events wait.

## Frame format (version 1, little-endian)

Header, 10 bytes, common to both frame types:

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | magic = 0xA5 |
| 1 | u8 | version = 1 |
| 2 | u8 | type: 1 = telemetry, 2 = event |
| 3 | u8 | flight state (0 INIT, 1 FAULT, 2 PAD, 3 BOOST, 4 COAST, 5 DESCENT, 6 LANDED) |
| 4 | u16 | sequence number (separate counters for radio and log; gaps on the radio = lost packets) |
| 6 | u32 | time since boot [ms] |

Every frame ends with a u16 CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)
over all preceding bytes.

### Telemetry frame (45 bytes)

| Offset | Type | Field | Unit |
|---|---|---|---|
| 10 | i32 | altitude above pad (filtered) | cm |
| 14 | i16 | vertical velocity (filtered) | cm/s |
| 16 | 3 × i16 | acceleration x, y, z (body) | mg |
| 22 | 3 × i16 | rotation rate x, y, z (body) | 0.1 °/s |
| 28 | 3 × i16 | magnetic field x, y, z (body) | 0.1 µT |
| 34 | u32 | pressure | Pa |
| 38 | i16 | barometer temperature | 0.01 °C |
| 40 | u16 | battery voltage | mV |
| 42 | u8 | flags (below) | |
| 43 | u16 | CRC | |

Values outside the field's range are saturated, not wrapped.

Flags: bit 0 igniter continuity, 1 pyro armed, 2 pyro fired, 3 IMU ok,
4 magnetometer ok, 5 barometer ok, 6 battery low, 7 flight resumed after a reset.

### Event frame (17 bytes)

| Offset | Type | Field |
|---|---|---|
| 10 | u8 | event code |
| 11 | i32 | argument |
| 15 | u16 | CRC |

| Code | Event | Argument |
|---|---|---|
| 1 | BOOT | firmware git hash (first 8 hex digits) |
| 2 | SELFTEST_OK | warning bits: 0 IMU missing, 1 no magnetometer, 2 radio failed, 3 battery low, 4 up axis not ~+1 g, 5 no backup timer |
| 3 | SELFTEST_FAIL | fault code (1 baro missing, 2 pressure implausible, 3 config invalid) |
| 4 | LAUNCH | altitude [cm] |
| 5 | BURNOUT | altitude [cm] |
| 6 | APOGEE | maximum altitude [cm] |
| 7 | PYRO_FIRE | altitude when fired [cm] |
| 8 | PYRO_BACKUP_FIRE | altitude when fired by the backup timer [cm] |
| 9 | LANDED | maximum altitude [cm] |
| 10 | BATT_LOW | battery [mV] |
| 11 | BATT_CRITICAL | battery [mV] |
| 12 | PYRO_OPEN | ms from fire to continuity loss (bridge burned through) |
| 13 | SENSOR_LOST | sensor bits: 0 IMU, 1 magnetometer, 2 barometer, 3 radio |
| 14 | RESUMED | ms since launch restored after a reset |
| 15 | APOGEE_NO_DEPLOY | maximum altitude [cm] (below the minimum deploy altitude) |
| 16 | BENCH_FIRE | pulse length [ms] (bench build only) |

## CSV

The ground station, the `csv on` console stream and the `dump` command all
print the same columns ([lib/fc_core/src/fc/csv.h](../lib/fc_core/src/fc/csv.h)):

```
T,seq,t_ms,state,alt_m,vel_mps,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,mx_uT,my_uT,mz_uT,p_pa,temp_c,batt_v,flags
E,seq,t_ms,state,event,arg
```

The ground station appends `rssi_dbm,snr_db`. Lines starting with `#` are
comments. `tools/ground_station.py` saves every line to `logs/`.

## Onboard log

Until an SD card is fitted, every frame is also written to a 2 MB ring buffer
in the XIAO's PSRAM: telemetry at 50 Hz in flight and 1 Hz otherwise, plus all
events. That holds about 14 minutes of flight-rate data; on the pad and after
landing it lasts many hours before the oldest data is overwritten.

The log is lost when power is removed. After recovery: remove the arm link,
**leave the main switch on**, connect USB and run
`python tools/ground_station.py <port> --dump` (or type `dump` in the
console).
