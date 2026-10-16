// LoRa time-on-air and a duty-cycle limiter that keeps transmissions legal.
//
// EU 868 MHz rules (ERC REC 70-03, band 54, 869.40-869.65 MHz) allow a 10 %
// duty cycle measured over any 1-hour window. A token bucket with refill rate
// r and capacity C bounds the airtime in ANY window of length T by C + r*T,
// so choosing C + r*3600 s <= 360 s guarantees compliance while still letting
// the rocket burst at a high packet rate during the (short) flight.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fc {

struct LoraModulation {
  uint8_t sf = 7;           // spreading factor 7..12
  uint32_t bw_hz = 250000;  // bandwidth
  uint8_t cr_denom = 5;     // coding rate 4/5..4/8 -> 5..8
  uint16_t preamble = 8;    // preamble symbols
  bool explicit_header = true;
  bool crc = true;
};

// Time on air in microseconds (Semtech AN1200.13 / SX1262 datasheet 6.1.4),
// valid for SF7..SF12. Low-data-rate optimization is enabled automatically
// when the symbol time exceeds 16 ms, as RadioLib does.
uint32_t lora_airtime_us(const LoraModulation& m, size_t payload_len);

class DutyCycleLimiter {
 public:
  // fraction: long-term average duty cycle allowed (e.g. 0.08).
  // capacity_ms: burst budget (airtime that may be spent back-to-back).
  DutyCycleLimiter(float fraction, uint32_t capacity_ms);

  // Starts with a full bucket.
  void reset(uint32_t now_ms);

  // If the budget allows `airtime_us` now, consume it and return true.
  bool try_consume(uint32_t now_ms, uint32_t airtime_us);

  // Remaining burst budget [us] (for telemetry/diagnostics).
  uint64_t available_us(uint32_t now_ms);

 private:
  void refill(uint32_t now_ms);

  uint64_t rate_ppm_;  // duty fraction in parts per million (0.08 -> 80000)
  uint64_t capacity_us_;
  uint64_t tokens_us_;
  uint32_t last_ms_ = 0;
  bool started_ = false;
};

}  // namespace fc
