// Settings table, text storage format, validation and radio rules.
#include <string.h>
#include <unity.h>

#include "fc/settings.h"

using namespace fc;

void setUp() {}
void tearDown() {}

static void test_table_is_consistent() {
  const Settings s;
  TEST_ASSERT_TRUE(param_count() > 50);
  for (size_t i = 0; i < param_count(); ++i) {
    const ParamDef& d = param_def(i);
    TEST_ASSERT_TRUE_MESSAGE(strlen(d.key) > 0 && strlen(d.key) < 32, d.key);
    TEST_ASSERT_NULL_MESSAGE(strchr(d.key, ','), d.key);
    TEST_ASSERT_NULL_MESSAGE(strchr(d.help, '\n'), d.key);
    TEST_ASSERT_TRUE_MESSAGE(d.min <= d.def && d.def <= d.max, d.key);
    TEST_ASSERT_TRUE_MESSAGE(param_value(s, d) == d.def || d.type == ParamType::F32, d.key);
    for (size_t j = 0; j < i; ++j) TEST_ASSERT_TRUE_MESSAGE(strcmp(d.key, param_def(j).key) != 0, d.key);
    if (d.type == ParamType::Enum) {
      size_t n = 1;
      for (const char* p = d.options; *p; ++p) n += (*p == '|');
      TEST_ASSERT_EQUAL_MESSAGE(static_cast<size_t>(d.max) + 1, n, d.key);
    }
    TEST_ASSERT_EQUAL_PTR(&d, find_param(d.key));
  }
  TEST_ASSERT_NULL(find_param("no_such_setting"));
}

static void test_rf_key_default_is_detected() {
  fc::Settings s;
  TEST_ASSERT_TRUE(fc::rf_key_is_default(s));
  s.rf_key ^= 1;
  TEST_ASSERT_FALSE(fc::rf_key_is_default(s));
}

static void test_defaults_valid_and_match_flight_logic_defaults() {
  const Settings s;
  char why[160];
  TEST_ASSERT_TRUE_MESSAGE(validate(s, why, sizeof(why)), why);
  TEST_ASSERT_TRUE(radio_within_eu_rules(s));
  // The defaults are the values the flight logic is tested with.
  const FlightComputer::Config c = flight_computer_config(s);
  const FlightConfig f;
  TEST_ASSERT_EQUAL_FLOAT(f.launch_accel_g, c.flight.launch_accel_g);
  TEST_ASSERT_EQUAL_UINT32(f.launch_accel_ms, c.flight.launch_accel_ms);
  TEST_ASSERT_EQUAL_FLOAT(f.launch_alt_m, c.flight.launch_alt_m);
  TEST_ASSERT_EQUAL_UINT32(f.launch_alt_ms, c.flight.launch_alt_ms);
  TEST_ASSERT_EQUAL_FLOAT(f.burnout_accel_g, c.flight.burnout_accel_g);
  TEST_ASSERT_EQUAL_UINT32(f.apogee_lockout_ms, c.flight.apogee_lockout_ms);
  TEST_ASSERT_EQUAL_UINT32(f.apogee_confirm_ms, c.flight.apogee_confirm_ms);
  TEST_ASSERT_EQUAL_FLOAT(f.apogee_drop_m, c.flight.apogee_drop_m);
  TEST_ASSERT_EQUAL_FLOAT(f.min_deploy_alt_m, c.flight.min_deploy_alt_m);
  TEST_ASSERT_EQUAL_UINT32(0, c.flight.deploy_delay_ms);
  TEST_ASSERT_EQUAL_UINT32(0, c.flight.backup_deploy_ms);
  TEST_ASSERT_EQUAL_UINT32(f.landed_ms, c.flight.landed_ms);
  TEST_ASSERT_EQUAL_FLOAT(f.landed_window_m, c.flight.landed_window_m);
  const AltitudeFilter::Params af;
  TEST_ASSERT_EQUAL_FLOAT(af.baro_sigma_m, c.estimator.filter.baro_sigma_m);
  TEST_ASSERT_EQUAL_FLOAT(af.accel_sigma_mps2, c.estimator.filter.accel_sigma_mps2);
  TEST_ASSERT_EQUAL_FLOAT(af.no_imu_sigma_mps2, c.estimator.filter.no_imu_sigma_mps2);
  TEST_ASSERT_EQUAL(Axis::PosX, c.estimator.up_axis);
  TEST_ASSERT_EQUAL_UINT32(5000, c.settle_ms);
  TEST_ASSERT_EQUAL_UINT32(2000, c.calib_ms);
  const BatteryMonitor::Params b = battery_params(s), bd;
  TEST_ASSERT_EQUAL_FLOAT(bd.low_v, b.low_v);
  TEST_ASSERT_EQUAL_FLOAT(bd.critical_v, b.critical_v);
  const Ahrs::Params a = ahrs_params(s), ad;
  TEST_ASSERT_EQUAL_FLOAT(ad.kp, a.kp);
  TEST_ASSERT_EQUAL_FLOAT(ad.accel_gate_g, a.accel_gate_g);
  const LoraModulation m = lora_modulation(s);
  TEST_ASSERT_EQUAL_UINT8(7, m.sf);
  TEST_ASSERT_EQUAL_UINT32(250000, m.bw_hz);
}

static void test_set_param_types_and_ranges() {
  Settings s;
  TEST_ASSERT_NULL(set_param(s, *find_param("launch_accel_g"), "3.5"));
  TEST_ASSERT_EQUAL_FLOAT(3.5f, s.launch_accel_g);
  TEST_ASSERT_NOT_NULL(set_param(s, *find_param("launch_accel_g"), "1.0"));  // below 1.5
  TEST_ASSERT_EQUAL_FLOAT(3.5f, s.launch_accel_g);                          // unchanged
  TEST_ASSERT_NOT_NULL(set_param(s, *find_param("launch_accel_g"), "abc"));
  TEST_ASSERT_NOT_NULL(set_param(s, *find_param("launch_accel_g"), "3.5x"));
  TEST_ASSERT_NOT_NULL(set_param(s, *find_param("launch_accel_g"), "nan"));
  TEST_ASSERT_NOT_NULL(set_param(s, *find_param("pyro_pulse_ms"), "-5"));
  TEST_ASSERT_NOT_NULL(set_param(s, *find_param("pyro_pulse_ms"), "2001"));
  TEST_ASSERT_NULL(set_param(s, *find_param("pyro_pulse_ms"), "1500"));
  TEST_ASSERT_EQUAL_UINT32(1500, s.pyro_pulse_ms);
  TEST_ASSERT_NULL(set_param(s, *find_param("radio_power_dbm"), "-9"));
  TEST_ASSERT_EQUAL_INT32(-9, s.radio_power_dbm);
  TEST_ASSERT_NOT_NULL(set_param(s, *find_param("radio_power_dbm"), "23"));
  TEST_ASSERT_NULL(set_param(s, *find_param("buzzer_on"), "off"));
  TEST_ASSERT_FALSE(s.buzzer_on);
  TEST_ASSERT_NOT_NULL(set_param(s, *find_param("buzzer_on"), "2"));
  TEST_ASSERT_NULL(set_param(s, *find_param("imu_up_axis"), "-Z"));
  TEST_ASSERT_EQUAL(Axis::NegZ, up_axis(s));
  TEST_ASSERT_NULL(set_param(s, *find_param("imu_up_axis"), "2"));
  TEST_ASSERT_EQUAL(Axis::PosY, up_axis(s));
  TEST_ASSERT_NOT_NULL(set_param(s, *find_param("imu_up_axis"), "6"));
  TEST_ASSERT_NOT_NULL(set_param(s, *find_param("imu_up_axis"), "up"));
  TEST_ASSERT_NULL(set_param(s, *find_param("rf_key"), "0xDEADBEEF"));
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, s.rf_key);
  char buf[32];
  format_param(s, *find_param("rf_key"), buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0xDEADBEEF", buf);
  format_param(s, *find_param("radio_bw"), buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("250", buf);
  TEST_ASSERT_NULL(set_param(s, *find_param("radio_bw"), "62.5"));
  TEST_ASSERT_EQUAL_FLOAT(62.5f, radio_bw_khz(s));
  reset_param(s, *find_param("radio_bw"));
  TEST_ASSERT_EQUAL_FLOAT(250.0f, radio_bw_khz(s));
}

static void test_text_roundtrip_only_changed() {
  Settings a;
  TEST_ASSERT_NULL(set_param(a, *find_param("backup_deploy_ms"), "9000"));
  TEST_ASSERT_NULL(set_param(a, *find_param("imu_up_axis"), "+Z"));
  TEST_ASSERT_NULL(set_param(a, *find_param("batt_divider"), "2.037"));
  TEST_ASSERT_NULL(set_param(a, *find_param("pad_beeps"), "always"));
  char text[4096];
  const size_t n = settings_to_text(a, true, text, sizeof(text));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_STRING("imu_up_axis=+Z\nbackup_deploy_ms=9000\nbatt_divider=2.037\npad_beeps=always\n", text);
  Settings b;
  LoadReport r;
  settings_from_text(b, text, n, &r);
  TEST_ASSERT_EQUAL_UINT16(4, r.applied);
  TEST_ASSERT_EQUAL_UINT16(0, r.errors);
  TEST_ASSERT_EQUAL_HEX32(settings_crc(a), settings_crc(b));
  TEST_ASSERT_TRUE(settings_crc(a) != settings_crc(Settings{}));

  // All values, then back.
  const size_t m = settings_to_text(a, false, text, sizeof(text));
  TEST_ASSERT_TRUE(m > n);
  Settings c;
  settings_from_text(c, text, m, &r);
  TEST_ASSERT_EQUAL_HEX32(settings_crc(a), settings_crc(c));
  TEST_ASSERT_EQUAL_UINT(0, settings_to_text(a, false, text, 64));  // does not fit
}

static void test_text_tolerates_old_and_bad_entries() {
  const char* t = "# comment\r\n\n  launch_alt_m = 50 \r\nold_setting=1\nlaunch_accel_g=0.2\ngarbage\n";
  Settings s;
  LoadReport r;
  settings_from_text(s, t, strlen(t), &r);
  TEST_ASSERT_EQUAL_FLOAT(50.0f, s.launch_alt_m);
  TEST_ASSERT_EQUAL_FLOAT(2.5f, s.launch_accel_g);  // bad value ignored
  TEST_ASSERT_EQUAL_UINT16(1, r.applied);
  TEST_ASSERT_EQUAL_UINT16(1, r.unknown);
  TEST_ASSERT_EQUAL_UINT16(2, r.errors);
  TEST_ASSERT_TRUE(strlen(r.first_problem) > 0);
}

static void test_cross_checks() {
  char why[160];
  Settings s;
  s.calib_ms = 6000;
  TEST_ASSERT_FALSE(validate(s, why, sizeof(why)));
  s = Settings{};
  s.burnout_accel_g = 3.0f;
  TEST_ASSERT_FALSE(validate(s, why, sizeof(why)));
  s = Settings{};
  s.batt_crit_v = 3.7f;
  TEST_ASSERT_FALSE(validate(s, why, sizeof(why)));
  s = Settings{};
  s.backup_deploy_ms = 1000;  // shorter than the apogee lockout (flight logic check)
  TEST_ASSERT_FALSE(validate(s, why, sizeof(why)));
  s = Settings{};
  s.pyro_pulse_ms = 5000;  // out of range even if set directly
  TEST_ASSERT_FALSE(validate(s, why, sizeof(why)));
}

static void test_radio_rules() {
  char why[160];
  Settings s;
  TEST_ASSERT_NULL(set_param(s, *find_param("radio_freq_mhz"), "915"));
  TEST_ASSERT_FALSE(validate_radio(s, why, sizeof(why)));
  TEST_ASSERT_FALSE(validate(s, why, sizeof(why)));
  s.radio_unlock = true;
  TEST_ASSERT_TRUE(validate(s, why, sizeof(why)));

  s = Settings{};
  s.radio_bw = 3;  // 500 kHz does not fit in the 250 kHz band
  TEST_ASSERT_FALSE(radio_within_eu_rules(s));
  s.radio_bw = 1;  // 125 kHz at 869.525: fits
  TEST_ASSERT_TRUE(radio_within_eu_rules(s));
  s.radio_freq_mhz = 869.6f;  // 869.5375..869.6625: over the top edge
  TEST_ASSERT_FALSE(radio_within_eu_rules(s));

  s = Settings{};
  s.radio_duty_pct = 10.0f;  // + 72 s burst = 432 s > 360 s per hour
  TEST_ASSERT_FALSE(radio_within_eu_rules(s));
  s.radio_burst_s = 0;
  TEST_ASSERT_TRUE(radio_within_eu_rules(s));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_table_is_consistent);
  RUN_TEST(test_defaults_valid_and_match_flight_logic_defaults);
  RUN_TEST(test_rf_key_default_is_detected);
  RUN_TEST(test_set_param_types_and_ranges);
  RUN_TEST(test_text_roundtrip_only_changed);
  RUN_TEST(test_text_tolerates_old_and_bad_entries);
  RUN_TEST(test_cross_checks);
  RUN_TEST(test_radio_rules);
  return UNITY_END();
}
