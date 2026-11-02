# Settings and bench mode

## Settings

Everything you may want to change without editing code is a **setting**:
launch, apogee and deployment thresholds, deployment delay and backup timer,
pyro pulse length, self-test checks, estimator and attitude filter tuning,
IMU and barometer filters, battery thresholds and divider calibration,
telemetry and log rates, buzzer and LED behaviour, radio (frequency,
bandwidth, spreading factor, power, receive boost, duty cycle, pairing key),
the optional GPS (off / auto / required, baud rate, radio period, clock),
manual ejection by radio, and the ground station's panel (search mode
range, buzzer, battery divider and low warning).

**One table defines them all:** `FC_SETTINGS` in
[lib/fc_core/src/fc/settings.h](../lib/fc_core/src/fc/settings.h): name,
group, type, default, range, unit, flags and help text. The firmware, the
console command and the UI's Settings panel are all generated from it. The
defaults are the values the flight logic is tested with (`test_settings`
checks this).

### Using them

- **UI (WiFi page):** Settings → Rocket or Station → Read. The rocket's
  settings travel by radio (it must be on the ground and in range). Each
  change is checked on the device as you make it; Save, then Reboot to
  apply. Radio settings change on both kits together. `r param export` in
  the console prints a settings file (`key=value` lines) to keep with your
  flight records.
- **Console:** `param list [group]`, `param get <key>`, `param set <key> <value>`,
  `param reset <key>|all`, `param revert` (discard unsaved), `param save`,
  `param export`. `config` prints the settings in use, marking changed ones.

### Rules

- **Stored in flash** (ESP32 NVS), only the values that differ from the
  defaults. They survive power-off and reflashing (not a full flash erase). A
  newer firmware with different defaults keeps your changes and uses its new
  defaults for the rest.
- **Applied at boot only.** The configuration in use never changes until the
  next boot. `param save` stores; reboot applies.
- **Checked twice:** each value against its range when set; the whole set
  (cross-checks such as calib_ms ≤ settle_ms, burnout below launch threshold,
  critical below low battery, the flight logic's own `validate()`, the radio
  rules) before it can be saved.
- **Invalid stored settings** (unreadable, a value out of range for this
  firmware, failed cross-check): the computer runs on the defaults, prints
  why, and its self-test fails with fault 3 (it will not fly). Fix with
  `param set` / `param save` or `param reset all` / `param save`, then reboot.
  A stored setting this firmware does not know is ignored with a message.
- **Refused in flight** (BOOST, COAST, DESCENT).
- **Traceable:** the CRC-32 of the complete settings in use is printed at boot,
  in `status` and `config`, in the `dump` header, and logged as the CONFIG
  event after BOOT. Commit hash + settings CRC (+ the exported settings file)
  identify exactly what flew.
- Settings marked **flight safety** (`S`) decide when the charge fires. After
  changing any of them, run a simulated flight (bench mode, `sim`) and re-read
  docs/FLIGHT_LOGIC.md.

### Manual ejection (`manual_eject`, flight safety)

Off by default. When on, the ground station can fire the ejection charge at
any moment after launch detection (UI: "Eject parachute", or `eject` on the
ground station). It overrides the apogee logic, so a mistaken command during
the climb ejects at full speed. The UI asks for confirmation when you switch
it on (also for `radio_unlock`). The rocket prints a warning at boot, sets
self-test warning bit 7 and telemetry link bit 6 (UI: MANUAL EJECT pill). Set it back to 0 when you do
not need it. Without it, manual ejection works only on the pad in bench mode
(test pulse).

### Ground station settings (`G`)

`gs_*` settings exist only on the ground station (its `param` command and
the UI's Settings → Station): search mode signal range, buzzer on/off,
battery divider and low-battery threshold, and the launch channel:
`gs_launch_countdown_s` (3 s; 0 = fire at once), `gs_launch_pulse_ms`
(2000 ms), `gs_launch_need_pad` (1: in FLIGHT mode, launch only while the
rocket is heard in PAD and in flight mode; 0: launch without the flight
computer) and `gs_launch_divider`. Like all settings they apply after save
and reboot.

### Radio settings (both kits)

Radio settings (`R`) exist on the flight computer and the ground station and
must be identical, or there is no link. The UI sends each radio change to
both kits (the rocket's by radio); save and reboot the rocket first, then
the station. The
ground station's `param` command has only these and its own `gs_*` settings.

Without `radio_unlock` the channel must lie within the EU 869.40–869.65 MHz
band (centre ± bandwidth/2) and the duty limit must not exceed 10 % per hour
(`radio_duty_pct` × 3600 s + `radio_burst_s` ≤ 360 s). `radio_unlock` = 1
allows 860–930 MHz and any duty cycle: you
are responsible for the rules where you operate. The SX1262 transmits at most
22 dBm. Receive sensitivity is set by the spreading factor and bandwidth
(each SF step ≈ 2.5 dB, at twice the airtime) and `radio_rx_boost` (≈ 2 dB).
Slower settings lengthen every packet: the telemetry rate falls to what the
duty limit allows, and the rocket's listen window after each packet grows
automatically to fit the ground station's reply.

### Adding a setting

Add one `X(...)` line to `FC_SETTINGS`, use `set_.<name>` in the firmware
(or map it in `settings.cpp` for fc_core objects), add a cross-check to
`validate()` if needed, and run the tests. The UI picks it up by itself.

## Bench mode

There is one flight computer firmware. **Bench mode** enables its test
commands: `sim` / `sim live` (simulated flight), `fire <ms> CONFIRM` (USB test
pulse), the ejection charge test from the ground station on the pad, and the
rocket's WiFi for the log download.

- **Every boot starts in bench mode** (setting `bench_at_boot` = 1; 0 = start
  in flight mode). Not after a processor reset in flight (the flight resumes).
- Normally set by the ground station's **MODE** (switch open or page = BENCH,
  switch closed = FLIGHT; the station also starts in BENCH): the station sends the
  rocket's bench mode by radio until it matches (on the ground only). The
  request carries a code derived from the pairing key `rf_key`. Over USB:
  `bench on|off` (the station switches it back to match its switch).
- A detected launch switches it off (BENCH_MODE event, argument 2).
- Telemetry link bit 3 and the BENCH_MODE event report it.
- **Before a real flight switch the station to FLIGHT:** in BENCH, LAUNCH
  does not check that the rocket is ready (in PAD, in flight mode).
- Bench mode does not change the flight logic: launch detection and apogee
  deployment work the same. It only adds the test commands.
