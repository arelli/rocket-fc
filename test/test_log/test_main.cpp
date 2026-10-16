// Onboard log ring buffer, CSV formatting, and the firmware's real
// configuration (default settings, fc/settings.h) passing validation.
#include <string.h>
#include <unity.h>

#include "fc/csv.h"
#include "fc/frame_ring.h"
#include "fc/telemetry.h"
#include "fc/settings.h"

using namespace fc;

void setUp() {}
void tearDown() {}

static void test_ring_order_and_overwrite() {
  uint8_t mem[FrameRing::kSlot * 4];
  FrameRing r;
  r.attach(mem, sizeof(mem));
  TEST_ASSERT_EQUAL(4, r.capacity());
  for (uint8_t i = 1; i <= 6; ++i) {
    uint8_t f[3] = {i, static_cast<uint8_t>(i * 2), static_cast<uint8_t>(i * 3)};
    TEST_ASSERT_TRUE(r.push(f, sizeof(f)));
  }
  TEST_ASSERT_EQUAL(4, r.count());
  TEST_ASSERT_EQUAL(2, r.overwritten());
  uint8_t out[8];
  for (size_t i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL(3, r.get(i, out, sizeof(out)));
    TEST_ASSERT_EQUAL(i + 3, out[0]);  // frames 1 and 2 were overwritten
  }
  TEST_ASSERT_EQUAL(0, r.get(4, out, sizeof(out)));
}

static void test_ring_rejects() {
  FrameRing r;
  uint8_t f[1] = {0};
  TEST_ASSERT_FALSE(r.push(f, 1));  // no memory
  uint8_t mem[FrameRing::kSlot * 2];
  r.attach(mem, sizeof(mem));
  uint8_t big[FrameRing::kSlot] = {};
  TEST_ASSERT_FALSE(r.push(big, sizeof(big)));
  TEST_ASSERT_TRUE(r.push(big, tlm::kMaxFrameLen));
}

static void test_csv_roundtrip() {
  tlm::Telemetry t;
  t.state = FlightState::Coast;
  t.seq = 7;
  t.t_ms = 12345;
  t.alt_m = 123.45f;
  t.vel_mps = -1.5f;
  t.batt_v = 3.912f;
  t.flags = tlm::kFlagBaroOk | tlm::kFlagPyroArmed;
  uint8_t frame[tlm::kMaxFrameLen];
  const size_t n = tlm::encode(t, frame, sizeof(frame));
  TEST_ASSERT_EQUAL(tlm::kTelemetryLen, n);
  char line[csv::kLineMax];
  TEST_ASSERT_TRUE(csv::format_frame(frame, n, line, sizeof(line)) > 0);
  TEST_ASSERT_EQUAL_STRING_LEN("T,7,12345,COAST,123.45,-1.50,", line, 29);
  TEST_ASSERT_NOT_NULL(strstr(line, ",3.912,0x22"));

  tlm::Event e;
  e.state = FlightState::Descent;
  e.seq = 8;
  e.t_ms = 20000;
  e.code = EventCode::PyroFire;
  e.arg = 17470;
  const size_t m = tlm::encode(e, frame, sizeof(frame));
  TEST_ASSERT_TRUE(csv::format_frame(frame, m, line, sizeof(line)) > 0);
  TEST_ASSERT_EQUAL_STRING("E,8,20000,DESCENT,PYRO_FIRE,17470", line);

  frame[3] ^= 1;  // corrupt
  TEST_ASSERT_EQUAL(0, csv::format_frame(frame, m, line, sizeof(line)));
  TEST_ASSERT_EQUAL_STRING("", line);
}

static void test_csv_extremes_fit() {
  tlm::Telemetry t;
  t.seq = 65535;
  t.t_ms = 4294967295u;
  t.alt_m = -21474836.0f;
  t.vel_mps = -327.0f;
  t.accel_g = {-32.0f, -32.0f, -32.0f};
  t.gyro_dps = {-3276.0f, -3276.0f, -3276.0f};
  t.mag_uT = {-3276.0f, -3276.0f, -3276.0f};
  t.pressure_pa = 4294967295.0f;
  t.temp_c = -327.0f;
  t.batt_v = 65.535f;
  char line[csv::kLineMax];
  TEST_ASSERT_TRUE(csv::format(t, line, sizeof(line)) > 0);
}

static void test_firmware_config_valid() {
  // The default settings (fc/settings.h) must pass every check.
  const Settings st;
  char why[160];
  TEST_ASSERT_TRUE_MESSAGE(validate(st, why, sizeof(why)), why);
  const FlightComputer::Config c = flight_computer_config(st);
  TEST_ASSERT_NULL(validate(c.flight));
  TEST_ASSERT_TRUE(c.calib_ms < c.settle_ms);
  TEST_ASSERT_TRUE(st.pyro_pulse_ms > 0 && st.pyro_pulse_ms <= 2000);
  const BatteryMonitor::Params b = battery_params(st);
  TEST_ASSERT_TRUE(b.critical_v < b.low_v);
  TEST_ASSERT_TRUE(st.tlm_flight_ms >= 100);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ring_order_and_overwrite);
  RUN_TEST(test_ring_rejects);
  RUN_TEST(test_csv_roundtrip);
  RUN_TEST(test_csv_extremes_fit);
  RUN_TEST(test_firmware_config_valid);
  return UNITY_END();
}
