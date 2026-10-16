// Ground -> rocket heartbeat: frame format, rocket link state, ground reply policy.
#include <unity.h>

#include "fc/csv.h"
#include "fc/radio_budget.h"
#include "fc/telemetry.h"
#include "fc/uplink.h"

using namespace fc;

void setUp() {}
void tearDown() {}

static void test_uplink_roundtrip() {
  tlm::Uplink u;
  u.seq = 4321;
  u.t_ms = 987654;
  u.beep_period_s = 30;
  uint8_t buf[32];
  const size_t n = tlm::encode(u, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT(tlm::kUplinkLen, n);
  tlm::FrameType type;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::frame_type(buf, n, &type));
  TEST_ASSERT_EQUAL(tlm::FrameType::Uplink, type);
  tlm::Uplink out;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(buf, n, &out));
  TEST_ASSERT_EQUAL_UINT16(4321, out.seq);
  TEST_ASSERT_EQUAL_UINT32(987654, out.t_ms);
  TEST_ASSERT_EQUAL_UINT8(30, out.beep_period_s);
  // Not mistaken for rocket frames.
  tlm::Telemetry t;
  tlm::Event e;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::BadType, tlm::decode(buf, n, &t));
  TEST_ASSERT_EQUAL(tlm::DecodeResult::BadType, tlm::decode(buf, n, &e));
  char line[csv::kLineMax];
  TEST_ASSERT_EQUAL(0, csv::format_frame(buf, n, line, sizeof(line)));
  // Corruption is detected.
  buf[11] ^= 0x01;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::BadCrc, tlm::decode(buf, n, &out));
}

static void test_telemetry_link_fields_roundtrip() {
  tlm::Telemetry t;
  t.link = tlm::kLinkGroundHeard;
  t.uplink_rssi_dbm = -97;
  t.beep_period_s = 15;
  uint8_t buf[64];
  const size_t n = tlm::encode(t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT(tlm::kTelemetryLen, n);
  tlm::Telemetry out;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(buf, n, &out));
  TEST_ASSERT_EQUAL_UINT8(tlm::kLinkGroundHeard, out.link);
  TEST_ASSERT_EQUAL_INT8(-97, out.uplink_rssi_dbm);
  TEST_ASSERT_EQUAL_UINT8(15, out.beep_period_s);
}

static void test_ground_link_timeout() {
  GroundLink g(5000);
  TEST_ASSERT_FALSE(g.connected(0));
  TEST_ASSERT_EQUAL_UINT8(0, g.pad_beep_period_s(0));
  TEST_ASSERT_EQUAL_INT8(-128, g.rssi_dbm(0));
  g.heard(1000, -80, 10);
  TEST_ASSERT_TRUE(g.connected(1000));
  TEST_ASSERT_TRUE(g.connected(5999));
  TEST_ASSERT_EQUAL_UINT8(10, g.pad_beep_period_s(3000));
  TEST_ASSERT_EQUAL_INT8(-80, g.rssi_dbm(3000));
  TEST_ASSERT_FALSE(g.connected(6000));
  TEST_ASSERT_EQUAL_UINT8(0, g.pad_beep_period_s(6000));
  g.heard(7000, -90, 0);  // ground asks for silence
  TEST_ASSERT_TRUE(g.connected(7000));
  TEST_ASSERT_EQUAL_UINT8(0, g.pad_beep_period_s(7000));
}

static void test_uplink_policy_rate_and_flight() {
  UplinkPolicy p(900);
  TEST_ASSERT_TRUE(p.on_rocket_frame(FlightState::Pad, 0));
  TEST_ASSERT_FALSE(p.on_rocket_frame(FlightState::Pad, 500));  // rate limit
  TEST_ASSERT_TRUE(p.on_rocket_frame(FlightState::Pad, 1000));
  TEST_ASSERT_FALSE(p.on_rocket_frame(FlightState::Boost, 3000));
  TEST_ASSERT_FALSE(p.on_rocket_frame(FlightState::Coast, 4000));
  TEST_ASSERT_FALSE(p.on_rocket_frame(FlightState::Descent, 5000));
  TEST_ASSERT_TRUE(p.on_rocket_frame(FlightState::Landed, 6000));
  TEST_ASSERT_TRUE(p.on_rocket_frame(FlightState::Fault, 7000));
  p.set_enabled(false);
  TEST_ASSERT_FALSE(p.on_rocket_frame(FlightState::Pad, 9000));
}

static void test_uplink_airtime_budget() {
  // Ground: at most one 14-byte heartbeat per 900 ms stays far below 10 %.
  LoraModulation m;
  const uint32_t us = lora_airtime_us(m, tlm::kUplinkLen);
  TEST_ASSERT_TRUE(us < 30000);
  TEST_ASSERT_TRUE(us * 1000.0 / 900.0 / 1e6 < 0.04);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_uplink_roundtrip);
  RUN_TEST(test_telemetry_link_fields_roundtrip);
  RUN_TEST(test_ground_link_timeout);
  RUN_TEST(test_uplink_policy_rate_and_flight);
  RUN_TEST(test_uplink_airtime_budget);
  return UNITY_END();
}
