// Wire format: CRC check value, frame sizes, round trips, corruption detection.
#include <unity.h>

#include <string.h>

#include "fc/crc16.h"
#include "fc/telemetry.h"

using namespace fc;

void setUp() {}
void tearDown() {}

static void test_crc_check_value() {
  const char* s = "123456789";
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_ccitt(reinterpret_cast<const uint8_t*>(s), 9));
}

static tlm::Telemetry sample_tlm() {
  tlm::Telemetry t;
  t.state = FlightState::Coast;
  t.seq = 0xBEEF;
  t.t_ms = 123456789;
  t.alt_m = 456.78f;
  t.vel_mps = -12.34f;
  t.accel_g = {1.234f, -0.5f, 15.9f};
  t.gyro_dps = {123.4f, -1999.9f, 0.1f};
  t.mag_uT = {45.6f, -12.3f, 0.0f};
  t.pressure_pa = 95432.0f;
  t.temp_c = -5.25f;
  t.batt_v = 3.987f;
  t.flags = tlm::kFlagPyroArmed | tlm::kFlagBaroOk | tlm::kFlagImuOk;
  t.q = Quat{0.5f, -0.5f, 0.5f, -0.5f};
  return t;
}

static void test_telemetry_roundtrip() {
  uint8_t buf[64];
  const tlm::Telemetry in = sample_tlm();
  const size_t n = tlm::encode(in, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT(tlm::kTelemetryLen, n);

  tlm::FrameType type;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::frame_type(buf, n, &type));
  TEST_ASSERT_EQUAL(tlm::FrameType::Telemetry, type);

  tlm::Telemetry out;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(buf, n, &out));
  TEST_ASSERT_EQUAL(in.state, out.state);
  TEST_ASSERT_EQUAL_UINT16(in.seq, out.seq);
  TEST_ASSERT_EQUAL_UINT32(in.t_ms, out.t_ms);
  TEST_ASSERT_FLOAT_WITHIN(0.006f, in.alt_m, out.alt_m);
  TEST_ASSERT_FLOAT_WITHIN(0.006f, in.vel_mps, out.vel_mps);
  TEST_ASSERT_FLOAT_WITHIN(0.0006f, in.accel_g.x, out.accel_g.x);
  TEST_ASSERT_FLOAT_WITHIN(0.0006f, in.accel_g.z, out.accel_g.z);
  TEST_ASSERT_FLOAT_WITHIN(0.06f, in.gyro_dps.y, out.gyro_dps.y);
  TEST_ASSERT_FLOAT_WITHIN(0.06f, in.mag_uT.x, out.mag_uT.x);
  TEST_ASSERT_FLOAT_WITHIN(0.6f, in.pressure_pa, out.pressure_pa);
  TEST_ASSERT_FLOAT_WITHIN(0.006f, in.temp_c, out.temp_c);
  TEST_ASSERT_FLOAT_WITHIN(0.0006f, in.batt_v, out.batt_v);
  TEST_ASSERT_EQUAL_HEX8(in.flags, out.flags);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, in.q.w, out.q.w);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, in.q.x, out.q.x);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, in.q.y, out.q.y);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, in.q.z, out.q.z);
}

static void test_event_roundtrip() {
  uint8_t buf[32];
  tlm::Event e;
  e.state = FlightState::Descent;
  e.seq = 7;
  e.t_ms = 99000;
  e.code = EventCode::PyroFire;
  e.arg = -123456;
  const size_t n = tlm::encode(e, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT(tlm::kEventLen, n);
  tlm::Event out;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(buf, n, &out));
  TEST_ASSERT_EQUAL(e.state, out.state);
  TEST_ASSERT_EQUAL(e.code, out.code);
  TEST_ASSERT_EQUAL_INT32(e.arg, out.arg);
  TEST_ASSERT_EQUAL_UINT32(e.t_ms, out.t_ms);
}

static void test_every_single_bit_flip_is_detected() {
  uint8_t buf[64];
  const size_t n = tlm::encode(sample_tlm(), buf, sizeof(buf));
  for (size_t byte = 0; byte < n; ++byte) {
    for (int bit = 0; bit < 8; ++bit) {
      uint8_t c[64];
      memcpy(c, buf, n);
      c[byte] ^= static_cast<uint8_t>(1 << bit);
      tlm::Telemetry out;
      TEST_ASSERT_NOT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(c, n, &out));
    }
  }
}

static void test_rejects_wrong_type_and_length() {
  uint8_t buf[64];
  const size_t n = tlm::encode(sample_tlm(), buf, sizeof(buf));
  tlm::Event e;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::BadType, tlm::decode(buf, n, &e));
  tlm::Telemetry t;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::BadLength, tlm::decode(buf, n - 1, &t));
  TEST_ASSERT_EQUAL(tlm::DecodeResult::TooShort, tlm::decode(buf, 5, &t));
}

static void test_encode_fails_cleanly_when_buffer_too_small() {
  uint8_t buf[20];
  TEST_ASSERT_EQUAL_UINT(0, tlm::encode(sample_tlm(), buf, sizeof(buf)));
}

static void test_values_saturate_instead_of_wrapping() {
  uint8_t buf[64];
  tlm::Telemetry t = sample_tlm();
  t.accel_g.x = 100.0f;   // beyond +-32.767 g field range
  t.vel_mps = -1000.0f;   // beyond +-327 m/s
  const size_t n = tlm::encode(t, buf, sizeof(buf));
  tlm::Telemetry out;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(buf, n, &out));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 32.767f, out.accel_g.x);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -327.68f, out.vel_mps);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc_check_value);
  RUN_TEST(test_telemetry_roundtrip);
  RUN_TEST(test_event_roundtrip);
  RUN_TEST(test_every_single_bit_flip_is_detected);
  RUN_TEST(test_rejects_wrong_type_and_length);
  RUN_TEST(test_encode_fails_cleanly_when_buffer_too_small);
  RUN_TEST(test_values_saturate_instead_of_wrapping);
  return UNITY_END();
}
