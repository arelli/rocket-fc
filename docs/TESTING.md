# Testing

Work through the levels in order. Do not attach an ejection charge until
every bench step has passed, and repeat the relevant steps after any change
to wiring or code.

## 1. Unit tests (your PC)

```
pio test -e native
```

| Suite | Covers |
|---|---|
| `test_flight` | Complete simulated flights through estimator + state machine: nominal and high-power flights, 20 noise seeds, 10 min on the pad, knock and gust on the pad, low hop below the minimum deploy altitude, backup timer with dead barometer, dead IMU, failed self-test, short flight vs. lockout, resets in coast and after deployment, config validation. Asserts that the charge is commanded exactly once, never before launch detection, never on the pad. |
| `test_crc_telemetry` | CRC-16, frame encode/decode, corruption detection, saturation |
| `test_radio_budget` | LoRa airtime against reference values, 10 % per hour duty cycle |
| `test_battery_indicators` | Battery thresholds and debounce, LED patterns, beep sequences |
| `test_resume` | Flight record checksum and when to resume after a reset |
| `test_log` | Onboard log ring buffer, CSV format, the default settings pass validation |
| `test_flash_log` | Flash log on a simulated NOR flash: sessions, order, wrap-around, power loss mid-record, erase ahead |
| `test_gps` | NMEA parsing (fix, no fix, corrupted data, midnight), GPS frame and CSV line |
| `test_settings` | Settings table, ranges, text storage format, cross-checks, radio band rules, defaults = tested flight logic values |
| `test_param_shell` | The `param` command: listing, editing, refusal in flight, saving |
| `test_uplink` / `test_remote_fire` | Heartbeat, bench mode and remote fire frames, one-time codes |

They must all pass before you flash the firmware for a flight.

## 2. Bench: electronics (no igniter)

Flash `pio run -e flight -t upload`, open `pio device monitor` (or the UI).

1. **Boot:** the banner shows the git hash, the settings CRC (and any settings
   that differ from the defaults) and `mode: FLIGHT`; IMU, barometer, radio
   and log report ok. `i2c` lists 0x68 and 0x76/0x77. Type `bench on`: the
   test commands below need bench mode (UI: banner at the top).
2. **Countdown:** fast LED blink, one tick per second, then 2 beeps (self-test ok).
   Read the warnings printed.
3. **Orientation:** stand the board as it will sit in the rocket, nose up.
   `status`: `accel_up` ≈ +1.00 g. If not, set `imu_up_axis` (Settings panel), save, reboot.
4. **Barometer:** `status` altitude ≈ 0. Lift the board 1–2 m: altitude
   follows within about ±0.5 m.
5. **Battery:** compare the `status` voltage with a multimeter on the cell;
   adjust the `batt_divider` setting if they differ by more than 0.05 V.
6. **Radio:** run the ground station (`python tools/ground_station.py <port>`):
   1 packet/s, rssi shown, loss 0 %. Walk away for a range check if you can.
6b. **GPS (if fitted):** `gps raw on` shows NMEA sentences within a second
   (none: check D7 wiring and `gps_baud`), `gps raw off`. Outdoors, `gps`
   and the UI's GPS panel show a fix within a minute or two.
7. **Simulated flight:** type `sim`. Watch the console: LAUNCH about 10 s
   later, BURNOUT, APOGEE, `[sim] FIRE command`, LANDED, and the final line
   comparing true and detected apogee. The ground station shows the whole
   flight. `reboot` afterwards.
8. **Reset recovery:** not testable by hand on the bench (a reset button press
   counts as power-on, which correctly starts fresh). Covered by unit tests.

## 3. Bench: pyro channel (LED, then bare nichrome)

1. **Dummy load first:** in place of the igniter, an LED with a 220 Ω resistor
   (anode towards the arm link). The LED's forward voltage keeps the drain
   below the 60 % threshold, so continuity usually reads "no" with this
   dummy; that is expected. Continuity is checked in step 5.
2. `fire 500 CONFIRM`: the dummy LED lights for 0.5 s.
3. `sim live`: the dummy LED lights at the simulated apogee, once.
4. **Power-cycle test:** with the dummy fitted and the link in, switch off/on
   10 times, press reset 10 times, plug/unplug USB 10 times. The dummy LED
   must never flash. Repeat in flight mode (after a reboot).
5. **Bare nichrome** (1 cm, no charge, away from anything flammable): `fire 1000 CONFIRM`.
   Before firing, PAD shows continuity (LED 2 flashes/s; 3 short beeps while
   the ground station is running).
   The wire must glow; if it burns through, a PYRO_OPEN event follows. Do this
   with the cell at its lowest expected voltage (~3.7 V) too. The PYRO_SAG
   event (UI: Battery panel, "Last pyro pulse") shows the battery before and
   its minimum during the pulse; below about 3.3 V the processor may reset.
   If it resets, the supply wiring is too thin or the 1000 µF capacitor is missing.
6. `sim live` with bare nichrome: burns at simulated apogee.

## 3b. Ejection charge ground test (remote, bench mode)

Proves the charge size separates the rocket and deploys the parachute.

1. Rocket assembled with parachute and charge, restrained horizontally, nose
   pointing away from people. Ground station MODE switch to **BENCH** (the
   rocket follows: banner on the WiFi page).
2. Fit the rocket's arm link last; check "CHARGE OK" on the page. Everyone
   moves to a safe distance.
3. **Eject parachute** → OK: the rocket arms and fires a test pulse at once.
4. After a misfire wait at least 60 s, then remove the arm link before
   touching anything.
5. MODE switch back to **FLIGHT**; the banner must be gone.

## 3c. Launch channel (ground station)

1. No igniter, ARM off: the page shows "ARM off".
2. ARM on: three beeps, then a chirp every 2 s; "NO igniter".
3. Bare nichrome (or an LED with a resistor) on the launch leads: "igniter OK".
4. MODE **BENCH**, LAUNCH: countdown beeps, then the pulse burns the nichrome.
   ABORT during the countdown must stop it; ARM off must too.
5. MODE **FLIGHT**: LAUNCH is refused unless the rocket is heard in PAD.

## 4. Before every flight

At home:
- [ ] Unit tests pass; firmware flashed from a clean commit (boot banner shows no `-dirty`)
- [ ] Settings: intended values (backup timer, deploy delay for this motor?), saved, rebooted; export the settings file and note its CRC
- [ ] After changing any flight-safety setting: a simulated flight in bench mode (`bench on`, `sim`), then reboot
- [ ] Cell fully charged (≥ 4.1 V at rest)
- [ ] Ground station kit and laptop charged; `ground_station.py` tested

At the pad (arm link OUT):
- [ ] Main switch on; countdown with the rocket still and vertical; 2 beeps
- [ ] Ground station UI shows telemetry and "Rocket hears ground: yes", no bench mode banner
- [ ] LED 1 flash/s, 1 long beep every 10 s (no continuity yet; beeps need the ground station)
- [ ] Battery ≥ 3.9 V in telemetry; no unexpected warnings
- [ ] GPS (if fitted): fix on the pad (UI GPS panel), so the pad position is recorded
- [ ] Charge and igniter installed; fit the arm link LAST, from the side
- [ ] LED 2 flashes/s, 3 short beeps every 10 s (continuity)

After recovery:
- [ ] Remove the arm link first (an unfired charge may still be live)
- [ ] Listen to the altitude beeps, then switch off
- [ ] At home: UI → Flight computer → List sessions → Download the flight session (or `flog dump <n>`)
- [ ] Recharge the cell
