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
| Airtime | telemetry 53.9 ms, event 25.7 ms, uplink heartbeat 23.2 ms | `fc::lora_airtime_us` |

Both kits must be built from the same `radio_config.h`. Outside the EU change
the frequency and duty limits to your local rules.

**Rates:** telemetry every 1 s on the pad (5.4 % duty), every 200 ms in flight
(27 %), every 5 s after landing (1.1 %). Events are sent before telemetry.
On the ground the rocket waits 150 ms after each transmission before the next,
listening for the ground station's heartbeat.

**Duty cycle:** `fc::DutyCycleLimiter` is a token bucket refilled at 8 % with
a 72 s burst allowance. Any one-hour window therefore contains at most
72 s + 8 % × 3600 s = 360 s of transmission, the 10 % limit, while the
flight itself can use the 5 Hz rate for about 6 minutes from a full bucket.
The ground station applies the same limiter to its heartbeats (at most about
one per second, 2.6 %).
When the budget is exhausted, telemetry is skipped (counted as "denied" in
`status`) and queued events wait.

## Frame format (version 3, little-endian)

Header, 10 bytes, common to both frame types:

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | magic = 0xA5 |
| 1 | u8 | version = 3 (2 added the attitude quaternion, 3 the ground link fields and the uplink frame) |
| 2 | u8 | type: 1 = telemetry, 2 = event (rocket → ground); 3 = uplink (ground → rocket) |
| 3 | u8 | flight state (0 INIT, 1 FAULT, 2 PAD, 3 BOOST, 4 COAST, 5 DESCENT, 6 LANDED) |
| 4 | u16 | sequence number (separate counters for radio and log; gaps on the radio = lost packets) |
| 6 | u32 | time since boot [ms] |

Every frame ends with a u16 CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)
over all preceding bytes.

### Telemetry frame (56 bytes)

Acceleration, rotation rate and magnetic field are in the **rocket frame**:
+Z points to the nose; X and Y follow from the IMU mounting
(`fc::to_rocket_frame`, set by `cfg::kImuUpAxis`).

| Offset | Type | Field | Unit |
|---|---|---|---|
| 10 | i32 | altitude above pad (filtered) | cm |
| 14 | i16 | vertical velocity (filtered) | cm/s |
| 16 | 3 × i16 | acceleration x, y, z (rocket frame) | mg |
| 22 | 3 × i16 | rotation rate x, y, z (rocket frame) | 0.1 °/s |
| 28 | 3 × i16 | magnetic field x, y, z (rocket frame) | 0.1 µT |
| 34 | u32 | pressure | Pa |
| 38 | i16 | barometer temperature | 0.01 °C |
| 40 | u16 | battery voltage | mV |
| 42 | u8 | flags (below) | |
| 43 | 4 × i16 | attitude quaternion w, x, y, z | 1/32767 |
| 51 | u8 | link: bit 0 = ground station heartbeat heard within 5 s | |
| 52 | i8 | heartbeat RSSI at the rocket (−128 = none) | dBm |
| 53 | u8 | pad beep period in use (0 = silent) | s |
| 54 | u16 | CRC | |

The quaternion rotates rocket-frame vectors into the earth frame (x = magnetic
north, y = west, z = up). It comes from a complementary filter on the flight
computer (`fc/ahrs.h`): gyro integration at 100 Hz, corrected by the
accelerometer and magnetometer only when the rocket is not accelerating. It is
for display and analysis; no flight decision uses it. Without an IMU it is
(1, 0, 0, 0) and the IMU flag is clear.

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
| 17 | GROUND_LINK | 1 = rocket hears the ground station, 0 = lost it |

### Uplink frame (14 bytes, ground → rocket)

| Offset | Type | Field |
|---|---|---|
| 0–9 | | header; state byte = 0, time = ground station clock |
| 10 | u8 | kind = 1 (heartbeat) |
| 11 | u8 | pad beep period [s] (0 = silent) |
| 12 | u16 | CRC |

The ground station answers a rocket frame with a heartbeat 15 ms after
receiving it, at most once per 900 ms, and only while the rocket reports
INIT, FAULT, PAD or LANDED (`fc::UplinkPolicy`). The rocket counts the
ground station as heard for 5 s after each heartbeat (`fc::GroundLink`).
The heartbeat only sets the pad beep period: it cannot arm, fire or change
anything the flight logic uses, and the rocket ignores it in flight. It is not
authenticated, which is acceptable for that reason.

Ground station USB commands: `beep <0..255>` (seconds), `uplink on|off`, an
empty line re-prints the CSV headers. It reports `# uplink on beep=10 sent=N`.

## CSV

The ground station, the `csv on` console stream and the `dump` command all
print the same columns ([lib/fc_core/src/fc/csv.h](../lib/fc_core/src/fc/csv.h)):

```
T,seq,t_ms,state,alt_m,vel_mps,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,mx_uT,my_uT,mz_uT,p_pa,temp_c,batt_v,flags,qw,qx,qy,qz,link,up_rssi_dbm,beep_s
E,seq,t_ms,state,event,arg
```

The ground station appends `rssi_dbm,snr_db`. Lines starting with `#` are
comments; the ground station also prints `# noise <dBm>` (channel noise floor,
every 2 s) and `# stats: ...` (every 10 s).

## Onboard log

Until an SD card is fitted, every frame is also written to a 2 MB ring buffer
in the XIAO's PSRAM: telemetry at 50 Hz in flight and 1 Hz otherwise, plus all
events. That holds about 11 minutes of flight-rate data; on the pad and after
landing it lasts many hours before the oldest data is overwritten.

The log is lost when power is removed. After recovery: remove the arm link,
**leave the main switch on**, connect USB and run
`python tools/ground_station.py <port> --dump` (or type `dump` in the
console).
