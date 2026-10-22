// Ground station panel: button decoding and the search mode beep rate.
#include <unity.h>

#include "fc/station.h"

using namespace fc;

void setUp() {}
void tearDown() {}

namespace {
using A = PanelButton::Action;
// Holds the level from t0 to t1 (1 ms steps); counts the actions.
struct Run {
  int shorts = 0, longs = 0;
  uint32_t long_at = 0;
};
void hold(PanelButton& b, bool level, uint32_t t0, uint32_t t1, uint32_t long_ms, Run& r) {
  for (uint32_t t = t0; t < t1; ++t) {
    const A a = b.update(level, t, long_ms);
    if (a == A::Short) ++r.shorts;
    if (a == A::Long) {
      ++r.longs;
      r.long_at = t;
    }
  }
}
}  // namespace

static void test_short_press() {
  PanelButton b;
  Run r;
  hold(b, false, 0, 100, 1500, r);
  hold(b, true, 100, 300, 1500, r);
  TEST_ASSERT_TRUE(b.pressed());
  hold(b, false, 300, 500, 1500, r);
  TEST_ASSERT_EQUAL_INT(1, r.shorts);
  TEST_ASSERT_EQUAL_INT(0, r.longs);
}

static void test_bounce_is_ignored() {
  PanelButton b;
  Run r;
  hold(b, false, 0, 100, 1500, r);
  for (uint32_t t = 100; t < 120; t += 4) {  // contact bounce, shorter than the debounce time
    hold(b, true, t, t + 2, 1500, r);
    hold(b, false, t + 2, t + 4, 1500, r);
  }
  hold(b, false, 120, 400, 1500, r);
  TEST_ASSERT_EQUAL_INT(0, r.shorts);
  TEST_ASSERT_FALSE(b.pressed());
}

static void test_long_hold_fires_once_while_held() {
  PanelButton b;
  Run r;
  hold(b, true, 0, 4000, 1500, r);
  TEST_ASSERT_EQUAL_INT(1, r.longs);
  TEST_ASSERT_UINT32_WITHIN(2, 1500, r.long_at);
  hold(b, false, 4000, 4200, 1500, r);
  TEST_ASSERT_EQUAL_INT(0, r.shorts);  // no short press after a long one
}

static void test_release_between_short_and_long_does_nothing() {
  PanelButton b;
  Run r;
  hold(b, true, 0, 1200, 3000, r);
  hold(b, false, 1200, 1400, 3000, r);
  TEST_ASSERT_EQUAL_INT(0, r.shorts);
  TEST_ASSERT_EQUAL_INT(0, r.longs);
  TEST_ASSERT_EQUAL_UINT32(0, b.held_ms(1400));
}

static void test_search_interval() {
  TEST_ASSERT_EQUAL_UINT32(SearchBeep::kSlowMs, SearchBeep::interval_ms(-130.0f, -125, -50));
  TEST_ASSERT_EQUAL_UINT32(SearchBeep::kSlowMs, SearchBeep::interval_ms(-125.0f, -125, -50));
  TEST_ASSERT_EQUAL_UINT32(SearchBeep::kFastMs, SearchBeep::interval_ms(-50.0f, -125, -50));
  TEST_ASSERT_EQUAL_UINT32(SearchBeep::kFastMs, SearchBeep::interval_ms(-20.0f, -125, -50));
  TEST_ASSERT_EQUAL_UINT32(790, SearchBeep::interval_ms(-87.5f, -125, -50));  // midpoint
  // Stronger signal = faster, monotonic.
  uint32_t prev = SearchBeep::kSlowMs + 1;
  for (int r = -125; r <= -50; ++r) {
    const uint32_t i = SearchBeep::interval_ms(static_cast<float>(r), -125, -50);
    TEST_ASSERT_TRUE(i <= prev);
    prev = i;
  }
  TEST_ASSERT_EQUAL_UINT32(SearchBeep::kSlowMs, SearchBeep::interval_ms(-80.0f, -50, -50));  // bad range
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_short_press);
  RUN_TEST(test_bounce_is_ignored);
  RUN_TEST(test_long_hold_fires_once_while_held);
  RUN_TEST(test_release_between_short_and_long_does_nothing);
  RUN_TEST(test_search_interval);
  return UNITY_END();
}
