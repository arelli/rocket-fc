// Hardware pin map: Seeed XIAO ESP32-S3 + Wio-SX1262 kit.
// This is the ONLY file that names GPIO numbers. Wiring: docs/HARDWARE.md.
//
//   XIAO pin  GPIO  use
//   D0 / A0     1   battery voltage sense  (100k/100k divider from switched BAT+)
//   D1 / A1     2   pyro continuity sense  (100k/100k divider from pyro MOSFET drain)
//   D2          3   active buzzer          (+ on the pin, − to GND)
//   D3          4   pyro MOSFET gate       (100R series, 100k pull-down)
//   D4          5   I2C SDA  (MPU9250 0x68, BMP280 0x76/0x77)
//   D5          6   I2C SCL
//   D6         43   status LED (100R to LED anode, cathode to GND)
//   D7         44   GPS module TX -> UART1 RX (optional L76K, receive only)
//   D8-D10   7-9   SPI to Wio-SX1262 (SCK, MISO, MOSI)
//   B2B     38-42   Wio-SX1262 control (RF switch, DIO1, BUSY, NSS, RESET)
//   B2B        21   Wio-SX1262 user button (also the XIAO's yellow LED)
#pragma once

namespace pins {

// I2C sensor bus
constexpr int kI2cSda = 5;  // D4
constexpr int kI2cScl = 6;  // D5

// Wio-SX1262 LoRa radio (same mapping as Seeed's and Meshtastic's firmware)
constexpr int kLoraSck = 7;     // D8
constexpr int kLoraMiso = 8;    // D9
constexpr int kLoraMosi = 9;    // D10
constexpr int kLoraNss = 41;    // B2B
constexpr int kLoraDio1 = 39;   // B2B
constexpr int kLoraReset = 42;  // B2B
constexpr int kLoraBusy = 40;   // B2B
constexpr int kLoraRfSw = 38;   // B2B, RF switch (RX enable)
constexpr int kWioButton = 21;  // active low; do NOT drive this pin as an output

// Analog inputs (ADC1)
constexpr int kBattSense = 1;  // D0 / A0
constexpr int kPyroSense = 2;  // D1 / A1

// Outputs
constexpr int kBuzzer = 3;      // D2: active buzzer + (buzzer − to GND)
constexpr int kPyroGate = 4;    // D3
constexpr int kStatusLed = 43;  // D6 (flickers briefly at boot: ROM log on UART0 TX)

// Optional GPS (L76K): its TX output into this pin (UART1 RX). An SD card
// would need another chip-select pin (none left on the header).
constexpr int kGpsRx = 44;  // D7

// Ground station panel (second kit only; docs/HARDWARE.md "Ground station").
// The flight computer's pin map above does not apply to it (no sensors).
namespace ground {
constexpr int kBattSense = 1; // D0 / A0: 100k/100k divider from BAT+ (same as the flight kit)
constexpr int kBuzzer = 2;    // D1: active buzzer + (buzzer − to GND)
constexpr int kButton = 3;    // D2: push button to GND (internal pull-up)
constexpr int kLedPower = 4;  // D3: "on" LED (330R)
constexpr int kLedLink = 5;   // D4: "rocket connected" LED (330R)
}  // namespace ground

}  // namespace pins
