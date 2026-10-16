// Remote test fire (bench build): arming, one-time code, window, refusals,
// and the uplink frames that carry it.
#include <unity.h>

#include "fc/remote_fire.h"
#include "fc/telemetry.h"

using namespace fc;

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t kKey = 0x5EC12E7u;
constexpr RemoteFireEnd kOk = RemoteFireEnd::Fired;
}  // namespace

static void test_arm_then_fire_once() {
  RemoteFire rf(kKey);
  TEST_ASSERT_EQUAL(kOk, rf.arm(1000, 500, 0xDEADBEEF, kOk));
  TEST_ASSERT_TRUE(rf.armed());
  const uint32_t code = remote_fire_code(0xDEADBEEF, kKey);
  TEST_ASSERT_EQUAL(kOk, rf.fire(5000, code, 500, kOk));
  TEST_ASSERT_FALSE(rf.armed());
  // Replaying the same fire frame does nothing.
  TEST_ASSERT_EQUAL(RemoteFireEnd::NotArmed, rf.fire(5100, code, 500, kOk));
}

static void test_fire_without_arming_refused() {
  RemoteFire rf(kKey);
  TEST_ASSERT_EQUAL(RemoteFireEnd::NotArmed, rf.fire(0, remote_fire_code(0, kKey), 500, kOk));
}

static void test_wrong_code_disarms() {
  RemoteFire rf(kKey);
  rf.arm(0, 500, 1234, kOk);
  TEST_ASSERT_EQUAL(RemoteFireEnd::BadCode, rf.fire(100, remote_fire_code(1234, kKey + 1), 500, kOk));
  TEST_ASSERT_FALSE(rf.armed());
  // ...and the right code afterwards no longer works.
  TEST_ASSERT_EQUAL(RemoteFireEnd::NotArmed, rf.fire(200, remote_fire_code(1234, kKey), 500, kOk));
}

static void test_code_is_bound_to_nonce() {
  RemoteFire rf(kKey);
  rf.arm(0, 500, 1111, kOk);
  TEST_ASSERT_EQUAL(RemoteFireEnd::BadCode, rf.fire(100, remote_fire_code(2222, kKey), 500, kOk));
  TEST_ASSERT_NOT_EQUAL(remote_fire_code(1111, kKey), remote_fire_code(1112, kKey));
}

static void test_window_expires() {
  RemoteFire rf(kKey);
  rf.arm(1000, 500, 77, kOk);
  TEST_ASSERT_FALSE(rf.expired(1000 + RemoteFire::kWindowMs - 1));
  TEST_ASSERT_TRUE(rf.expired(1000 + RemoteFire::kWindowMs));
  TEST_ASSERT_FALSE(rf.armed());
  rf.arm(1000, 500, 77, kOk);
  TEST_ASSERT_EQUAL(RemoteFireEnd::Timeout,
                    rf.fire(1000 + RemoteFire::kWindowMs, remote_fire_code(77, kKey), 500, kOk));
}

static void test_conditions_and_pulse() {
  RemoteFire rf(kKey);
  TEST_ASSERT_EQUAL(RemoteFireEnd::NotBenchBuild, rf.arm(0, 500, 1, RemoteFireEnd::NotBenchBuild));
  TEST_ASSERT_EQUAL(RemoteFireEnd::NotOnPad, rf.arm(0, 500, 1, RemoteFireEnd::NotOnPad));
  TEST_ASSERT_EQUAL(RemoteFireEnd::NoContinuity, rf.arm(0, 500, 1, RemoteFireEnd::NoContinuity));
  TEST_ASSERT_FALSE(rf.armed());
  TEST_ASSERT_EQUAL(RemoteFireEnd::BadPulse, rf.arm(0, 0, 1, kOk));
  TEST_ASSERT_EQUAL(RemoteFireEnd::BadPulse, rf.arm(0, 2001, 1, kOk));
  // Conditions lost between arm and fire (e.g. arm link pulled): refused.
  rf.arm(0, 500, 9, kOk);
  TEST_ASSERT_EQUAL(RemoteFireEnd::NoContinuity,
                    rf.fire(100, remote_fire_code(9, kKey), 500, RemoteFireEnd::NoContinuity));
  TEST_ASSERT_FALSE(rf.armed());
  // Pulse length must match what was armed.
  rf.arm(0, 500, 9, kOk);
  TEST_ASSERT_EQUAL(RemoteFireEnd::BadPulse, rf.fire(100, remote_fire_code(9, kKey), 2000, kOk));
}

static void test_uplink_command_frames() {
  const tlm::UplinkKind kinds[] = {tlm::UplinkKind::FireArm, tlm::UplinkKind::Fire,
                                   tlm::UplinkKind::FireDisarm};
  for (tlm::UplinkKind kind : kinds) {
    tlm::Uplink u;
    u.kind = kind;
    u.code = 0xCAFEF00D;
    u.pulse_ms = 750;
    uint8_t buf[32];
    const size_t n = tlm::encode(u, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(tlm::kUplinkLen, n);  // all kinds have the same length
    tlm::Uplink out;
    TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(buf, n, &out));
    TEST_ASSERT_EQUAL(kind, out.kind);
    TEST_ASSERT_EQUAL_HEX32(0xCAFEF00D, out.code);
    TEST_ASSERT_EQUAL_UINT16(750, out.pulse_ms);
  }
  // Unknown kinds are rejected.
  tlm::Uplink u;
  uint8_t buf[32];
  const size_t n = tlm::encode(u, buf, sizeof(buf));
  buf[10] = 9;  // kind
  tlm::Uplink out;
  TEST_ASSERT_NOT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(buf, n, &out));  // CRC or kind
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_arm_then_fire_once);
  RUN_TEST(test_fire_without_arming_refused);
  RUN_TEST(test_wrong_code_disarms);
  RUN_TEST(test_code_is_bound_to_nonce);
  RUN_TEST(test_window_expires);
  RUN_TEST(test_conditions_and_pulse);
  RUN_TEST(test_uplink_command_frames);
  return UNITY_END();
}
