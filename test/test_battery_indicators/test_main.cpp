// Battery model/supervision and LED/buzzer pattern timing.
#include <unity.h>

#include "fc/battery.h"
#include "fc/indicators.h"

using namespace fc;

void setUp() {}
void tearDown() {}

static void test_percent_curve() {
  TEST_ASSERT_EQUAL_FLOAT(100.0f, liion_percent(4.25f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, liion_percent(3.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.0f, liion_percent(3.80f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 53.5f, liion_percent(3.85f));
}

static void test_monitor_ignores_short_sags() {
  BatteryMonitor m;
  m.update(3.9f, 0);
  TEST_ASSERT_EQUAL(BatteryMonitor::Level::Ok, m.level());
  // 1 s sag (radio TX / pyro pulse) must not trigger low.
  for (uint32_t t = 100; t <= 1100; t += 100) m.update(3.3f, t);
  TEST_ASSERT_EQUAL(BatteryMonitor::Level::Ok, m.level());
  m.update(3.9f, 1200);
  TEST_ASSERT_EQUAL(BatteryMonitor::Level::Ok, m.level());
}

static void test_monitor_low_then_critical_with_hysteresis() {
  BatteryMonitor m;
  m.update(3.9f, 0);
  uint32_t t = 0;
  for (; t <= 4000; t += 100) m.update(3.55f, t);
  TEST_ASSERT_EQUAL(BatteryMonitor::Level::Low, m.level());
  // 3.62 V is above low (3.60) but inside the hysteresis band: stays Low.
  for (uint32_t end = t + 5000; t <= end; t += 100) m.update(3.62f, t);
  TEST_ASSERT_EQUAL(BatteryMonitor::Level::Low, m.level());
  for (uint32_t end = t + 4000; t <= end; t += 100) m.update(3.35f, t);
  TEST_ASSERT_EQUAL(BatteryMonitor::Level::Critical, m.level());
}

static int count_rising_edges(BeepSequencer& s, uint32_t start, uint32_t dur, uint32_t* on_ms) {
  int edges = 0;
  bool prev = false;
  *on_ms = 0;
  for (uint32_t t = start; t < start + dur; t += 10) {
    const bool on = s.update(t);
    if (on && !prev) ++edges;
    if (on) *on_ms += 10;
    prev = on;
  }
  return edges;
}

static void test_digits_beep_out_a_number() {
  BeepSequencer s;
  s.digits(152);  // 1 + 5 + 2 beeps
  uint32_t on;
  TEST_ASSERT_EQUAL_INT(8, count_rising_edges(s, 1000, 20000, &on));
  TEST_ASSERT_FALSE(s.busy());
}

static void test_zero_digit_is_one_long_beep() {
  BeepSequencer s;
  s.digits(105);  // 1, long, 5
  uint32_t on;
  TEST_ASSERT_EQUAL_INT(7, count_rising_edges(s, 0, 20000, &on));
  TEST_ASSERT_EQUAL_UINT32(6 * 150 + 600, on);
}

static void test_queue_overflow_is_reported() {
  BeepSequencer s;
  bool ok = true;
  for (int i = 0; i < 200; ++i) ok &= s.push(10, 10);
  TEST_ASSERT_FALSE(ok);
}

static void test_led_patterns() {
  TEST_ASSERT_TRUE(led_on(LedMode::Flight, 12345));
  TEST_ASSERT_FALSE(led_on(LedMode::Off, 12345));
  // PadReady: two 100 ms flashes per second.
  TEST_ASSERT_TRUE(led_on(LedMode::PadReady, 50));
  TEST_ASSERT_FALSE(led_on(LedMode::PadReady, 150));
  TEST_ASSERT_TRUE(led_on(LedMode::PadReady, 300));
  TEST_ASSERT_FALSE(led_on(LedMode::PadReady, 500));
  TEST_ASSERT_TRUE(led_on(LedMode::PadReady, 1050));
  // PadSafe: one flash per second.
  TEST_ASSERT_TRUE(led_on(LedMode::PadSafe, 2050));
  TEST_ASSERT_FALSE(led_on(LedMode::PadSafe, 2300));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_percent_curve);
  RUN_TEST(test_monitor_ignores_short_sags);
  RUN_TEST(test_monitor_low_then_critical_with_hysteresis);
  RUN_TEST(test_digits_beep_out_a_number);
  RUN_TEST(test_zero_digit_is_one_long_beep);
  RUN_TEST(test_queue_overflow_is_reported);
  RUN_TEST(test_led_patterns);
  return UNITY_END();
}
