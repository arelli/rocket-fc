// LoRa link parameters shared by the flight computer and the ground station.
// Both ends MUST be built from the same version of this file.
//
// Band: 869.40-869.65 MHz (EU, ERC REC 70-03 band 54): up to 500 mW ERP and
// 10 % duty cycle per hour; the whole 250 kHz band may be used as one channel.
// Both ends enforce the duty cycle with fc::DutyCycleLimiter (the rocket for
// telemetry, the ground station for its heartbeats).
// Outside the EU, change frequency/duty limits to your local rules.
#pragma once

#include <stdint.h>

namespace radio_cfg {

constexpr float kFreqMHz = 869.525f;  // centre of the 869.40-869.65 MHz band
constexpr float kBandwidthKHz = 250.0f;
constexpr uint8_t kSpreadingFactor = 7;
constexpr uint8_t kCodingRateDenom = 5;  // 4/5
constexpr uint8_t kSyncWord = 0x12;      // private network (not LoRaWAN 0x34, not Meshtastic 0x2B)
constexpr uint16_t kPreambleLen = 8;
constexpr int8_t kTxPowerDbm = 20;       // SX1262 max 22; legal limit here is 27 dBm ERP
constexpr float kTcxoVolts = 1.8f;       // Wio-SX1262 TCXO supply

// Remote test fire pairing key (bench build only, see fc/remote_fire.h): the
// ground station and the flight computer must be built with the same value.
// Change it to your own random number so another rocket-fc ground station
// cannot fire your rocket. Not a cryptographic secret; the arm link is the
// real safety.
constexpr uint32_t kRemoteFireKey = 0x6B1D4F37u;

// Duty cycle budget: 8 %/h long-term + 72 s burst  =>  <= 360 s (10 %) in any hour.
constexpr float kDutyFraction = 0.08f;
constexpr uint32_t kDutyBurstMs = 72000;

}  // namespace radio_cfg
