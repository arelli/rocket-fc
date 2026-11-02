# Contributing

Contributions are welcome: bug reports, flight reports, documentation,
hardware variants and code. This is flight software that fires pyrotechnic
charges, so changes are reviewed with that in mind. Please read this page
before opening a pull request.

## Development setup

1. [VS Code](https://code.visualstudio.com/) with the **PlatformIO IDE**
   extension, or the PlatformIO CLI (`pip install platformio`).
2. A C++17 compiler on your computer for the unit tests (Linux/macOS: the
   system `g++`/`clang++`; Windows: e.g. `scoop install gcc` or MSYS2).
3. Clone and run:

```
pio test -e native          # unit tests (no hardware needed)
pio run -e flight           # flight computer firmware
pio run -e ground           # ground station firmware (embeds the UI)
```

All three run in CI on every push and pull request
(`.github/workflows/ci.yml`). Every version (platform, framework, RadioLib)
is pinned in `platformio.ini`; do not unpin.

## Where things go

Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) first. The one rule:

> **Anything that decides something lives in `lib/fc_core` and has a unit test.**

| Directory | Contains | Arduino allowed |
|---|---|---|
| `lib/fc_core/src/fc/` | Flight logic, formats, settings, codes, panel logic | **No** (the native build fails if it is) |
| `lib/fc_hal/src/hal/` | Chip drivers (IMU, barometer, radio, pyro, ADC, flash) | Yes |
| `src/flight/`, `src/ground/` | Wiring of core + drivers for each kit | Yes |
| `include/` | Pins and fixed constants | Data only |
| `test/test_<topic>/` | One Unity suite per topic | No |
| `tools/` | Ground station UI, CLI recorder, schematic generator, build scripts | |

Every part beyond the XIAO and the radio is **optional** at run time: a
missing sensor, GPS, switch, buzzer or divider must leave the rest working
(self-test warnings or faults decide whether the rocket may fly). Keep it
that way: probe, report, and carry on.

## Common changes

| Change | Steps |
|---|---|
| **Add or change a setting** | One `X(...)` line in `FC_SETTINGS` (`lib/fc_core/src/fc/settings.h`); cross-checks in `validate()`; run the tests. The console and the UI pick it up by themselves. Flight-safety settings get the `S` flag. See [docs/SETTINGS.md](docs/SETTINGS.md). |
| **Add an event** | Append to `EventCode` (never renumber), name in `types.cpp`, row in [docs/TELEMETRY.md](docs/TELEMETRY.md). |
| **Change a telemetry frame** | Append fields, increment `tlm::kVersion`, update `docs/TELEMETRY.md`, the CSV format in `lib/fc_core/src/fc/csv.cpp`, `tools/ground_station.html` (`TLM_COLUMNS`) and `tools/ground_station.py` (`TLM_FIELDS`) in the same commit. |
| **Change wiring** | `include/board_pins.h`, [docs/HARDWARE.md](docs/HARDWARE.md) and `tools/schematics.py` together; regenerate the drawings with `python tools/schematics.py docs/schematics`. |
| **Change the UI** | Edit `tools/ground_station.html` (one file, no dependencies, no build step). It is embedded in the ground station firmware at build time: reflash the station to test it. |
| **Add a sensor or log store** | See "Extending" in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). |

## Flight-safety changes

A change is flight-safety relevant if it can change **when or whether a
charge fires**: the pyro or launch channel, `FlightStateMachine`,
`Estimator`, `RemoteFire`, `LaunchControl`, the resume logic, or any setting
flagged `S`. For those:

- A unit test that fails without the change and passes with it, plus the
  complete-flight tests in `test_flight` still passing.
- On hardware: a simulated flight (`sim live`, bench mode) with a dummy load
  or bare nichrome, as in [docs/TESTING.md](docs/TESTING.md).
- Explain the reasoning in the pull request. Small, separate commits.
- Never weaken an interlock (table in [docs/FLIGHT_LOGIC.md](docs/FLIGHT_LOGIC.md))
  as a side effect of another change.

## Style

- C++17, the style of the surrounding code: 2-space indent, 120 columns
  (`.clang-format`). Format only the lines you change; no whole-file
  reformatting in the same pull request as a functional change.
- No dynamic allocation and no blocking calls in the 100 Hz loop.
- Comments say *why*; names say *what*. Units in names (`_ms`, `_mps`, `_v`).
- Documentation in plain English, short sentences, units always given.

## Commits and pull requests

- One topic per pull request; describe what changed and why.
- Commit messages: a short imperative summary line (≤ 72 characters), a
  blank line, then details if needed.
- Update [CHANGELOG.md](CHANGELOG.md) under "Unreleased" for anything a user
  would notice.
- Fill in the pull request checklist.

## Reporting bugs and flights

Use the issue templates. Include the firmware commit and the settings CRC
from the boot banner (`status`), and logs where possible. Flight reports
(successful or not) with the onboard log and the exported settings file are
especially useful.

By contributing you agree that your contribution is licensed under the
project's [MIT License](LICENSE).
