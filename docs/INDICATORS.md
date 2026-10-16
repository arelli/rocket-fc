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
| Slow 50 % blink (1 Hz) | Battery low (on the pad or after landing; overrides the above) |

## Buzzer

| Sound | When | Meaning |
|---|---|---|
| Short tick every second | INIT | Countdown |
| 2 beeps | End of INIT | Self-test passed (check the console or telemetry for warnings) |
| N long beeps, repeated every 10 s | FAULT | Fault code N: 1 = barometer missing, 2 = pressure implausible, 3 = invalid flight config |
| 3 short beeps, every N s | PAD, ground station heard | Igniter continuity OK |
| 1 long beep, every N s | PAD, ground station heard | No continuity |
| Silent | PAD, no ground station (or N = 0) | |
| Silent | In flight | |
| 1 long beep, then digits, every 10 s | LANDED | Maximum altitude in metres: each digit is that many short beeps, 0 = one long beep. E.g. 152 m = ▪ · ▪▪▪▪▪ · ▪▪ |

Pad status beeps sound only while the rocket hears the ground station's
heartbeat (sent in reply to its telemetry, see docs/TELEMETRY.md). N is the
"pad beeps every … s" setting in the ground station UI (default 10 s, 0 =
silent). The countdown, self-test, fault code and landed altitude beacon do not
depend on the ground station. The LED always shows the pad state.
