// LoRa time-on-air against Semtech's calculator, and duty-cycle compliance:
// a sender that transmits as often as it is allowed must never exceed 10 %
// airtime in ANY one-hour window.
#include <unity.h>

#include <vector>

#include "fc/radio_budget.h"
#include "fc/telemetry.h"

using namespace fc;

void setUp() {}
void tearDown() {}

static void test_airtime_sf7_bw125_matches_semtech_calculator() {
  LoraModulation m;
  m.sf = 7;
  m.bw_hz = 125000;
  m.cr_denom = 5;
  m.preamble = 8;
  // Semtech LoRa calculator: 10 bytes, SF7, 125 kHz, CR4/5, explicit header,
  // CRC on, 8 preamble symbols -> 41.22 ms.
  TEST_ASSERT_UINT32_WITHIN(5, 41216, lora_airtime_us(m, 10));
}

static void test_airtime_sf12_uses_low_data_rate_optimization() {
  LoraModulation m;
  m.sf = 12;
  m.bw_hz = 125000;
  m.cr_denom = 5;
  m.preamble = 8;
  // Semtech calculator with LDRO on: 991.23 ms.
  TEST_ASSERT_UINT32_WITHIN(5, 991232, lora_airtime_us(m, 10));
}

static void test_flight_telemetry_frame_airtime() {
  LoraModulation m;  // project default: SF7, 250 kHz, CR4/5
  const uint32_t us = lora_airtime_us(m, tlm::kTelemetryLen);
  TEST_ASSERT_UINT32_WITHIN(5, 46208, us);
}

struct Tx {
  uint32_t t_ms;
  uint32_t air_us;
};

static uint64_t max_window_airtime_us(const std::vector<Tx>& tx, uint32_t window_ms) {
  uint64_t best = 0, sum = 0;
  size_t lo = 0;
  for (size_t hi = 0; hi < tx.size(); ++hi) {
    sum += tx[hi].air_us;
    while (tx[hi].t_ms - tx[lo].t_ms >= window_ms) {
      sum -= tx[lo].air_us;
      ++lo;
    }
    if (sum > best) best = sum;
  }
  return best;
}

static void test_greedy_sender_never_exceeds_10_percent_per_hour() {
  LoraModulation m;
  const uint32_t air = lora_airtime_us(m, tlm::kTelemetryLen);
  // Project defaults: 8 % long-term rate + 72 s burst = at most 360 s per hour.
  DutyCycleLimiter lim(0.08f, 72000);
  std::vector<Tx> tx;
  const uint32_t three_hours = 3u * 3600u * 1000u;
  uint32_t busy_until = 0;
  for (uint32_t t = 0; t < three_hours; t += 5) {
    if (t < busy_until) continue;  // radio still transmitting
    if (lim.try_consume(t, air)) {
      tx.push_back({t, air});
      busy_until = t + air / 1000 + 1;
    }
  }
  const uint64_t worst = max_window_airtime_us(tx, 3600u * 1000u);
  TEST_ASSERT_TRUE(worst <= 360ull * 1000000ull);
  // ...while still being useful: the long-term average is close to 8 %.
  TEST_ASSERT_TRUE(worst >= 300ull * 1000000ull);
}

static void test_burst_allows_fast_telemetry_during_a_flight() {
  LoraModulation m;
  const uint32_t air = lora_airtime_us(m, tlm::kTelemetryLen);
  DutyCycleLimiter lim(0.08f, 72000);
  lim.reset(0);
  // Pad: 1 Hz for 30 minutes (well under the refill rate).
  uint32_t t = 0;
  for (; t < 30u * 60u * 1000u; t += 1000) TEST_ASSERT_TRUE(lim.try_consume(t, air));
  // Flight: 10 Hz for 3 minutes must all be allowed.
  for (uint32_t end = t + 180000; t < end; t += 100) TEST_ASSERT_TRUE(lim.try_consume(t, air));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_airtime_sf7_bw125_matches_semtech_calculator);
  RUN_TEST(test_airtime_sf12_uses_low_data_rate_optimization);
  RUN_TEST(test_flight_telemetry_frame_airtime);
  RUN_TEST(test_greedy_sender_never_exceeds_10_percent_per_hour);
  RUN_TEST(test_burst_allows_fast_telemetry_during_a_flight);
  return UNITY_END();
}
