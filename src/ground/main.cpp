// Ground station firmware (second XIAO ESP32-S3 + Wio-SX1262 kit on USB).
//
// Receives the flight computer's LoRa frames and prints one CSV line per
// frame on USB serial (format: lib/fc_core/src/fc/csv.h) with the radio
// signal quality appended:   ...,rssi_dbm,snr_db
// Lines starting with '#' are comments/diagnostics. tools/ground_station.py
// saves everything to a file and shows a live summary.
//
// This build only receives: it never transmits.
#include <Arduino.h>

#include "board_pins.h"
#include "fc/csv.h"
#include "fc/telemetry.h"
#include "hal/radio_link.h"
#include "radio_config.h"

namespace {

hal::RadioLink radio;
uint32_t n_ok = 0, n_bad = 0, t_stats = 0;
float last_rssi = 0, last_snr = 0;

void print_headers() {
  Serial.printf("# rocket-fc ground station %s  %.3f MHz SF%u BW%.0f sync 0x%02X\n", FC_GIT_REV,
                static_cast<double>(radio_cfg::kFreqMHz), radio_cfg::kSpreadingFactor,
                static_cast<double>(radio_cfg::kBandwidthKHz), radio_cfg::kSyncWord);
  Serial.printf("%s,rssi_dbm,snr_db\n", fc::csv::telemetry_header());
  Serial.printf("%s,rssi_dbm,snr_db\n", fc::csv::event_header());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(20);
  // Note: GPIO21 is the Wio-SX1262 button and the XIAO LED: never drive it.

  const hal::RadioLink::Pins p{pins::kLoraSck,  pins::kLoraMiso,  pins::kLoraMosi,
                               pins::kLoraNss,  pins::kLoraDio1,  pins::kLoraReset,
                               pins::kLoraBusy, pins::kLoraRfSw};
  const hal::RadioLink::Params r{radio_cfg::kFreqMHz,         radio_cfg::kBandwidthKHz,
                                 radio_cfg::kSpreadingFactor, radio_cfg::kCodingRateDenom,
                                 radio_cfg::kSyncWord,        radio_cfg::kTxPowerDbm,
                                 radio_cfg::kPreambleLen,     radio_cfg::kTcxoVolts};
  delay(1500);  // give the USB host time to open the port and see the header
  print_headers();
  if (!radio.begin(p, r) || !radio.start_receive()) {
    Serial.printf("# ERROR: radio init failed (RadioLib error %d)\n", radio.last_error());
  } else {
    Serial.println("# listening");
  }
}

void loop() {
  uint8_t buf[64];
  float rssi = 0, snr = 0;
  bool crc_error = false;
  const size_t n = radio.receive(buf, sizeof(buf), &rssi, &snr, &crc_error);
  if (n > 0) {
    last_rssi = rssi;
    last_snr = snr;
    char line[fc::csv::kLineMax];
    if (!crc_error && fc::csv::format_frame(buf, n, line, sizeof(line))) {
      ++n_ok;
      Serial.printf("%s,%.1f,%.2f\n", line, static_cast<double>(rssi), static_cast<double>(snr));
    } else {
      ++n_bad;
      Serial.printf("# bad frame: %u bytes, %s, rssi %.1f snr %.2f\n", static_cast<unsigned>(n),
                    crc_error ? "LoRa CRC error" : "not a rocket-fc frame",
                    static_cast<double>(rssi), static_cast<double>(snr));
    }
  }

  const uint32_t now = millis();
  if (now - t_stats >= 10000) {
    t_stats = now;
    Serial.printf("# stats: %lu ok, %lu bad, last rssi %.1f dBm snr %.2f dB\n",
                  static_cast<unsigned long>(n_ok), static_cast<unsigned long>(n_bad),
                  static_cast<double>(last_rssi), static_cast<double>(last_snr));
  }

  // Re-print the headers on request (any key), e.g. after opening the port late.
  if (Serial.available() > 0) {
    while (Serial.available() > 0) Serial.read();
    print_headers();
  }
}
