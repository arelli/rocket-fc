# rocket-fc

Flight computer for a model rocket: apogee detection and parachute
deployment, live LoRa telemetry and an onboard flight log.

| Part | Use |
|---|---|
| Seeed XIAO ESP32-S3 + Wio-SX1262 kit | Processor and LoRa radio (flight) |
| Second XIAO ESP32-S3 + Wio-SX1262 kit | Ground station on your computer's USB |
| MPU-9250 | Accelerometer ±16 g, gyroscope ±2000 °/s, magnetometer |
| BMP280 | Barometric altitude, used for apogee |
| IRLB8748 MOSFET ×2 | Pyro (nichrome igniter) and buzzer drivers |
| Status LED, active buzzer | Continuous state and audible signals |
| 1S Li-ion cell (18350) | Soldered to the XIAO battery pads, charged over the XIAO's USB-C |
| SD card (future) | Pin and software interface reserved |

> **Safety.** This drives a pyrotechnic charge. Read
> [docs/FLIGHT_LOGIC.md](docs/FLIGHT_LOGIC.md) and run the whole bench
> procedure in [docs/TESTING.md](docs/TESTING.md) with bare nichrome (no
> charge) before any flight. Fly under your club's / national rules.

## Quick start

1. Install [VS Code](https://code.visualstudio.com/) and the **PlatformIO IDE** extension.
2. Open this folder in VS Code.
3. Flight computer: connect it by USB, then run `pio run -e bench -t upload`
   (bench build, for testing) or `pio run -e flight -t upload` (for flight).
4. Ground station: connect the second kit, `pio run -e ground -t upload`.
5. Serial console: `pio device monitor` (115200 baud), type `help`.
6. Ground station window: open `tools/ground_station.html` in Chrome or Edge
   (double-click it; works offline). Connect the ground kit for live telemetry
   and the flight computer for its console. It also opens saved logs.
   Command-line alternative: `pip install pyserial`, then
   `python tools/ground_station.py COM5`.
7. Unit tests on your computer: `pio test -e native` (needs a C++ compiler:
   on Windows install one, e.g. `scoop install gcc` or MSYS2).

## Build environments

| Env | What |
|---|---|
| `flight` | Flight firmware |
| `bench` | Flight firmware plus `sim` (simulated flight with fake sensors) and `fire <ms> CONFIRM` (test-fire the pyro output). **Never fly a bench build.** It announces itself at boot and in `status` (`[BENCH]`). |
| `ground` | Ground station: receives telemetry, answers with heartbeats (pad beep period) |
| `native` | Unit tests of the flight logic on your PC |

## Repository layout

```
include/            board_pins.h, flight_config.h, radio_config.h   <- the only place for pins and tunables
lib/fc_core/        hardware-independent logic (tested on the PC)
  fc/flight_sm      state machine: launch, apogee, deployment, landing
  fc/estimator      Kalman filter: altitude + velocity from baro + accel
  fc/telemetry      binary frame format (radio and log), CRC-16
  fc/...            battery, radio duty cycle, LED/beep patterns, reset recovery, simulator
lib/fc_hal/         drivers: MPU-9250, BMP280, SX1262, pyro channel, ADC, system
src/flight/         flight firmware (app.cpp = wiring of the above)
src/ground/         ground station firmware
test/               unit tests (pio test -e native)
tools/              ground_station.html (UI), ground_station.py (CLI recorder), build_info.py
docs/               hardware, flight logic, telemetry, indicators, testing
```

## Documentation

- [docs/HARDWARE.md](docs/HARDWARE.md): wiring, pin map, battery, pyro circuit
- [docs/FLIGHT_LOGIC.md](docs/FLIGHT_LOGIC.md): states, deployment rules, failure handling
- [docs/INDICATORS.md](docs/INDICATORS.md): LED patterns and beep codes
- [docs/TELEMETRY.md](docs/TELEMETRY.md): radio settings, frame format, CSV columns
- [docs/TESTING.md](docs/TESTING.md): unit tests, bench tests, pre-flight checklist
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): code structure, how to extend
- [CHANGELOG.md](CHANGELOG.md)

## Traceability

Every build is stamped with its git commit (`FC_GIT_REV`, `-dirty` if there
were uncommitted changes). It is printed at boot and in `status`, and is the
argument of the first telemetry event (BOOT). Fly only clean builds: a log
then identifies exactly which code and configuration flew.
