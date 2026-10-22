# rocket-fc

Flight computer for a model rocket: apogee detection and parachute
deployment, live LoRa telemetry and an onboard flight log.

| Part | Use |
|---|---|
| Seeed XIAO ESP32-S3 + Wio-SX1262 kit | Processor and LoRa radio (flight) |
| Second XIAO ESP32-S3 + Wio-SX1262 kit | Ground station on your computer's USB |
| MPU-9250 | Accelerometer ±16 g, gyroscope ±2000 °/s, magnetometer |
| BMP280 | Barometric altitude, used for apogee |
| IRLB8748 MOSFET | Pyro (nichrome igniter) driver |
| Status LED, active buzzer | Continuous state and audible signals |
| 1S Li-ion cell (18350) | Soldered to the XIAO battery pads, charged over the XIAO's USB-C |
| L76K GPS (optional) | Recovery position, on D7; works with or without it |
| (no SD card) | Flights are logged to the XIAO's own flash, kept after power-off |

> **Safety.** This drives a pyrotechnic charge. Read
> [docs/FLIGHT_LOGIC.md](docs/FLIGHT_LOGIC.md) and run the whole bench
> procedure in [docs/TESTING.md](docs/TESTING.md) with bare nichrome (no
> charge) before any flight. Fly under your club's / national rules.

## Quick start

1. Install [VS Code](https://code.visualstudio.com/) and the **PlatformIO IDE** extension.
2. Open this folder in VS Code.
3. Flight computer: connect it by USB, then run `pio run -e flight -t upload`
   (one firmware for bench tests and flight; test commands need bench mode,
   see [docs/SETTINGS.md](docs/SETTINGS.md)).
4. Ground station: connect the second kit, `pio run -e ground -t upload`.
5. Serial console: `pio device monitor` (115200 baud), type `help`.
6. Ground station window: open `tools/ground_station.html` in Chrome or Edge
   (double-click it; works offline). Connect the ground kit for live telemetry
   and the flight computer for its console. It also opens saved logs.
   Command-line alternative: `pip install pyserial`, then
   `python tools/ground_station.py COM5`.
   **Without a laptop:** power the ground station from a power bank, join its
   WiFi (`rocketfc-XXXX`; the password is printed by the `wifi` command over
   USB) and open http://192.168.4.1 on a phone. Same UI, radio features only
   (docs/HARDWARE.md, "Ground station WiFi").
7. Unit tests on your computer: `pio test -e native` (needs a C++ compiler:
   on Windows install one, e.g. `scoop install gcc` or MSYS2).

## Build environments

| Env | What |
|---|---|
| `flight` | Flight computer firmware. Test commands (`sim`, `fire`, remote test fire) work only in **bench mode**, switched on at run time from the UI or console and off at every boot and at launch. |
| `ground` | Ground station: receives telemetry, answers with heartbeats (pad beep period) |
| `native` | Unit tests of the flight logic on your PC |

## Repository layout

```
include/            board_pins.h (pins), flight_config.h, radio_config.h (fixed constants)
lib/fc_core/        hardware-independent logic (tested on the PC)
  fc/flight_sm      state machine: launch, apogee, deployment, landing
  fc/estimator      Kalman filter: altitude + velocity from baro + accel
  fc/telemetry      binary frame format (radio and log), CRC-16
  fc/settings       every tunable value: one table -> flash storage, "param" command, UI form
  fc/flash_log      persistent flight log in the XIAO's flash (sessions, ring of sectors)
  fc/gps            NMEA parser for the optional GPS
  fc/...            battery, radio duty cycle, LED/beep patterns, reset recovery, simulator
lib/fc_hal/         drivers: MPU-9250, BMP280, SX1262, GPS UART, flash partition, pyro channel, ADC, system
src/flight/         flight firmware (app.cpp = wiring of the above)
src/ground/         ground station firmware (main.cpp) and its WiFi access point / web UI server (web.cpp)
test/               unit tests (pio test -e native)
tools/              ground_station.html (UI), ground_station.py (CLI recorder), build_info.py
docs/               hardware, flight logic, telemetry, indicators, testing
```

## Documentation

- [docs/HARDWARE.md](docs/HARDWARE.md): wiring, pin map, battery, pyro circuit
- [docs/FLIGHT_LOGIC.md](docs/FLIGHT_LOGIC.md): states, deployment rules, failure handling
- [docs/INDICATORS.md](docs/INDICATORS.md): LED patterns and beep codes
- [docs/TELEMETRY.md](docs/TELEMETRY.md): radio settings, frame format, CSV columns
- [docs/SETTINGS.md](docs/SETTINGS.md): changing settings (UI / `param`), radio rules, bench mode
- [docs/TESTING.md](docs/TESTING.md): unit tests, bench tests, pre-flight checklist
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): code structure, how to extend
- [CHANGELOG.md](CHANGELOG.md)

## Traceability

Every build is stamped with its git commit (`FC_GIT_REV`, `-dirty` if there
were uncommitted changes). It is printed at boot and in `status`, and is the
argument of the first telemetry event (BOOT). The settings in use are
identified by their CRC-32 (boot message, `status`, CONFIG event). Fly only
clean builds and keep an exported settings file: a log then identifies exactly
which code and configuration flew.
