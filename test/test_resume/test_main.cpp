// When may a boot resume a stored flight?
#include <unity.h>

#include "fc/resume.h"

using namespace fc;

void setUp() {}
void tearDown() {}

static FlightRecord rec(FlightState s) {
  FlightRecord r;
  r.state = static_cast<uint8_t>(s);
  r.fired = 0;
  r.ground_pa = 100000.0f;
  r.max_alt_m = 120.0f;
  r.ms_since_launch = 4200;
  r.seal();
  return r;
}

static void test_power_on_never_resumes() {
  TEST_ASSERT_FALSE(should_resume(rec(FlightState::Coast), ResetCause::PowerOn));
}

static void test_watchdog_panic_brownout_resume_in_flight() {
  TEST_ASSERT_TRUE(should_resume(rec(FlightState::Boost), ResetCause::Watchdog));
  TEST_ASSERT_TRUE(should_resume(rec(FlightState::Coast), ResetCause::Panic));
  TEST_ASSERT_TRUE(should_resume(rec(FlightState::Descent), ResetCause::Brownout));
  TEST_ASSERT_TRUE(should_resume(rec(FlightState::Landed), ResetCause::Software));
}

static void test_pad_and_init_start_fresh() {
  TEST_ASSERT_FALSE(should_resume(rec(FlightState::Pad), ResetCause::Watchdog));
  TEST_ASSERT_FALSE(should_resume(rec(FlightState::Init), ResetCause::Watchdog));
  TEST_ASSERT_FALSE(should_resume(rec(FlightState::Fault), ResetCause::Watchdog));
}

static void test_corrupted_record_is_ignored() {
  FlightRecord r = rec(FlightState::Coast);
  r.max_alt_m = 999.0f;  // changed after sealing
  TEST_ASSERT_FALSE(r.valid());
  TEST_ASSERT_FALSE(should_resume(r, ResetCause::Watchdog));
  FlightRecord garbage;  // never sealed (fresh RTC RAM)
  TEST_ASSERT_FALSE(should_resume(garbage, ResetCause::Watchdog));
}

static void test_implausible_ground_pressure_is_ignored() {
  FlightRecord r = rec(FlightState::Coast);
  r.ground_pa = 0.0f;
  r.seal();
  TEST_ASSERT_FALSE(should_resume(r, ResetCause::Watchdog));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_power_on_never_resumes);
  RUN_TEST(test_watchdog_panic_brownout_resume_in_flight);
  RUN_TEST(test_pad_and_init_start_fresh);
  RUN_TEST(test_corrupted_record_is_ignored);
  RUN_TEST(test_implausible_ground_pressure_is_ignored);
  return UNITY_END();
}
