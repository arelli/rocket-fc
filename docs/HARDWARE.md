# Hardware

All GPIO numbers live in [include/board_pins.h](../include/board_pins.h).
If you change wiring, change that file and this document in the same commit.

## Pin map (XIAO ESP32-S3)

| XIAO pin | GPIO | Function | Notes |
|---|---|---|---|
| D0 / A0 | 1 | Battery voltage sense | 100k/100k divider from switched BAT+ |
| D1 / A1 | 2 | Pyro continuity sense | 100k/100k divider from pyro MOSFET drain |
| D2 | 3 | Buzzer MOSFET gate | Strapping pin: the 100k pull-down is required |
| D3 | 4 | Pyro MOSFET gate | 100 Ω series, 100k pull-down |
| D4 | 5 | I²C SDA | MPU-9250 (0x68), BMP280 (0x76/0x77) |
| D5 | 6 | I²C SCL | |
| D6 | 43 | Status LED | Flickers briefly at boot (UART0 TX) |
| D7 | 44 | GPS TX → UART1 RX | Optional L76K, receive only (below) |
| D8 / D9 / D10 | 7 / 8 / 9 | SPI SCK / MISO / MOSI | Wio-SX1262 (an SD card could share it, but needs a free CS pin) |
| 3V3 | | Sensor supply | MPU-9250, BMP280 |
| BAT+ / BAT− pads | | Battery | Underside of the XIAO |
| B2B connector | 38–42 | Wio-SX1262 control | NSS 41, DIO1 39, RESET 42, BUSY 40, RF switch 38 |
| B2B connector | 21 | Wio-SX1262 button | Also the XIAO's yellow LED. **Never drive it.** |

All header pins are used.

## GPS (optional): Seeed L76K GNSS module for XIAO

Not stacked on the XIAO: that board also connects to D0, D2 and D6, which are
the battery sense, the buzzer gate and the LED here. Wire it with four leads:

| L76K pin | Connect to | Why |
|---|---|---|
| 3V3 (VCC) | XIAO 3V3 | ~41 mA while tracking |
| GND | GND | |
| TX (GPS data out) | **D7** | Read at 9600 baud (setting `gps_baud`) |
| RX (GPS data in) | 3V3 | Not used (default NMEA output is enough); held at the idle level |
| the two control pins at its D0 and D2 positions | 3V3 | Seeed: both must be held high for normal operation |
| its D6 position | not connected | |

- Settings (group "gps"): `gps_mode` = auto (default: used if present, a
  self-test warning if not), off (D7 unused), required (self-test FAULT 7 without
  GPS data); `gps_tlm_ms` radio period; `gps_time` (set the clock from GPS
  when the ground station has not).
- Antenna: ceramic patch facing up (nose cone), away from carbon fibre and
  metal, a few cm from the LoRa antenna. First fix: 30 s or more outdoors:
  wait for the fix on the pad.
- The position is for recovery and the log only; the flight logic never uses it.
- Console: `gps` (status), `gps raw on|off` (echo the NMEA sentences, for wiring checks).
- With the GPS on D7 there is no pin left for an SD card's chip select.

## Power

```
                   main switch (slide)
 18350 + ──┬────────o/ o──────────┬──────────────┬───────────── XIAO BAT+ pad
           │                      │              │
           │                   1000 µF        100k
           │                   16 V (+)          ├──── D0 (+100 nF to GND, optional)
           │                      │           100k
           │                      │              │
 18350 − ──┼──────────────────────┴──────────────┴───────────── XIAO BAT− pad (GND)
           │
           └── to the pyro circuit (arm link), see below
```

- **Polarity:** on the XIAO the BAT− pad is the one **nearest the USB
  connector**. Check twice: reversing the cell destroys the board.
- **Pads under the radio:** if the Wio-SX1262 board covers the BAT pads, unplug
  it (B2B connector), solder thin wires to the pads, route them out to the
  side, and plug it back in.
- **Wire the cell with solder tabs or a tight, taped holder.** A spring holder
  can bounce open for a millisecond under boost and reset the processor.
- **1000 µF capacitor** across BAT+/GND, close to the XIAO: rides through the
  voltage dip during the pyro pulse and radio transmissions. Observe its polarity.
- **Battery sense divider** (2 × 100k): ratio 2.0, setting `batt_divider`.
  It draws about 20 µA. Calibrate with a multimeter against the `status`
  reading. During every pyro pulse the battery is sampled about every 0.5 ms;
  the PYRO_SAG event reports the voltage before and the minimum during the
  pulse (a large drop means a weak cell, thin wiring or a missing capacitor).

### Charging and battery management

- **Charging:** plug USB-C into the XIAO with the main switch **on**. The XIAO's
  built-in charger stops at 4.2 V. Seeed lists the charge current as 50 mA
  (XIAO ESP32-S3 spec) or 100 mA (kit page), so a full 700 mAh charge takes
  roughly 7–14 hours. The red LED on the XIAO shows charging. There is no
  charge-status signal to the processor. The ground station UI infers charging from
  telemetry: the USB-host flag (a computer is connected to the flight
  computer's USB) and the voltage trend.
- **While USB is connected**, the battery reading shows the charger voltage,
  not the cell's resting voltage.
- **No over-discharge protection.** The XIAO has none, and the 18350 cell is
  most likely unprotected. The firmware is the only protection:
  - below 3.60 V: BATT_LOW event, LED low-battery pattern on the pad;
  - below 3.40 V for 3 s: BATT_CRITICAL event; if this persists for 30 s on
    the pad, in FAULT or after landing (never in flight, never while a computer is connected over USB), the computer puts
    the sensors and radio to sleep and enters deep sleep.
  - Deep sleep reduces the drain about 100-fold but does not stop it.
    **Switch off promptly after recovery** and recharge before storage.
- Voltage is reported in every telemetry frame, `status`, and the onboard log.
- Thresholds: settings `batt_low_v`, `batt_crit_v`, `batt_sleep_s` (battery group).
- Alternative: a protected cell, or the TP4056 module's protection (1.6 g) between the cell and the XIAO.

## Pyro channel (low-side MOSFET Q1)

```
 18350 + ── ARM link ── 1 Ω 5 W ── igniter (1 cm 32 AWG nichrome) ──┬── Q1 drain (IRLB8748)
            (screw terminal +                                       │
             removable wire)                                       100k
                                                                    ├── D1 (+100 nF to GND, optional)
                                                                   100k
                                                                    │
 D3 ── 100 Ω ──┬── Q1 gate                                         GND
               100k
               │
 GND ──────────┴── Q1 source ── 18350 − (thick wire, direct to the cell)
```

- The pyro branch is taken from the cell **before** the main switch, so its
  ~3 A pulse never flows through the 1 A slide switch.
- **ARM link:** the igniter circuit is open until the link is fitted. Order on
  the pad: main switch on → wait for the PAD state → fit the link. After
  recovery: remove the link → main switch off.
- **Current:** about 3.7 V / (1 Ω + 0.34 Ω + wiring) ≈ 2.5–3 A. The 1 Ω limits the
  battery sag that would otherwise reset the processor.
- **Gate pull-down (100k):** keeps Q1 off while the ESP32 boots, resets or
  sleeps. The firmware also drives D3 low before anything else.
- **Continuity:** with the link fitted and the bridge intact, the drain sits
  at battery voltage (through the bridge); the divider halves it for D1. The
  firmware reports continuity when the drain reads ≥ 60 % of the battery
  voltage. The sense current (~20 µA) is far below anything that could heat
  the bridge.
- Use thick wire (≥ 22 AWG) for the cell → link → resistor → igniter → Q1 →
  cell− loop; the 1 Ω resistor gets warm only during the 1 s pulse.

## Buzzer (low-side MOSFET Q2)

```
 switched BAT+ ── buzzer (+) ── buzzer (−) ── Q2 drain (IRLB8748)
 D2 ── 100 Ω ──┬── Q2 gate
               100k
 GND ──────────┴── Q2 source
```

- The "5 V" active buzzer runs from the 3.0–4.2 V battery, a little quieter.
  Do not drive it straight from a GPIO pin (current).
- D2 is a strapping pin of the ESP32-S3. With the 100k pull-down it boots normally.
- Optional: a 1N4148 diode across the buzzer (cathode to BAT+).

## Status LED

`D6 ── 100 Ω ── LED anode, LED cathode ── GND` (about 13 mA for a red LED).
Use any visible 3 mm/5 mm LED.

## Sensors (I²C, 400 kHz)

| Board | VCC | GND | SDA | SCL | Other |
|---|---|---|---|---|---|
| MPU-9250 (SparkFun breakout, no regulator) | 3V3 | GND | D4 | D5 | CS → 3V3, FSYNC → GND; AD0, INT open (0x68 via SJ2) |
| GY-BMP280-5V (4 pins) | **3V3**, never 5V | GND | D4 | D5 | Nothing else: regulator, level shifter, pull-ups and address are on the board |

- The GY-BMP280-5V board's 10k pull-ups go to its VCC pin. At VCC = 5 V they
  would pull SDA/SCL to 5 V, above the ESP32-S3's 3.6 V limit; at 3V3 its
  662K regulator simply passes ~3.2 V through to the sensor.
- The firmware identifies the IMU chip from WHO_AM_I. Many "MPU-9250" boards
  carry an MPU-6500 (no magnetometer); the boot message and `status` say which
  you have. Everything works without the magnetometer.
- **IMU orientation:** mount it with one axis along the rocket's long axis and
  set the `imu_up_axis` setting (e.g. `+X` if the +X arrow points to the nose).
  Check on the bench: stand the rocket nose-up and `status` must show
  `accel_up` ≈ +1.00 g. Self-test warns if it does not.
- **Barometer:** needs a vented bay (3–4 holes of ~2 mm round the airframe),
  shielded from direct sunlight and from the ejection gases. Foam over the
  sensor reduces pressure noise from airflow.

## LoRa radio

The Wio-SX1262 plugs into the XIAO's B2B connector. Always attach the antenna
before powering up: transmitting without an antenna can damage the radio.
Settings: [docs/TELEMETRY.md](TELEMETRY.md).

## SD card (future)

An SPI micro-SD module can share SCK/MISO/MOSI (D8–D10) with the radio, but
needs its own CS pin: D7 is now the optional GPS input, so the GPS and an SD
card exclude each other unless a pin is freed (e.g. the LED). In software: implement `hal::LogSink` (see
[lib/fc_hal/src/hal/log_sink.h](../lib/fc_hal/src/hal/log_sink.h)) and register
it in `App::init_log()`. Until then flights are logged to PSRAM (see
[TELEMETRY.md](TELEMETRY.md#onboard-log)).

## Parts list (besides the two kits and sensors)

| Part | Qty |
|---|---|
| IRLB8748PBF logic-level MOSFET (pyro, buzzer) | 2 |
| Resistor 1 Ω 5 W (pyro current limit) | 1 |
| Resistor 100 Ω (gates, LED) | 3 |
| Resistor 100 kΩ (gate pull-downs, two dividers) | 6 |
| Electrolytic capacitor 1000 µF 16 V | 1 |
| 2-pin screw terminal (arm link, igniter) | 2 |
| Active buzzer | 1 |
| LED, 3 or 5 mm | 1 |
| Slide switch (main power, ≥ 0.5 A) | 1 |
| Perfboard, wire (≥ 22 AWG for the pyro loop), crimp ferrules for the nichrome | |
| Optional: 100 nF capacitors on D0/D1, 1N4148 across the buzzer | |
