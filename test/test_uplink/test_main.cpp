// Ground -> rocket heartbeat: frame format, rocket link state, ground reply policy.
#include <unity.h>

#include "fc/csv.h"
#include "fc/radio_budget.h"
#include "fc/remote_fire.h"
#include "fc/telemetry.h"
#include "fc/time_sync.h"
#include "fc/uplink.h"

using namespace fc;

void setUp() {}
void tearDown() {}

static void test_uplink_roundtrip() {
  tlm::Uplink u;
  u.seq = 4321;
  u.t_ms = 987654;
  u.beep_period_s = 30;
  u.utc_ms = 1792161234567ull;
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
  TEST_ASSERT_TRUE(out.utc_ms == 1792161234567ull);
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

static void test_bench_mode_uplink() {
  tlm::Uplink u;
  u.kind = tlm::UplinkKind::BenchMode;
  u.pulse_ms = 1;
  u.code = bench_mode_code(true, 0x6B1D4F37u);
  uint8_t buf[32];
  const size_t n = tlm::encode(u, buf, sizeof(buf));
  tlm::Uplink out;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(buf, n, &out));
  TEST_ASSERT_EQUAL(tlm::UplinkKind::BenchMode, out.kind);
  TEST_ASSERT_EQUAL_UINT16(1, out.pulse_ms);
  TEST_ASSERT_EQUAL_HEX32(bench_mode_code(true, 0x6B1D4F37u), out.code);
  // Codes differ by direction and by pairing key.
  TEST_ASSERT_TRUE(bench_mode_code(true, 1) != bench_mode_code(false, 1));
  TEST_ASSERT_TRUE(bench_mode_code(true, 1) != bench_mode_code(true, 2));
}

static void test_telemetry_link_fields_roundtrip() {
  tlm::Telemetry t;
  t.link = tlm::kLinkGroundHeard | tlm::kLinkBenchMode;
  t.uplink_rssi_dbm = -97;
  t.beep_period_s = 15;
  uint8_t buf[64];
  const size_t n = tlm::encode(t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT(tlm::kTelemetryLen, n);
  tlm::Telemetry out;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(buf, n, &out));
  TEST_ASSERT_EQUAL_UINT8(tlm::kLinkGroundHeard | tlm::kLinkBenchMode, out.link);
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
  // Ground: at most one 22-byte heartbeat per 900 ms stays far below 10 %.
  LoraModulation m;
  const uint32_t us = lora_airtime_us(m, tlm::kUplinkLen);
  TEST_ASSERT_TRUE(us < 32000);
  TEST_ASSERT_TRUE(us * 1000.0 / 900.0 / 1e6 < 0.04);
}

static void test_time_sync() {
  TimeSync ts;
  TEST_ASSERT_FALSE(ts.synced());
  TEST_ASSERT_TRUE(ts.utc_ms(1000) == 0);
  TEST_ASSERT_FALSE(ts.apply(0, 1000));             // ground station has no time yet
  TEST_ASSERT_FALSE(ts.apply(1000000ull, 1000));     // 1970: implausible
  // Received at rocket time 12,345 ms; real time 2026-10-16 14:33:54.567 UTC.
  TEST_ASSERT_TRUE(ts.apply(1792161234567ull, 12345));
  TEST_ASSERT_TRUE(ts.synced());
  TEST_ASSERT_TRUE(ts.utc_ms(12345) == 1792161234567ull);
  TEST_ASSERT_TRUE(ts.utc_ms(13345) == 1792161235567ull);
  // Later syncs are ignored: no jumps within a log.
  TEST_ASSERT_FALSE(ts.apply(1792161299999ull, 20000));
  TEST_ASSERT_TRUE(ts.utc_ms(12345) == 1792161234567ull);
  // Event point: next whole second, and the conversion formula reproduces it.
  uint32_t t_ms;
  int32_t unix_s;
  ts.event_point(12345, &t_ms, &unix_s);
  TEST_ASSERT_EQUAL_INT32(1792161235, unix_s);
  TEST_ASSERT_EQUAL_UINT32(12345 + 433, t_ms);
  // utc_ms = arg * 1000 + (t - event_t) for any t
  const uint32_t t = 99999;
  TEST_ASSERT_TRUE(static_cast<uint64_t>(unix_s) * 1000 + (t - t_ms) == ts.utc_ms(t));
}

static void test_ping_and_eject_uplinks() {
  const tlm::UplinkKind kinds[] = {tlm::UplinkKind::Ping, tlm::UplinkKind::EjectArm,
                                   tlm::UplinkKind::Eject};
  for (const auto k : kinds) {
    tlm::Uplink u;
    u.kind = k;
    u.pulse_ms = 1234;
    u.code = 0xA5A5A5A5u;
    uint8_t buf[32];
    const size_t n = tlm::encode(u, buf, sizeof(buf));
    tlm::Uplink out;
    TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(buf, n, &out));
    TEST_ASSERT_EQUAL(k, out.kind);
    TEST_ASSERT_EQUAL_UINT16(1234, out.pulse_ms);
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, out.code);
  }
  tlm::Uplink u;
  u.kind = static_cast<tlm::UplinkKind>(9);  // unknown kind
  uint8_t buf[32];
  const size_t n = tlm::encode(u, buf, sizeof(buf));
  tlm::Uplink out;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::BadType, tlm::decode(buf, n, &out));
  TEST_ASSERT_EQUAL_STRING("PONG", to_string(EventCode::Pong));
  TEST_ASSERT_EQUAL_STRING("MANUAL_EJECT", to_string(EventCode::ManualEject));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_uplink_roundtrip);
  RUN_TEST(test_bench_mode_uplink);
  RUN_TEST(test_telemetry_link_fields_roundtrip);
  RUN_TEST(test_ground_link_timeout);
  RUN_TEST(test_uplink_policy_rate_and_flight);
  RUN_TEST(test_uplink_airtime_budget);
  RUN_TEST(test_time_sync);
  RUN_TEST(test_ping_and_eject_uplinks);
  return UNITY_END();
}
