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
| `test_log` | Onboard log ring buffer, CSV format, the firmware's real configuration |

They must all pass before you flash a flight build.

## 2. Bench: electronics (bench build, no igniter)

Flash `pio run -e bench -t upload`, open `pio device monitor`.

1. **Boot:** the banner shows the git hash and `[BENCH BUILD]`; IMU, barometer,
   radio and log report ok. `i2c` lists 0x68 and 0x76/0x77.
2. **Countdown:** fast LED blink, one tick per second, then 2 beeps (self-test ok).
   Read the warnings printed.
3. **Orientation:** stand the board as it will sit in the rocket, nose up.
   `status`: `accel_up` ≈ +1.00 g. If not, fix `cfg::kImuUpAxis`.
4. **Barometer:** `status` altitude ≈ 0. Lift the board 1–2 m: altitude
   follows within about ±0.5 m.
5. **Battery:** compare the `status` voltage with a multimeter on the cell;
   adjust `cfg::kBattDividerRatio` if they differ by more than 0.05 V.
6. **Radio:** run the ground station (`python tools/ground_station.py <port>`):
   1 packet/s, rssi shown, loss 0 %. Walk away for a range check if you can.
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
   must never flash. Repeat with a flight build.
5. **Bare nichrome** (1 cm, no charge, away from anything flammable): `fire 1000 CONFIRM`.
   Before firing, PAD shows continuity (LED 2 flashes/s, 3 short beeps).
   The wire must glow; if it burns through, a PYRO_OPEN event follows. Do this
   with the cell at its lowest expected voltage (~3.7 V) too. If the
   processor resets during the pulse, the supply wiring is too thin or the
   1000 µF capacitor is missing.
6. `sim live` with bare nichrome: burns at simulated apogee.

## 4. Before every flight

At home:
- [ ] Unit tests pass; flight build flashed from a clean commit (boot banner shows no `-dirty`, no `[BENCH BUILD]`)
- [ ] `config` shows the intended values (backup timer for this motor?)
- [ ] Cell fully charged (≥ 4.1 V at rest)
- [ ] Ground station kit and laptop charged; `ground_station.py` tested

At the pad (arm link OUT):
- [ ] Main switch on; countdown with the rocket still and vertical; 2 beeps
- [ ] LED 1 flash/s, 1 long beep every 10 s (no continuity yet), telemetry arriving
- [ ] Battery ≥ 3.9 V in telemetry; no unexpected warnings
- [ ] Charge and igniter installed; fit the arm link LAST, from the side
- [ ] LED 2 flashes/s, 3 short beeps every 10 s (continuity)

After recovery:
- [ ] Remove the arm link first (an unfired charge may still be live)
- [ ] Listen to the altitude beeps; leave the main switch ON
- [ ] Download the onboard log (`ground_station.py <port> --dump`), then switch off
- [ ] Recharge the cell
