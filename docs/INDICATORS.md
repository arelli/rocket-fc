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
| 3 short beeps every 10 s | PAD | Igniter continuity OK |
| 1 long beep every 10 s | PAD | No continuity |
| Silent | In flight | |
| 1 long beep, then digits, every 10 s | LANDED | Maximum altitude in metres: each digit is that many short beeps, 0 = one long beep. E.g. 152 m = ▪ · ▪▪▪▪▪ · ▪▪ |
