# Flight logic

The only code that decides to fire the charge is
[lib/fc_core/src/fc/flight_sm.cpp](../lib/fc_core/src/fc/flight_sm.cpp),
fed by the estimator in
[estimator.cpp](../lib/fc_core/src/fc/estimator.cpp). Both are hardware
independent and exercised by simulated flights in
[test/test_flight](../test/test_flight/test_main.cpp). All numbers below are
the defaults in [include/flight_config.h](../include/flight_config.h).

## States

```
 INIT ──self-test ok──► PAD ──launch──► BOOST ──burnout──► COAST ──apogee──► DESCENT ──still 5 s──► LANDED
   │                                      └──────────── apogee / backup timer ──┘
   └──self-test failed──► FAULT (final: never arms, never fires)
```

| State | Meaning | Pyro |
|---|---|---|
| INIT | 5 s countdown after power-on. Keep the rocket still: the last 2 s average the ground pressure, gyro bias and the "up" axis. | Disarmed |
| FAULT | Self-test failed (no barometer, implausible pressure, invalid config). Reboot to retry. | Disarmed |
| PAD | Waiting for launch. Ground pressure slowly follows the weather (60 s time constant, only while the rocket is still). | Disarmed |
| BOOST | Launch detected, motor burning. | **Armed** |
| COAST | Burnout detected, waiting for apogee. | **Armed** |
| DESCENT | Charge fired (or apogee passed without firing). | Disarmed |
| LANDED | Altitude steady. Recovery beacon. | Disarmed |

## Estimation

A two-state Kalman filter (altitude, vertical velocity). Prediction uses the
accelerometer component along the rocket axis minus 1 g; correction uses the
barometric altitude above the pad. If the IMU fails, the filter falls back to
a barometer-only model with a larger process noise. The estimate does not
rotate the accelerometer into the vertical: it assumes a near-vertical boost,
which is what deployment needs (falling velocity near apogee comes from the
barometer).

## Decisions

**Launch** (PAD → BOOST), either of:
- axial acceleration ≥ 2.5 g for 100 ms (launch time is back-dated to the start); or
- barometric altitude ≥ 30 m for 500 ms (fallback if the IMU is dead).

A knock, a dropped rocket or a wind gust does not satisfy either (tested).

**Burnout** (BOOST → COAST, informational): axial acceleration < 0.5 g for
100 ms (or, without IMU, velocity 2 m/s below its peak).

**Apogee deployment** (COAST/BOOST → DESCENT), all of:
1. at least 1500 ms since launch (lockout: no decision during boost, when
   the barometer is disturbed by transonic and pressure effects);
2. barometer working;
3. filtered velocity ≤ 0 **and** altitude at least 0.5 m below the maximum,
   both held continuously for 100 ms;
4. maximum altitude ≥ 10 m (otherwise APOGEE_NO_DEPLOY: a failed launch
   that hops is not worth a charge going off near people).

Typical timing in simulation: 0.3–0.5 s after true apogee.

**Backup timer** (optional, off by default): fires `kBackupDeployMs` after
launch if apogee was not detected (e.g. barometer failed). Set it from your
simulation (OpenRocket apogee time + 1–2 s). Firing too early means
deployment at speed; 0 disables it. Self-test notes when it is off.

**Landing** (DESCENT → LANDED): altitude stays within ±2 m for 5 s.
Also from COAST if the rocket came down without an apogee decision.

## Pyro safety layers

| # | Layer | Where |
|---|---|---|
| 1 | Physical arm link: igniter circuit open until fitted on the pad | Hardware |
| 2 | 100k gate pull-down: MOSFET off while the processor boots, resets or sleeps | Hardware |
| 3 | Gate driven low first thing in `setup()`, re-asserted every loop | `PyroChannel` |
| 4 | Software arming only in BOOST/COAST after launch detection; FAULT never arms | `FlightStateMachine::pyro_armed` |
| 5 | Driver refuses any fire command before launch detection | `App::control_tick`, `PyroChannel::fire` |
| 6 | One shot per flight, also across processor resets | State machine + RTC record |
| 7 | Pulse length 1000 ms, hard cap 2000 ms in the driver | `PyroChannel` |
| 8 | Apogee lockout, confirmation time, minimum altitude | State machine |

The `bench` build adds a test-fire command (`fire <ms> CONFIRM`). The
`flight` build does not contain that code at all.

## Reset in flight

If the processor resets during flight (watchdog after a 2 s stall, crash,
brown-out), it must not recalibrate "ground" at its current altitude. Every
10 ms the firmware writes a checksummed record to RTC memory (survives resets,
not power loss): state, fired flag, ground pressure, maximum altitude, time
since launch. On boot it resumes the flight only if all of these hold:
- the reset cause was software, watchdog, panic or brown-out (not power-on or
  the reset button);
- the record's checksum is valid;
- the stored state was BOOST, COAST, DESCENT or LANDED.

On the pad a fresh start is always the safer choice. A resumed flight sends
RESUMED and sets the RESUMED flag in every frame. If the charge had already
fired, it is never fired again.

## Failure behaviour

| Failure | Effect |
|---|---|
| Barometer missing at boot | FAULT (no flight) |
| Barometer dies in flight | No apogee deployment; backup timer if set; SENSOR_LOST event |
| IMU missing / dies | Launch by barometer (30 m), deployment unaffected; warning / SENSOR_LOST |
| Magnetometer missing | Warning only (data not used for decisions) |
| Radio fails | Warning; flight unaffected, onboard log continues |
| Battery low | Warning; LED pattern on pad |
| Processor reset in flight | Resumes from RTC record (above) |
| Ejection charge does not light | No retry (single shot); PYRO_OPEN event absent tells you the bridge did not burn through |
