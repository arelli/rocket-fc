// The "param" console command: listing, editing, refusing in flight, saving.
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "fc/param_shell.h"

using namespace fc;

void setUp() {}
void tearDown() {}

namespace {
struct Capture {
  std::vector<std::string> lines;
  std::string stored;
  int stores = 0;
  bool store_ok = true;
};
void out(void* ctx, const char* line) { static_cast<Capture*>(ctx)->lines.push_back(line); }
bool store(void* ctx, const char* text, size_t len) {
  Capture* c = static_cast<Capture*>(ctx);
  ++c->stores;
  c->stored.assign(text, len);
  return c->store_ok;
}
int count_prefix(const Capture& c, const char* p) {
  int n = 0;
  for (const auto& l : c.lines) n += l.rfind(p, 0) == 0 ? 1 : 0;
  return n;
}
void run(ParamShell& sh, Capture& c, std::vector<const char*> args, bool allowed = true) {
  c.lines.clear();
  sh.exec(static_cast<int>(args.size()), args.data(), allowed, out, store, &c);
}
}  // namespace

static void test_list_and_line_format() {
  ParamShell sh(false);
  sh.init(Settings{}, Settings{});
  Capture c;
  run(sh, c, {});
  int flight = 0;  // the flight computer lists everything but the ground station settings
  for (size_t i = 0; i < param_count(); ++i) flight += (param_def(i).flags & kParamGround) ? 0 : 1;
  TEST_ASSERT_EQUAL_INT(flight, count_prefix(c, "P,"));
  TEST_ASSERT_TRUE(flight < static_cast<int>(param_count()));
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "PE,"));
  TEST_ASSERT_TRUE(c.lines.back().find(",ok") != std::string::npos);
  // P,key,group,type,value,running,default,min,max,unit,flags,options,help
  char line[400];
  sh.format_line(*find_param("imu_up_axis"), line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING(
      "P,imu_up_axis,mount,enum,+X,+X,+X,0,5,,S,+X|-X|+Y|-Y|+Z|-Z,IMU axis that points to the nose. Check: "
      "rocket nose-up, 'status' shows accel_up about +1.00 g.",
      line);
  sh.format_line(*find_param("rf_key"), line, sizeof(line));
  TEST_ASSERT_EQUAL_INT(0, strncmp("P,rf_key,radio,u32,0x6B1D4F37,0x6B1D4F37,0x6B1D4F37,0,4294967295,,RH,,", line, 70));
  run(sh, c, {"list", "deploy"});
  TEST_ASSERT_EQUAL_INT(9, count_prefix(c, "P,"));
  run(sh, c, {"get", "gs_buzzer_on"});
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "PERR,gs_buzzer_on,unknown"));
}

static void test_set_save_and_refusals() {
  ParamShell sh(false);
  sh.init(Settings{}, Settings{});
  Capture c;
  run(sh, c, {"set", "backup_deploy_ms", "9000"});
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "P,backup_deploy_ms,deploy,u32,9000,0,0,"));
  run(sh, c, {"set", "backup_deploy_ms", "1000"});  // in range, but invalid with the lockout
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "P,"));
  TEST_ASSERT_TRUE(c.lines.back().find("invalid: backup_deploy_ms") != std::string::npos);
  run(sh, c, {"save"});
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "PERR,*,not saved"));
  TEST_ASSERT_EQUAL_INT(0, c.stores);
  run(sh, c, {"set", "backup_deploy_ms", "9000"});
  run(sh, c, {"set", "pyro_pulse_ms", "5000"});
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "PERR,pyro_pulse_ms,out of range"));
  run(sh, c, {"set", "nonsense", "1"});
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "PERR,nonsense,unknown"));
  run(sh, c, {"set", "launch_alt_m", "40"}, false);  // in flight
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "PERR,*,refused: in flight"));
  TEST_ASSERT_EQUAL_FLOAT(30.0f, sh.edit().launch_alt_m);
  run(sh, c, {"save"});
  TEST_ASSERT_EQUAL_INT(1, c.stores);
  TEST_ASSERT_EQUAL_STRING("backup_deploy_ms=9000\n", c.stored.c_str());
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "PSAVED,"));
  TEST_ASSERT_EQUAL_UINT32(9000, sh.saved().backup_deploy_ms);
  TEST_ASSERT_EQUAL_UINT32(0, sh.running().backup_deploy_ms);  // until reboot
  // revert and reset
  run(sh, c, {"set", "launch_alt_m", "40"});
  run(sh, c, {"revert"});
  TEST_ASSERT_EQUAL_FLOAT(30.0f, sh.edit().launch_alt_m);
  run(sh, c, {"reset", "all"});
  TEST_ASSERT_EQUAL_UINT32(0, sh.edit().backup_deploy_ms);
  run(sh, c, {"save"});
  TEST_ASSERT_EQUAL_STRING("", c.stored.c_str());  // all defaults: entry erased
  c.store_ok = false;
  run(sh, c, {"set", "launch_alt_m", "40"});
  run(sh, c, {"save"});
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "PERR,*,flash write failed"));
}

static void test_radio_only_shell() {
  ParamShell sh(true);
  sh.init(Settings{}, Settings{});
  Capture c;
  run(sh, c, {"list"});
  int shared = 0;
  for (size_t i = 0; i < param_count(); ++i)
    shared += (param_def(i).flags & (kParamShared | kParamGround)) ? 1 : 0;
  TEST_ASSERT_EQUAL_INT(shared, count_prefix(c, "P,"));
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "P,gs_buzzer_on,station,bool,1,1,1,0,1,,G,"));
  run(sh, c, {"set", "gs_search_lo_dbm", "-40"});  // above gs_search_hi_dbm
  run(sh, c, {"save"});
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "PERR,*,not saved"));
  run(sh, c, {"reset", "gs_search_lo_dbm"});
  run(sh, c, {"set", "launch_alt_m", "40"});
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "PERR,launch_alt_m,unknown"));
  run(sh, c, {"set", "radio_freq_mhz", "915"});
  run(sh, c, {"save"});
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "PERR,*,not saved: radio outside"));
  run(sh, c, {"set", "radio_unlock", "1"});
  run(sh, c, {"save"});
  TEST_ASSERT_EQUAL_INT(1, count_prefix(c, "PSAVED,"));
  TEST_ASSERT_EQUAL_STRING("radio_freq_mhz=915\nradio_unlock=1\n", c.stored.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_list_and_line_format);
  RUN_TEST(test_set_save_and_refusals);
  RUN_TEST(test_radio_only_shell);
  return UNITY_END();
}
