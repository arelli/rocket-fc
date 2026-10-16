# Architecture

## Layers

```
 include/          pins and fixed constants (data only)
     │
 src/flight/       App: reads sensors, calls the core, drives outputs        (thin, untested on PC)
 src/ground/       receive and print
     │
 lib/fc_hal/       drivers: Mpu9250, Bmp280, RadioLink, PyroChannel,       (hardware-specific)
                   DigitalOut/AnalogIn, system (reset cause, RTC record,
                   watchdog, sleep), LogSink interface
     │
 lib/fc_core/      all decisions and formats: FlightComputer =            (no Arduino; unit-tested)
                   Estimator + FlightStateMachine, telemetry frames, CSV,
                   battery monitor, duty-cycle limiter, LED/beep patterns,
                   resume logic, log ring buffer, flight simulator,
                   settings table + "param" command (ParamShell)
```

Rule: anything that decides something lives in `fc_core` and has a test.
`fc_hal` only talks to chips; `src/` only connects the two. `native` builds
ignore `fc_hal`, so a core file that includes Arduino headers fails to build.

## Data flow (every 10 ms)

```
 MPU-9250 ─┐                           ┌─► PyroChannel (fire)
 BMP280  ──┼─► FlightComputer::tick ───┼─► events ─┬─► log sinks (PSRAM, later SD)
           │   (Estimator → SM)        │           └─► radio queue (priority)
           │                           └─► state ─► LED, beeps, RTC record
           └──────────────────────────────► snapshot ─► log (50 Hz in flight)
                                                     └─► radio (5 Hz, duty-limited)
```

Slow tasks (10 Hz): battery and continuity ADC, low-battery cutoff, status
beeps, CSV stream. The loop never blocks: radio TX is interrupt-completed, I²C
has a 5 ms timeout, and serial output is dropped when no host reads it. A
watchdog resets the processor if the loop stalls for 2 s, and the RTC record
then resumes the flight.

## Extending

**Add an SD card:** write `class SdLog : public hal::LogSink` (open a file
in `begin`, buffer frames, write whole 512-byte blocks in `write_frame`, sync
in `flush`), add it in `App::init_log()` with `add_sink()`. Frames are the
same binary format; `fc::csv::format_frame` converts them. Keep writes
short: `write_frame` runs in the 100 Hz loop (consider a FreeRTOS task with a
queue if the card stalls).

**Add a sensor:** driver in `lib/fc_hal`, read it in `App::read_sensors`. If
the flight logic should use it, extend `SmInput`/`Estimator` in `fc_core` with
tests. If it should be transmitted, append fields to the telemetry frame,
increment `tlm::kVersion`, and update `docs/TELEMETRY.md`, the CSV format and
`tools/ground_station.html` (TLM_COLUMNS) and `tools/ground_station.py` together.

**Change the ground station UI:** `tools/ground_station.html` is one file with
no dependencies. What it shows (columns, flags, gauges, plots, command
buttons) is defined in the CONFIG tables at the top of its script; add a row
there to add a gauge, plot series or button.

**Add an event:** append a value to `EventCode` (never renumber), add its
name in `types.cpp` and a row in `docs/TELEMETRY.md`.

**Change a threshold:** it is a setting: change it in the UI's Settings panel
or with `param set` + `param save`, reboot (docs/SETTINGS.md). To change a
default or add a setting, edit `FC_SETTINGS` in `lib/fc_core/src/fc/settings.h`,
run `pio test -e native`, commit.

## Auditing a flight

1. The BOOT event / boot banner gives the firmware commit; the CONFIG event
   and the boot banner give the settings CRC (the banner also lists every
   setting that differs from the defaults).
2. `git checkout <hash>` shows the exact code; your exported settings file
   with the same CRC shows the settings.
3. The onboard log (50 Hz) and ground log (5 Hz + RSSI) are CSV with the same
   columns; events (LAUNCH, APOGEE, PYRO_FIRE, PYRO_OPEN, ...) carry
   timestamps for the timeline.
4. Replay: the CSV pressure/acceleration columns can be fed through
   `fc::FlightComputer` in a native test to check a change against a real flight.

## Toolchain

PlatformIO, `espressif32@6.9.0` (Arduino-ESP32 2.0.17), RadioLib 7.1.2, C++17.
All versions are pinned in `platformio.ini`.
