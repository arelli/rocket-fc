# Hardware

All GPIO numbers live in [include/board_pins.h](../include/board_pins.h).
If you change wiring, change that file, this document and the schematics
(`tools/schematics.py`) in the same commit.

Schematics: [flight computer](schematics/flight.svg),
[ground station](schematics/ground.svg).

## Pin map (XIAO ESP32-S3)

| XIAO pin | GPIO | Function | Notes |
|---|---|---|---|
| D0 / A0 | 1 | Battery voltage sense | 100k/100k divider from switched BAT+ |
| D1 / A1 | 2 | Pyro continuity sense | 100k/100k divider from pyro MOSFET drain |
| D2 | 3 | Buzzer | Active buzzer + on the pin, − to GND |
| D3 | 4 | Pyro MOSFET gate | 100 Ω series, 100k pull-down |
| D4 | 5 | I²C SDA | MPU-9250 (0x68), BMP280 (0x76/0x77) |
| D5 | 6 | I²C SCL | |
| D6 | 43 | Status LED | Flickers briefly at boot (UART0 TX) |
| D7 | 44 | GPS TX → UART1 RX | Optional L76K, receive only (below) |
| D8 / D9 / D10 | 7 / 8 / 9 | SPI SCK / MISO / MOSI | Wio-SX1262 |
| 3V3 | | Sensor supply | MPU-9250, BMP280 |
| BAT+ / BAT− pads | | Battery | Underside of the XIAO |
| B2B connector | 38–42 | Wio-SX1262 control | NSS 41, DIO1 39, RESET 42, BUSY 40, RF switch 38 |
| B2B connector | 21 | Wio-SX1262 button | Also the XIAO's yellow LED. **Never drive it.** |

All header pins are used.

## GPS (optional): Seeed L76K GNSS module for XIAO

Not stacked on the XIAO: that board also connects to D0, D2 and D6, which are
the battery sense, the buzzer and the LED here. Wire it with four leads:

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
- With the GPS on D7 there is no pin left for an SD card's chip select: the
  flight log is kept in the XIAO's flash instead.

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

## Buzzer (on the pin)

`D2 ── buzzer (+), buzzer (−) ── GND`

- An active buzzer straight on the pin, tested on the flight kit. The
  firmware sets the pin to its strongest drive (about 40 mA); typical small
  active buzzers draw 10–30 mA at 3.3 V. A buzzer that is too quiet or draws
  more needs a transistor driver (logic-level N-MOSFET from BAT+).
- D2 is a strapping pin of the ESP32-S3 only for an eFuse-selected JTAG mode
  (not used); the buzzer to GND keeps it low at boot anyway.

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

## Ground station (second kit)

The ground station is the second XIAO ESP32-S3 + Wio-SX1262 kit, in its own
box, powered by an 18650 cell. It is also the launch controller. Schematic:
[schematics/ground.svg](schematics/ground.svg). Pins: `pins::ground` in
[include/board_pins.h](../include/board_pins.h).

| XIAO pin | GPIO | Function | Part |
|---|---|---|---|
| D0 / A0 | 1 | Battery sense | 100k/100k divider from BAT+ |
| D1 | 2 | Buzzer | active buzzer + on the pin, − to GND |
| D2 | 3 | Push button | to GND (internal pull-up) |
| D3 | 4 | ON LED | 330 Ω |
| D4 / A4 | 5 | Launch sense | 100k/100k divider from the launch MOSFET drain |
| D5 | 6 | Launch MOSFET gate | 100 Ω series, 100k pull-down, IRLB8748 |
| D6 | 43 | CONNECTED LED | 330 Ω (flickers at boot) |
| D7 | 44 | MODE switch | to GND (internal pull-up): open = FLIGHT, closed = BENCH |
| U.FL on the XIAO | | WiFi antenna | 2.4 GHz (D-Link) |
| U.FL on the Wio-SX1262 | | LoRa antenna | 868 MHz |

- **Power:** 18650 on the BAT pads through the power switch SW1, 1000 µF
  across BAT+/GND, battery divider on D0. The XIAO charges it from USB-C
  (50-100 mA: an 18650 takes a day; charge it in an external charger if you
  can). No automatic cutoff: switch off when the ON LED double-blinks.
- **Launch channel:** cell + (before SW1) → ARM switch SW2 → 1 Ω 5 W → launch
  lead terminals J1/J2 → igniter → J2 → MOSFET drain; source → cell −.
  ARM is in the igniter current: rated for at least 5 A (toggle or key
  switch). R8 (100k) across J1/J2 lets the firmware see ARM without an
  igniter: the drain reads 0 V with ARM off, about 2/3 of the battery when
  armed without an igniter, and the battery voltage when armed with an
  igniter connected. Thick wire for the whole loop; keep the launch leads
  short (put the station near the pad and use the phone at a distance).
- **MODE switch:** FLIGHT (open) or BENCH (closed). The station keeps the
  rocket's bench mode equal to it (by radio, on the ground). In FLIGHT a
  launch needs the rocket heard in PAD (`gs_launch_need_pad`); BENCH allows
  a launch channel test without the rocket.
- **Button:** ARM off: short = ping, hold 1.5 s = search mode on/off. ARM on:
  short = LAUNCH (countdown `gs_launch_countdown_s`), again = abort.
- **Buzzer and LEDs:** see the table.

| Signal | Meaning |
|---|---|
| 3 beeps, then a chirp every 2 s | ARMED (always audible) |
| 1 beep per second, then a long tone | Countdown, then the igniter pulse |
| 3 short beeps | Refused / aborted / no answer |
| Rocket beeps, station beeps 0.5 s later | Ping: both directions of the link work |
| Faster ticks | Search mode: stronger signal |
| ON LED steady / 2 Hz / fast / double blink | Running / ARMED / countdown, firing or radio failed / battery low |
| CONNECTED LED | On while rocket frames arrive, off for an instant at each frame |

`gs_buzzer_on` = 0 silences everything except the arming and launch sounds.

## Ground station WiFi

The ground station is also a WiFi access point that serves the UI, so it can
run from a USB power bank with a phone as the display. USB to a laptop keeps
working at the same time (both see the same data and can send commands).

- **Antenna:** fit the small 2.4 GHz antenna that came with the XIAO on the
  XIAO's own U.FL connector (not the Wio-SX1262's: that one is LoRa). Without
  it the WiFi range is about a metre.
- **Join:** network `rocketfc-XXXX` (WPA2). The password is random, made at
  the first boot; read it with `wifi` on the USB console or the UI's
  "Show password" (Radio link panel), and change it there or with
  `wifi pass <8..63 characters>`. Other commands: `wifi on|off`,
  `wifi ssid <name>`, `wifi channel <1..13>`; they apply after a reboot.
- **Open:** http://192.168.4.1 (always), http://rocketfc.local, or any http
  address (captive portal). The access point has no internet: if the phone
  asks, choose to stay connected.
- **Everything is over WiFi:** telemetry, launch, ping, search, ejection,
  both kits' settings and consoles (the rocket's by radio), and the rocket's
  onboard logs: "Rocket WiFi on" makes the rocket join this access point (only
  after landing, in FAULT or in bench mode), then the station relays its log
  files. The password is sent to the rocket encrypted (X25519 key exchange,
  docs/TELEMETRY.md). The rocket's USB console still works with `pio device monitor`.
- **Security:** anyone on the WiFi has the full UI, including LAUNCH (which
  still needs the physical ARM switch) and ejection. Keep the password private.
- **Power:** about 100-150 mA with WiFi on. Some power banks switch off below
  a minimum current; this load is normally above it.
- The UI served is `tools/ground_station.html`, embedded when the ground
  station firmware is built (`tools/embed_ui.py`): after changing the UI,
  reflash the ground station to update the WiFi copy.

## Flight log storage

Flights are logged to the XIAO's own flash (4.8 MB partition, kept after
power-off) and to PSRAM; see [TELEMETRY.md](TELEMETRY.md#onboard-log). No SD
card is needed. The flash layout is `partitions_flight.csv` (the settings in
NVS stay where they were). An SD card would need a chip-select pin; with the
GPS on D7 none is left.

## Parts list (besides the two kits and sensors)

| Part | Qty |
|---|---|
| IRLB8748PBF logic-level MOSFET (pyro) | 1 |
| Resistor 1 Ω 5 W (pyro current limit) | 1 |
| Resistor 100 Ω (gate, LED) | 2 |
| Resistor 100 kΩ (gate pull-down, two dividers) | 5 |
| Electrolytic capacitor 1000 µF 16 V | 1 |
| 2-pin screw terminal (arm link, igniter) | 2 |
| Active buzzer | 1 |
| LED, 3 or 5 mm | 1 |
| Slide switch (main power, ≥ 0.5 A) | 1 |
| Perfboard, wire (≥ 22 AWG for the pyro loop), crimp ferrules for the nichrome | |
| Optional: 100 nF capacitors on D0/D1 | |
