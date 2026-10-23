// Ground station launch channel: when the igniter may and may not fire.
#include <unity.h>

#include "fc/launch.h"
#include "fc/remote_fire.h"
#include "fc/telemetry.h"
#include "fc/crc16.h"
#include <string.h>

using namespace fc;

void setUp() {}
void tearDown() {}

namespace {
LaunchInputs ready() {
  LaunchInputs in;
  in.armed = in.continuity = in.rocket_ready = true;
  return in;
}
// Runs 1 ms steps; returns total gate-on ms and the first end reason.
struct Run {
  uint32_t gate_ms = 0, first_gate = 0;
  int ticks = 0;
  LaunchEnd ended = LaunchEnd::None;
};
Run run(LaunchControl& lc, uint32_t t0, uint32_t t1, const LaunchInputs& in, uint32_t pulse = 1000) {
  Run r;
  for (uint32_t t = t0; t < t1; ++t) {
    const auto o = lc.update(t, in, pulse);
    if (o.gate) {
      if (!r.gate_ms) r.first_gate = t;
      ++r.gate_ms;
    }
    if (o.tick_s >= 0) ++r.ticks;
    if (o.ended != LaunchEnd::None && r.ended == LaunchEnd::None) r.ended = o.ended;
  }
  return r;
}
}  // namespace

static void test_refusals() {
  LaunchControl lc;
  LaunchInputs in = ready();
  in.armed = false;
  TEST_ASSERT_EQUAL(LaunchEnd::NotArmed, lc.start(0, in, 3000));
  in = ready();
  in.continuity = false;
  TEST_ASSERT_EQUAL(LaunchEnd::NoContinuity, lc.start(0, in, 3000));
  in = ready();
  in.rocket_ready = false;
  TEST_ASSERT_EQUAL(LaunchEnd::RocketNotReady, lc.start(0, in, 3000));
  in.need_pad = false;  // setting off: allowed
  TEST_ASSERT_EQUAL(LaunchEnd::None, lc.start(0, in, 3000));
  TEST_ASSERT_EQUAL(LaunchEnd::Busy, lc.start(10, in, 3000));
  LaunchControl b;
  in = ready();
  in.rocket_ready = false;
  in.bench = true;  // BENCH: igniter test without the rocket
  TEST_ASSERT_EQUAL(LaunchEnd::None, b.start(0, in, 0));
}

static void test_countdown_then_fire_once() {
  LaunchControl lc;
  const LaunchInputs in = ready();
  TEST_ASSERT_EQUAL(LaunchEnd::None, lc.start(0, in, 3000));
  const Run r = run(lc, 0, 10000, in, 2000);
  TEST_ASSERT_EQUAL_INT(3, r.ticks);
  TEST_ASSERT_EQUAL_UINT32(3000, r.first_gate);
  TEST_ASSERT_EQUAL_UINT32(2000, r.gate_ms);
  TEST_ASSERT_EQUAL(LaunchEnd::Fired, r.ended);
  TEST_ASSERT_EQUAL(LaunchState::Idle, lc.state());
}

static void test_abort_and_lost_conditions() {
  LaunchControl lc;
  LaunchInputs in = ready();
  lc.start(0, in, 3000);
  run(lc, 0, 1500, in);
  lc.abort();
  Run r = run(lc, 1500, 6000, in);
  TEST_ASSERT_EQUAL_UINT32(0, r.gate_ms);
  TEST_ASSERT_EQUAL(LaunchEnd::Aborted, r.ended);

  lc.start(10000, in, 3000);
  run(lc, 10000, 11000, in);
  in.armed = false;  // ARM switch off during the countdown
  r = run(lc, 11000, 15000, in);
  TEST_ASSERT_EQUAL_UINT32(0, r.gate_ms);
  TEST_ASSERT_EQUAL(LaunchEnd::NotArmed, r.ended);

  in = ready();
  lc.start(20000, in, 3000);
  in.rocket_ready = false;  // rocket lost / left PAD
  r = run(lc, 20000, 25000, in);
  TEST_ASSERT_EQUAL_UINT32(0, r.gate_ms);
  TEST_ASSERT_EQUAL(LaunchEnd::RocketNotReady, r.ended);
}

static void test_arm_off_cuts_the_pulse() {
  LaunchControl lc;
  LaunchInputs in = ready();
  lc.start(0, in, 0);
  Run r = run(lc, 0, 300, in, 2000);
  TEST_ASSERT_EQUAL_UINT32(300, r.gate_ms);
  in.armed = false;
  r = run(lc, 300, 3000, in, 2000);
  TEST_ASSERT_EQUAL_UINT32(0, r.gate_ms);
  TEST_ASSERT_EQUAL(LaunchEnd::Fired, r.ended);
}

static void test_burn_through_does_not_stop_the_pulse() {
  LaunchControl lc;
  LaunchInputs in = ready();
  lc.start(0, in, 0);
  run(lc, 0, 100, in, 1000);
  in.continuity = false;  // the bridge wire burned through
  const Run r = run(lc, 100, 2000, in, 1000);
  TEST_ASSERT_EQUAL_UINT32(900, r.gate_ms);
}

static void test_text_frames() {
  tlm::Text t;
  t.cmd = true;
  t.seq = 7;
  const char* cmd = "param set launch_alt_m 40";
  t.len = static_cast<uint8_t>(strlen(cmd));
  memcpy(t.text, cmd, t.len);
  t.code = text_code(t.seq, crc16_ccitt(reinterpret_cast<const uint8_t*>(cmd), t.len), 0x6B1D4F37u);
  uint8_t buf[tlm::kMaxTextFrameLen];
  const size_t n = tlm::encode(t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(tlm::text_frame_len(true, t.len), n);
  tlm::Text o;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(buf, n, &o));
  TEST_ASSERT_TRUE(o.cmd);
  TEST_ASSERT_EQUAL_STRING(cmd, o.text);
  TEST_ASSERT_EQUAL_HEX32(t.code, o.code);
  TEST_ASSERT_TRUE(text_code(7, 1, 1) != text_code(8, 1, 1));
  TEST_ASSERT_TRUE(text_code(7, 1, 1) != text_code(7, 1, 2));
  // Output frame, longest text.
  tlm::Text out;
  out.len = tlm::kMaxText;
  memset(out.text, 'x', out.len);
  const size_t m = tlm::encode(out, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(tlm::text_frame_len(false, tlm::kMaxText), m);
  TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(buf, m, &o));
  TEST_ASSERT_FALSE(o.cmd);
  TEST_ASSERT_EQUAL_UINT8(tlm::kMaxText, o.len);
  buf[20] ^= 1;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::BadCrc, tlm::decode(buf, m, &o));
  // Too long a command is not encoded.
  t.len = tlm::kMaxCmdText + 1;
  TEST_ASSERT_EQUAL(0, tlm::encode(t, buf, sizeof(buf)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_refusals);
  RUN_TEST(test_countdown_then_fire_once);
  RUN_TEST(test_abort_and_lost_conditions);
  RUN_TEST(test_arm_off_cuts_the_pulse);
  RUN_TEST(test_burn_through_does_not_stop_the_pulse);
  RUN_TEST(test_text_frames);
  return UNITY_END();
}
