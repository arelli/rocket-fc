#include "fc/radio_budget.h"

#include <math.h>

namespace fc {

uint32_t lora_airtime_us(const LoraModulation& m, size_t payload_len) {
  const double tsym_us = static_cast<double>(1UL << m.sf) * 1.0e6 / static_cast<double>(m.bw_hz);
  const int de = (tsym_us > 16000.0) ? 1 : 0;  // low data rate optimization
  const int ih = m.explicit_header ? 0 : 1;
  const int crc = m.crc ? 1 : 0;
  const double num = 8.0 * static_cast<double>(payload_len) - 4.0 * m.sf + 28.0 + 16.0 * crc - 20.0 * ih;
  const double den = 4.0 * (m.sf - 2 * de);
  double extra = ceil(num / den) * m.cr_denom;
  if (extra < 0) extra = 0;
  const double n_payload = 8.0 + extra;
  const double t_us = (m.preamble + 4.25 + n_payload) * tsym_us;
  return static_cast<uint32_t>(ceil(t_us));
}

// Internally the refill rate is kept as "microseconds of airtime earned per
// millisecond of wall time" scaled by 1000 to avoid floats in the hot path:
// earned_us = elapsed_ms * rate_ppm_ / 1000, with rate_ppm_ = fraction * 1e6.
DutyCycleLimiter::DutyCycleLimiter(float fraction, uint32_t capacity_ms)
    : rate_ppm_(static_cast<uint64_t>(fraction * 1.0e6f + 0.5f)),
      capacity_us_(static_cast<uint64_t>(capacity_ms) * 1000ULL),
      tokens_us_(static_cast<uint64_t>(capacity_ms) * 1000ULL) {}

void DutyCycleLimiter::reset(uint32_t now_ms) {
  tokens_us_ = capacity_us_;
  last_ms_ = now_ms;
  started_ = true;
}

void DutyCycleLimiter::refill(uint32_t now_ms) {
  if (!started_) {
    reset(now_ms);
    return;
  }
  const uint64_t elapsed_ms = static_cast<uint32_t>(now_ms - last_ms_);
  last_ms_ = now_ms;
  tokens_us_ += elapsed_ms * rate_ppm_ / 1000ULL;
  if (tokens_us_ > capacity_us_) tokens_us_ = capacity_us_;
}

bool DutyCycleLimiter::try_consume(uint32_t now_ms, uint32_t airtime_us) {
  refill(now_ms);
  if (tokens_us_ < airtime_us) return false;
  tokens_us_ -= airtime_us;
  return true;
}

uint64_t DutyCycleLimiter::available_us(uint32_t now_ms) {
  refill(now_ms);
  return tokens_us_;
}

}  // namespace fc
