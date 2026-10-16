// Fixed radio hardware constants. The LoRa link itself (frequency, bandwidth,
// spreading factor, power, duty cycle, pairing key) is made of SETTINGS
// (lib/fc_core/src/fc/settings.h, group "radio"); they must be the same on the
// flight computer and the ground station, and the UI sets both together.
//
// Default band: 869.40-869.65 MHz (EU, ERC REC 70-03 band 54): up to 500 mW
// ERP and 10 % duty cycle per hour; the whole 250 kHz band may be used as one
// channel. Leaving it needs the radio_unlock setting ("I accept").
#pragma once

namespace radio_cfg {

constexpr float kTcxoVolts = 1.8f;  // Wio-SX1262 TCXO supply

}  // namespace radio_cfg
