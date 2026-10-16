# Settings and bench mode

## Settings

Everything you may want to change without editing code is a **setting**:
launch, apogee and deployment thresholds, deployment delay and backup timer,
pyro pulse length, self-test checks, estimator and attitude filter tuning,
IMU and barometer filters, battery thresholds and divider calibration,
telemetry and log rates, buzzer and LED behaviour, radio (frequency,
bandwidth, spreading factor, power, receive boost, duty cycle, pairing key).

**One table defines them all:** `FC_SETTINGS` in
[lib/fc_core/src/fc/settings.h](../lib/fc_core/src/fc/settings.h): name,
group, type, default, range, unit, flags and help text. The firmware, the
console command and the UI's Settings panel are all generated from it. The
defaults are the values the flight logic is tested with (`test_settings`
checks this).

### Using them

- **UI:** the Settings panel (bottom of `tools/ground_station.html`) reads
  the settings of every connected kit, checks each change on the device as you
  make it, saves to flash and reboots to apply. Filter by name or "only
  changed"; export/import a settings file (`key=value` lines) to keep it with
  your flight records.
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
  changing any of them, run a simulated flight (`bench on`, `sim`) and re-read
  docs/FLIGHT_LOGIC.md.

### Radio settings (both kits)

Radio settings (`R`) exist on the flight computer and the ground station and
must be identical, or there is no link. The UI sends each radio change to
every connected kit and warns when they differ; save and reboot both. The
ground station's `param` command has only these settings.

Without `radio_unlock` the channel must lie within the EU 869.40–869.65 MHz
band (centre ± bandwidth/2) and the duty limit must not exceed 10 % per hour
(`radio_duty_pct` × 3600 s + `radio_burst_s` ≤ 360 s). `radio_unlock` = 1
("I accept", confirmed in the UI) allows 860–930 MHz and any duty cycle: you
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
pulse) and the remote test fire by radio.

- Kept in RAM only: **every boot starts in flight mode**.
- Switched with `bench on|off` on the flight computer's console (UI: Flight
  computer panel), or by radio from the ground station (`bench on|off`, UI:
  Remote test fire panel). The radio request carries a code derived from the
  pairing key `rf_key`; the rocket accepts it only on the ground.
- A detected launch switches it off (BENCH_MODE event, argument 2).
- Telemetry link bit 3 and the BENCH_MODE event report it; the UI shows a
  banner while it is on.
- Bench mode does not change the flight logic: launch detection and apogee
  deployment work the same. It only adds the test commands.
