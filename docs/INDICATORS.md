# LED and buzzer

Patterns: [lib/fc_core/src/fc/indicators.cpp](../lib/fc_core/src/fc/indicators.cpp)
(LED) and [src/flight/app.cpp](../src/flight/app.cpp) (`pad_report`, `run_selftest`).

## Status LED (continuous state)

| Pattern | Meaning |
|---|---|
| Fast blink (10 Hz) | INIT: calibrating, keep still |
| 1 flash per second | PAD, **no** igniter continuity (arm link out or bridge open) |
| 2 quick flashes per second | PAD, igniter continuity OK: armed on the hardware side |
| 3 flashes, pause | FAULT: will not fly |
| Solid on | In flight (BOOST, COAST, DESCENT) |
| Short flash every 2 s | LANDED |
| Fast blink | Remote test fire or manual ejection armed (on the ground) |
| Slow 50 % blink (1 Hz) | Battery low (on the pad or after landing; overrides the above) |

Ground station panel LEDs and beeps: docs/HARDWARE.md, "Ground station".

## Buzzer

| Sound | When | Meaning |
|---|---|---|
| Short tick every second | INIT | Countdown |
| 2 beeps | End of INIT | Self-test passed (check the console or telemetry for warnings) |
| N long beeps, repeated every 10 s | FAULT | Fault code N: 1 = barometer missing, 2 = pressure implausible, 3 = invalid settings, 4 = IMU missing, 5 = radio failed (4 and 5 only if required by the settings), 6 = battery below `st_min_batt_v`, 7 = no GPS data with `gps_mode` = required |
| 3 short beeps, every N s | PAD, ground station heard | Igniter continuity OK |
| 1 long beep, every N s | PAD, ground station heard | No continuity |
| Silent | PAD, no ground station (or N = 0) | |
| Continuous beeping for 10 s | PAD (bench mode) | Remote test fire ARMED: stay clear (sounds even with the buzzer switched off) |
| 1 short beep (150 ms) | On the ground | Ping from the ground station received (it beeps 0.5 s after the answer reaches it) |
| Rapid beeping for 10 s | On the ground | Manual ejection ARMED: stay clear (always sounds) |
| Silent | In flight | |
| 1 long beep, then digits, every 10 s | LANDED | Maximum altitude in metres: each digit is that many short beeps, 0 = one long beep. E.g. 152 m = ▪ · ▪▪▪▪▪ · ▪▪ |

Pad status beeps sound only while the rocket hears the ground station's
heartbeat (sent in reply to its telemetry, see docs/TELEMETRY.md). N is the
"pad beeps every … s" setting in the ground station UI (default 10 s, 0 =
silent). The countdown, self-test, fault code and landed altitude beacon do not
depend on the ground station. The LED always shows the pad state.

**Settings** (group "buzzer" and "led", docs/SETTINGS.md): buzzer master
switch, countdown and self-test beeps on/off, pad beeps (with the ground
station / always / never) and their period, short and long beep lengths,
the fault/landed report period, the landed altitude beacon on/off; LED master
switch, brightness, solid-on in flight, low-battery pattern. The table above
shows the defaults.
