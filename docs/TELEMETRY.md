# Telemetry, radio and logs

One binary frame format is used for the radio link and the onboard log, so a
single decoder ([lib/fc_core/src/fc/telemetry.cpp](../lib/fc_core/src/fc/telemetry.cpp))
reads both. Keep this document and `telemetry.h` in sync; any incompatible
change must increment `kVersion`.

## Radio

| Parameter | Value | Where |
|---|---|---|
| Frequency | 869.525 MHz (EU 869.40–869.65 MHz band: 500 mW ERP, 10 % duty cycle) | setting `radio_freq_mhz` |
| Modulation | LoRa SF7, BW 250 kHz, CR 4/5, 8-symbol preamble, explicit header, CRC on | `radio_sf`, `radio_bw`, `radio_cr`, `radio_preamble` |
| Sync word | 0x12 (private; not LoRaWAN, not Meshtastic) | `radio_sync` |
| Power | 20 dBm | `radio_power_dbm` |
| Airtime | telemetry 53.9 ms, event 25.7 ms, uplink frame 28.3 ms | `fc::lora_airtime_us` |

These are the default settings. Both kits must use the same radio settings
(the UI sets both; docs/SETTINGS.md). Leaving the EU band or its duty-cycle
limit needs `radio_unlock`.

**Rates:** telemetry every 1 s on the pad (5.4 % duty), every 200 ms in flight
(27 %), every 5 s after landing (1.1 %). Events are sent before telemetry.
On the ground the rocket waits 150 ms (`uplink_rx_ms`, longer automatically
when slow radio settings make the reply longer) after each transmission before
the next, listening for the ground station's heartbeat.

**Duty cycle:** `fc::DutyCycleLimiter` is a token bucket refilled at 8 % with
a 72 s burst allowance. Any one-hour window therefore contains at most
72 s + 8 % × 3600 s = 360 s of transmission, the 10 % limit, while the
flight itself can use the 5 Hz rate for about 6 minutes from a full bucket.
The ground station applies the same limiter to its heartbeats (at most about
one per second, 3.1 %).
When the budget is exhausted, telemetry is skipped (counted as "denied" in
`status`) and queued events wait.

## Frame format (version 4, little-endian)

Header, 10 bytes, common to all frame types:

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | magic = 0xA5 |
| 1 | u8 | version = 4 (2: attitude quaternion; 3: ground link fields, uplink; 4: real time and remote fire in the uplink) |
| 2 | u8 | type: 1 = telemetry, 2 = event, 4 = GPS, 6 = console text (rocket → ground); 3 = uplink, 5 = console command (ground → rocket) |
| 3 | u8 | flight state (0 INIT, 1 FAULT, 2 PAD, 3 BOOST, 4 COAST, 5 DESCENT, 6 LANDED) |
| 4 | u16 | sequence number (separate counters for radio and log; gaps on the radio = lost packets) |
| 6 | u32 | time since boot [ms] |

Every frame ends with a u16 CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)
over all preceding bytes.

### Telemetry frame (56 bytes)

Acceleration, rotation rate and magnetic field are in the **rocket frame**:
+Z points to the nose; X and Y follow from the IMU mounting
(`fc::to_rocket_frame`, set by the `imu_up_axis` setting).

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
| 51 | u8 | link: bit 0 = ground station heartbeat heard within 5 s, bit 1 = real time known, bit 2 = computer on USB (board powered and charging from USB), bit 3 = bench mode on, bit 4 = GPS sending data, bit 5 = GPS fix, bit 6 = manual ejection by radio enabled (`manual_eject`) | |
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
| 2 | SELFTEST_OK | warning bits: 0 IMU missing, 1 no magnetometer, 2 radio failed, 3 battery low, 4 up axis not ~+1 g, 5 no backup timer, 6 no GPS data (gps_mode auto), 7 manual ejection enabled |
| 3 | SELFTEST_FAIL | fault code (1 baro missing, 2 pressure implausible, 3 settings invalid, 4 IMU missing, 5 radio failed, 6 battery below minimum, 7 no GPS data with gps_mode required) |
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
| 16 | BENCH_FIRE | pulse length [ms] (test pulse, bench mode) |
| 17 | GROUND_LINK | 1 = rocket hears the ground station, 0 = lost it |
| 18 | TIME_SYNC | Unix seconds (unsigned) at exactly this event's t_ms |
| 19 | REMOTE_ARMED | one-time nonce (remote test fire armed, bench mode) |
| 20 | REMOTE_DISARMED | reason (see Uplink) |
| 21 | REMOTE_REFUSED | reason (see Uplink) |
| 22 | CONFIG | CRC-32 of the settings in use (after BOOT) |
| 23 | PYRO_SAG | battery during a pyro pulse: `(before_mV << 16) \| min_mV` (unsigned) |
| 24 | BENCH_MODE | 1 = bench mode on, 0 = off, 2 = off because launch was detected |
| 25 | GPS | 0 = no GPS data (lost), 1 = data but no fix, 2 = position fix (on change) |
| 26 | PONG | ping id (answer to a ground station connectivity check) |
| 27 | EJECT_ARMED | one-time nonce (manual ejection armed) |
| 28 | MANUAL_EJECT | altitude [cm] when the charge was fired by the ground command |

### GPS frame (38 bytes, optional module)

Sent only while a GPS module sends data: every `gps_tlm_ms` (2 s) on the pad
and in flight, with every telemetry frame after landing (recovery); airtime
41 ms. Logged onboard at every new position report (about 1 Hz). The
position is the last known one: after the fix is lost it stays, with
`fix` = 0.

| Offset | Type | Field | Unit |
|---|---|---|---|
| 10 | u8 | fix quality (GGA: 0 none, 1 GPS, 2 DGPS, 6 estimated) | |
| 11 | u8 | satellites used | |
| 12 | u16 | HDOP | 0.01 |
| 14 | i32 | latitude | 1e-7 ° |
| 18 | i32 | longitude | 1e-7 ° |
| 22 | i32 | altitude above mean sea level | cm |
| 26 | u16 | ground speed | 0.1 m/s |
| 28 | u16 | course over ground | 0.01 ° |
| 30 | u16 | age of the fix when the frame was made (65535 = older) | ms |
| 32 | u32 | time of the fix (0 = unknown) | Unix s |
| 36 | u16 | CRC | |

### Uplink frames (22 bytes, ground → rocket)

All uplink frames have the same length. Header state byte = 0, header time =
ground station clock.

| Offset | Type | Heartbeat (kind 1) | Commands (kinds 2–8) |
|---|---|---|---|
| 10 | u8 | kind = 1 | 2 = remote fire arm, 3 = fire, 4 = disarm, 5 = bench mode, 6 = ping, 7 = manual eject arm, 8 = manual eject |
| 11 | | u8 pad beep period [s] (0 = silent) | u32 code (kinds 3, 5, 8) |
| 12 | | u64 real time when sent [Unix ms] (0 = unknown) | |
| 15 | | | u16 pulse length [ms] (kind 5: 1 = on, 0 = off; kind 6: ping id; kinds 7–8: 0 = the rocket's `pyro_pulse_ms`) |
| 17 | | | 3 bytes zero |
| 20 | u16 | CRC | CRC |

**Heartbeat.** The ground station answers a rocket frame with a heartbeat
15 ms after receiving it, at most once per 900 ms, and only while the rocket
reports INIT, FAULT, PAD or LANDED (`fc::UplinkPolicy`). The rocket counts the
ground station as heard for 5 s after each heartbeat (`fc::GroundLink`). The
heartbeat sets the pad beep period and gives the rocket real time; the rocket
ignores it in flight.

**Real time.** The UI sends the laptop clock to the ground kit (`time <ms>`),
and to the flight computer when it is on USB. The first time the rocket
receives in a boot (radio airtime compensated, a few ms accuracy) is logged as
a TIME_SYNC event: at that event's `t_ms` it was exactly `arg` Unix seconds,
so for every frame of that boot `utc_ms = arg × 1000 + (t_ms − event t_ms)`.
Later times in the same boot are ignored, so timestamps never jump. The `dump`
header also states it (`# clock utc0_ms N`: `utc_ms = N + t_ms`). Telemetry
link bit 1 shows whether the rocket has real time.

**Bench mode by radio (kind 5).** `code = fc::bench_mode_code(on, rf_key)`.
The rocket accepts it on the ground only (never in flight), answers
BENCH_MODE(1 or 0), or REMOTE_REFUSED(6) for a wrong code.

**Remote test fire (bench mode only).** Kept in the rocket firmware; the
ground station now uses the manual ejection command (kinds 7/8) instead, which
gives the same test pulse on the pad in bench mode. Protocol:

1. ground → arm(pulse). The rocket checks: bench mode, PAD state, igniter
   continuity, output idle. If all hold, it picks a random 32-bit nonce, sends
   REMOTE_ARMED(nonce), beeps continuously and blinks fast for 10 s;
   otherwise REMOTE_REFUSED(reason).
2. ground → fire(code, pulse) with `code = fc::remote_fire_code(nonce, key)`,
   `key` = the `rf_key` setting (same on both kits), within 10 s. The rocket checks the
   conditions again, the code and the pulse length, then pulses the output once
   (BENCH_FIRE event) or refuses. The nonce is used at most once.
3. Timeout, disarm command, or a lost condition (arm link pulled, state
   change) ends the arming (REMOTE_DISARMED(reason)).

In flight mode the rocket answers REMOTE_REFUSED(1).
The code is a keyed hash, not cryptography: it stops accidental firing and
other rocket-fc ground stations with another key. The physical arm link
remains the real safety. Set `rf_key` to your own number (on both kits).

Refusal / disarm reasons: 0 fired, 1 not in bench mode, 2 not in PAD, 3 no
continuity, 4 bad pulse length, 5 not armed, 6 wrong code, 7 window expired,
8 cancelled, 9 output busy, 10 manual ejection not enabled (`manual_eject`),
11 charge already fired, 12 no launch detected.

**Ping (kind 6, connectivity check).** The rocket answers PONG(id) with its
next transmission (events go first, about 0.1 s) and beeps once (not in
flight). The ground station prints `# ping reply id=N rtt=<ms> rssi=<dBm>
snr=<dB>` and beeps 0.5 s after the answer, or `# ping no reply id=N (why)`
and three short beeps after 3 s. In flight the rocket answers only when it
listens (manual ejection enabled).

**Manual ejection (kinds 7, 8).** Same two-step scheme and code as the remote
test fire, with its own nonce (EJECT_ARMED). The ground station sends kind 8
automatically in reply to the EJECT_ARMED frame. The rocket accepts:

- after launch detection (BOOST, COAST, DESCENT, LANDED): only with
  `manual_eject` = 1 (or in a simulated flight in bench mode), charge not
  fired yet. No altitude, apogee or continuity check. The pyro output fires
  once; the flight logic then never fires (it still logs APOGEE and LANDED,
  also after a reset);
- on the pad: only in bench mode with continuity, as a test pulse (like the
  remote test fire, does not use the flight shot);
- otherwise REMOTE_REFUSED(reason). Disarms after 10 s (REMOTE_DISARMED).

With `manual_eject` = 1 the rocket keeps its listen window after every
transmission in flight too (fewer telemetry frames per second).

Ground station commands (USB or WiFi): `launch`, `abort`, `ping`,
`search on|off`, `eject [ms]`, `eject cancel`, `r <rocket command>`, `rdefs`,
`rwifi` (the rocket joins the station's WiFi for the log download),
`beep <0..255>`, `uplink on|off`, `time <unix ms>`, `param ...` (radio and
station settings), `wifi ...`, `reboot`; an empty line re-prints the CSV
headers. Status: `# station mode=flight|bench arm=0|1 cont=0|1 ready=0|1
launch=idle|countdown|firing left=<s> search=0|1 vbat=<V> pct=<%> low=0|1`
(on change, every 1 s while armed, else every 5 s), `# launch ...`,
`# ping ...`, `# eject ...`, `# mode ...`. The rocket's bench mode follows the
station's MODE switch (bench mode uplink, on the ground).

Over WiFi (src/ground/web.h): `GET /events` streams every output line as
Server-Sent Events (`data: <line>`), `POST /cmd` takes command lines, `GET /`
serves the UI, `GET /rocket/<path>` relays to the rocket's log server.

### Console text frames (variable length, never logged)

The rocket's console works over the radio: the ground station command
`r <command>` sends a TextCmd; the rocket runs it as if typed on USB and
returns the output in TextOut frames (sent after events, before telemetry).
The station prints each output line as `RC,<line>`.

| Offset | TextCmd (type 5, ground → rocket) | TextOut (type 6, rocket → ground) |
|---|---|---|
| 10 | u32 code = `fc::text_code(seq, crc16(text), rf_key)` | u8 n (≤ 200) |
| 14 / 11 | u8 n (≤ 120), then n bytes of text | n bytes of text (lines end in `\n`) |
| end | u16 CRC | u16 CRC |

- Accepted only on the ground (never in flight), with the right code; a
  repeated sequence number is ignored. Settings print compactly
  (`V,<key>,<value>,<running>`, only non-default values for `param list`);
  the station supplies the definitions (`rdefs` → `D,` lines, same fields as
  `P,`). Refused by radio: `fire` (use the ejection command), `dump`,
  `flog dump`, `csv` (too long: use the WiFi log download).
- On the ground the rocket listens up to `15 ms + airtime(longest command) +
  60 ms` after each transmission.

## CSV

The ground station, the `csv on` console stream and the `dump` command all
print the same columns ([lib/fc_core/src/fc/csv.h](../lib/fc_core/src/fc/csv.h)):

```
T,seq,t_ms,state,alt_m,vel_mps,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,mx_uT,my_uT,mz_uT,p_pa,temp_c,batt_v,flags,qw,qx,qy,qz,link,up_rssi_dbm,beep_s
E,seq,t_ms,state,event,arg
G,seq,t_ms,state,fix,sats,hdop,lat_deg,lon_deg,alt_msl_m,speed_mps,course_deg,age_ms,utc_s
```

The ground station appends `rssi_dbm,snr_db`. Lines starting with `#` are
comments; the ground station also prints `# noise <dBm>` (channel noise floor,
every 2 s) and `# stats: ...` (every 10 s).

## Onboard log

Every frame (telemetry at 50 Hz in flight and 1 Hz otherwise, all events, and
with the optional GPS one GPS frame per position report) goes to two logs:

- **Flash log** (setting `flog_on`, default on): a 4.8 MB partition of the
  XIAO's own flash, **kept after power-off**. Each boot is a session; when
  full, the oldest sessions are overwritten. About 28 minutes of 50 Hz
  flight data, or about a day at the pad rate. Writing flash stalls the
  processor, so it never happens between launch and deployment: the flight
  is queued in RAM (2.7 minutes at 50 Hz) and written under the parachute,
  within a few seconds. On the pad, sectors are erased in advance.
  Format: [lib/fc_core/src/fc/flash_log.h](../lib/fc_core/src/fc/flash_log.h).
- **RAM log** (2 MB PSRAM, this boot only, lost at power-off): `dump`.

After recovery: remove the arm link. On the WiFi page, Logs: **Rocket WiFi
on** (the rocket joins the station's WiFi), **List onboard logs**, and save
the flight session (usually the one before the current boot). The rocket
serves `GET /flog` (sessions), `/flog?s=N` (CSV) and `/ram`; the station
relays them at `/rocket/...`. Or connect USB and
type `flog list` / `flog dump <session>` in the console. `flog erase CONFIRM`
clears the flash log.

## Settings protocol (USB console, both kits)

`param list` prints one line per setting, then a summary; the UI builds its
Settings panel from them (format in
[fc/param_shell.h](../lib/fc_core/src/fc/param_shell.h)):

```
P,<key>,<group>,<type>,<value>,<running>,<default>,<min>,<max>,<unit>,<flags>,<options>,<help>
PE,<count>,<edited crc>,<running crc>,<saved crc>,<ok | invalid: reason>
PERR,<key or *>,<reason>
PSAVED,<crc>,<number changed from the defaults>
```
