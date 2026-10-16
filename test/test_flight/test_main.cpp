// End-to-end tests of the decision pipeline (estimator + state machine)
// against simulated flights with sensor noise and injected failures.
//
// These are the tests that matter most: they encode, in executable form, when
// the deployment charge may and may not fire. Run them after ANY change to
// lib/fc_core or include/flight_config.h:   pio test -e native
#include <unity.h>

#include <vector>

#include "fc/flight_computer.h"
#include "fc/sim.h"

using namespace fc;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kDt = 10;  // 100 Hz control loop, as on the hardware

struct TimedEvent {
  uint32_t t;
  EventCode code;
  int32_t arg;
};

struct Faults {
  int64_t baro_dead_after_launch_ms = -1;  // baro stops answering N ms after ignition
  bool imu_dead = false;
  bool selftest_ok = true;
};

struct Result {
  std::vector<TimedEvent> events;
  int fires = 0;
  uint32_t fire_t = 0;
  EventCode fire_code = EventCode::Boot;
  bool armed_on_pad = false;  // must never happen
  FlightState final_state = FlightState::Init;
  uint32_t ignition_t = 0;
  uint32_t apogee_t = 0;
  float apogee_alt = 0;
  float max_alt_reported = 0;

  const TimedEvent* find(EventCode c) const {
    for (const auto& e : events)
      if (e.code == c) return &e;
    return nullptr;
  }
};

FlightComputer::Config default_config() {
  FlightComputer::Config c;
  c.estimator.up_axis = Axis::PosX;  // the simulator's nose axis
  return c;
}

SimParams nominal_sim() {
  SimParams p;
  p.ignition_ms = 20000;
  return p;
}

}  // namespace

// Runs the flight computer against the simulator until `until_ms` (or until
// both agree the rocket has landed). Can be called repeatedly to continue a
// flight after changing the simulation (do_init = false).
static void fly(FlightComputer& fc, SimFlight& sim, const SimParams& sp, const Faults& f,
                uint32_t until_ms, Result& r, bool do_init = true) {
  r.ignition_t = sp.ignition_ms;
  if (do_init) fc.begin(sim.t_ms());
  while (sim.t_ms() < until_ms) {
    const SimSample s = sim.step(kDt);
    if (fc.state() == FlightState::Init && fc.settle_done(s.t_ms)) fc.finish_init(f.selftest_ok);

    bool baro_ok = true;
    if (f.baro_dead_after_launch_ms >= 0 &&
        s.t_ms >= sp.ignition_ms + static_cast<uint32_t>(f.baro_dead_after_launch_ms))
      baro_ok = false;
    const bool imu_ok = !f.imu_dead;

    const SmOutput out = fc.tick(s.t_ms, baro_ok, s.pressure_pa, imu_ok, s.accel_g);
    for (size_t i = 0; i < out.n_events; ++i)
      r.events.push_back({s.t_ms, out.events[i].code, out.events[i].arg});
    if (out.fire) {
      ++r.fires;
      r.fire_t = s.t_ms;
      for (size_t i = 0; i < out.n_events; ++i) {
        const EventCode c = out.events[i].code;
        if (c == EventCode::PyroFire || c == EventCode::PyroBackupFire) r.fire_code = c;
      }
      sim.deploy();
    }
    if (fc.state() == FlightState::Pad && fc.sm().pyro_armed()) r.armed_on_pad = true;
    if (fc.state() == FlightState::Landed && sim.landed()) break;
  }
  r.final_state = fc.state();
  r.apogee_t = sim.apogee_ms();
  r.apogee_alt = sim.apogee_alt_m();
  r.max_alt_reported = fc.sm().max_alt_m();
}

static void assert_nominal_deploy(const Result& r, uint32_t lockout_ms) {
  TEST_ASSERT_FALSE(r.armed_on_pad);
  const TimedEvent* launch = r.find(EventCode::Launch);
  TEST_ASSERT_NOT_NULL(launch);
  TEST_ASSERT_TRUE(launch->t >= r.ignition_t);
  TEST_ASSERT_TRUE(launch->t <= r.ignition_t + 250);

  TEST_ASSERT_EQUAL_INT(1, r.fires);
  TEST_ASSERT_EQUAL(EventCode::PyroFire, r.fire_code);
  TEST_ASSERT_NOT_NULL(r.find(EventCode::Apogee));
  // Deploy close to true apogee: not meaningfully early, at most 1.5 s late.
  TEST_ASSERT_TRUE(r.fire_t + 300 >= r.apogee_t);
  TEST_ASSERT_TRUE(r.fire_t <= r.apogee_t + 1500);
  TEST_ASSERT_TRUE(r.fire_t >= launch->t + lockout_ms - 200);
  // Reported max altitude within 3 % (+1 m) of the truth.
  TEST_ASSERT_FLOAT_WITHIN(0.03f * r.apogee_alt + 1.0f, r.apogee_alt, r.max_alt_reported);

  TEST_ASSERT_EQUAL(FlightState::Landed, r.final_state);
  TEST_ASSERT_NOT_NULL(r.find(EventCode::Landed));
}

// ---------------------------------------------------------------------------

static void test_nominal_flight() {
  const SimParams sp = nominal_sim();
  FlightComputer::Config cfg = default_config();
  FlightComputer fc(cfg);
  SimFlight sim(sp);
  Result r;
  fly(fc, sim, sp, Faults{}, 200000, r);
  TEST_ASSERT_TRUE(r.apogee_alt > 50.0f);
  assert_nominal_deploy(r, cfg.flight.apogee_lockout_ms);
  TEST_ASSERT_NOT_NULL(r.find(EventCode::Burnout));
}

static void test_high_power_flight() {
  SimParams sp = nominal_sim();
  sp.thrust_accel_mps2 = 200.0f;
  sp.burn_s = 1.5f;
  sp.drag_k = 0.0004f;
  FlightComputer::Config cfg = default_config();
  FlightComputer fc(cfg);
  SimFlight sim(sp);
  Result r;
  fly(fc, sim, sp, Faults{}, 600000, r);
  TEST_ASSERT_TRUE(r.apogee_alt > 500.0f);
  assert_nominal_deploy(r, cfg.flight.apogee_lockout_ms);
}

static void test_many_noise_seeds() {
  // Same flight with 20 different noise realizations: deploy every time.
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    SimParams sp = nominal_sim();
    sp.seed = seed * 7919u;
    sp.baro_noise_m = 0.5f;  // noisier than a real BMP280 at our settings
    FlightComputer::Config cfg = default_config();
    FlightComputer fc(cfg);
    SimFlight sim(sp);
    Result r;
    fly(fc, sim, sp, Faults{}, 200000, r);
    assert_nominal_deploy(r, cfg.flight.apogee_lockout_ms);
  }
}

static void test_ten_minutes_on_the_pad_no_launch() {
  SimParams sp = nominal_sim();
  sp.ignition_ms = 0xFFFFFFF0u;  // never ignites
  sp.baro_noise_m = 0.6f;
  FlightComputer fc(default_config());
  SimFlight sim(sp);
  Result r;
  fly(fc, sim, sp, Faults{}, 10u * 60u * 1000u, r);
  TEST_ASSERT_EQUAL(FlightState::Pad, r.final_state);
  TEST_ASSERT_EQUAL_INT(0, r.fires);
  TEST_ASSERT_NULL(r.find(EventCode::Launch));
  TEST_ASSERT_FALSE(r.armed_on_pad);
}

static void test_knock_on_the_pad_is_not_a_launch() {
  SimParams sp = nominal_sim();
  sp.ignition_ms = 0xFFFFFFF0u;
  FlightComputer fc(default_config());
  SimFlight sim(sp);
  Result r;
  fly(fc, sim, sp, Faults{}, 10000, r);
  sim.add_accel_spike_g(6.0f);  // 60 ms knock, shorter than the 100 ms hold
  fly(fc, sim, sp, Faults{}, 10060, r, false);
  sim.add_accel_spike_g(0.0f);
  fly(fc, sim, sp, Faults{}, 30000, r, false);
  TEST_ASSERT_EQUAL(FlightState::Pad, r.final_state);
  TEST_ASSERT_EQUAL_INT(0, r.fires);
}

static void test_wind_gust_on_the_pad_is_not_a_launch() {
  SimParams sp = nominal_sim();
  sp.ignition_ms = 0xFFFFFFF0u;
  FlightComputer fc(default_config());
  SimFlight sim(sp);
  Result r;
  fly(fc, sim, sp, Faults{}, 10000, r);
  sim.add_pressure_offset_m(40.0f);  // 300 ms pressure transient, beyond launch_alt_m
  fly(fc, sim, sp, Faults{}, 10300, r, false);
  sim.add_pressure_offset_m(-40.0f);  // and the opposite swing
  fly(fc, sim, sp, Faults{}, 10600, r, false);
  sim.add_pressure_offset_m(0.0f);
  fly(fc, sim, sp, Faults{}, 40000, r, false);
  TEST_ASSERT_EQUAL(FlightState::Pad, r.final_state);
  TEST_ASSERT_EQUAL_INT(0, r.fires);
}

static void test_hop_below_min_deploy_altitude_does_not_fire() {
  SimParams sp = nominal_sim();
  sp.thrust_accel_mps2 = 40.0f;  // 4 g: detected as a launch...
  sp.burn_s = 0.2f;              // ...but only a ~2 m hop
  FlightComputer::Config cfg = default_config();
  FlightComputer fc(cfg);
  SimFlight sim(sp);
  Result r;
  fly(fc, sim, sp, Faults{}, 60000, r);
  TEST_ASSERT_NOT_NULL(r.find(EventCode::Launch));
  TEST_ASSERT_EQUAL_INT(0, r.fires);
  TEST_ASSERT_TRUE(r.find(EventCode::ApogeeNoDeploy) != nullptr ||
                   r.find(EventCode::Landed) != nullptr);
}

static void test_backup_timer_fires_when_baro_dies() {
  SimParams sp = nominal_sim();
  FlightComputer::Config cfg = default_config();
  cfg.flight.backup_deploy_ms = 8000;
  FlightComputer fc(cfg);
  SimFlight sim(sp);
  Faults f;
  f.baro_dead_after_launch_ms = 500;
  Result r;
  fly(fc, sim, sp, f, 60000, r);
  TEST_ASSERT_EQUAL_INT(1, r.fires);
  TEST_ASSERT_EQUAL(EventCode::PyroBackupFire, r.fire_code);
  const TimedEvent* launch = r.find(EventCode::Launch);
  TEST_ASSERT_NOT_NULL(launch);
  // Launch time is back-dated to the start of the acceleration (~ignition).
  TEST_ASSERT_UINT32_WITHIN(150, sp.ignition_ms + 8000, r.fire_t);
}

static void test_no_backup_timer_and_dead_baro_never_fires() {
  SimParams sp = nominal_sim();
  FlightComputer::Config cfg = default_config();
  cfg.flight.backup_deploy_ms = 0;
  FlightComputer fc(cfg);
  SimFlight sim(sp);
  Faults f;
  f.baro_dead_after_launch_ms = 500;
  Result r;
  fly(fc, sim, sp, f, 120000, r);
  TEST_ASSERT_EQUAL_INT(0, r.fires);
}

static void test_dead_imu_still_deploys_on_baro_alone() {
  SimParams sp = nominal_sim();
  FlightComputer::Config cfg = default_config();
  FlightComputer fc(cfg);
  SimFlight sim(sp);
  Faults f;
  f.imu_dead = true;
  Result r;
  fly(fc, sim, sp, f, 200000, r);
  TEST_ASSERT_EQUAL_INT(1, r.fires);
  TEST_ASSERT_EQUAL(EventCode::PyroFire, r.fire_code);
  TEST_ASSERT_TRUE(r.fire_t + 300 >= r.apogee_t);
  TEST_ASSERT_TRUE(r.fire_t <= r.apogee_t + 2000);
}

static void test_failed_selftest_never_arms_or_fires() {
  SimParams sp = nominal_sim();
  FlightComputer fc(default_config());
  SimFlight sim(sp);
  Faults f;
  f.selftest_ok = false;
  Result r;
  fly(fc, sim, sp, f, 120000, r);
  TEST_ASSERT_EQUAL(FlightState::Fault, r.final_state);
  TEST_ASSERT_EQUAL_INT(0, r.fires);
  TEST_ASSERT_NULL(r.find(EventCode::Launch));
}

static void test_short_fast_flight_respects_apogee_lockout() {
  SimParams sp = nominal_sim();
  sp.thrust_accel_mps2 = 80.0f;
  sp.burn_s = 0.15f;
  sp.drag_k = 0.004f;  // ~6 m hop, apogee ~1.2 s after ignition: before the lockout
  FlightComputer::Config cfg = default_config();
  cfg.flight.min_deploy_alt_m = 5.0f;
  FlightComputer fc(cfg);
  SimFlight sim(sp);
  Result r;
  fly(fc, sim, sp, Faults{}, 60000, r);
  const TimedEvent* launch = r.find(EventCode::Launch);
  TEST_ASSERT_NOT_NULL(launch);
  TEST_ASSERT_TRUE(r.apogee_t < launch->t + cfg.flight.apogee_lockout_ms);
  // Still deploys (it is above min_deploy_alt_m) but only after the lockout.
  TEST_ASSERT_EQUAL_INT(1, r.fires);
  TEST_ASSERT_TRUE(r.fire_t >= sp.ignition_ms + cfg.flight.apogee_lockout_ms);
}

static void test_reset_during_coast_resumes_and_deploys_once() {
  SimParams sp = nominal_sim();
  FlightComputer::Config cfg = default_config();
  FlightComputer fc(cfg);
  SimFlight sim(sp);
  Result r;
  // Fly until shortly after burnout.
  uint32_t t_burnout = 0;
  fc.begin(0);
  while (t_burnout == 0 && sim.t_ms() < 60000) {
    const SimSample s = sim.step(kDt);
    if (fc.settle_done(s.t_ms)) fc.finish_init(true);
    const SmOutput out = fc.tick(s.t_ms, true, s.pressure_pa, true, s.accel_g);
    TEST_ASSERT_FALSE(out.fire);
    if (fc.state() == FlightState::Coast) t_burnout = s.t_ms;
  }
  TEST_ASSERT_TRUE(t_burnout > 0);
  const FlightRecord rec = fc.record(sim.t_ms());

  // Watchdog reset: 300 ms of nothing, then a new boot with millis() at 0.
  sim.step(300);
  TEST_ASSERT_TRUE(should_resume(rec, ResetCause::Watchdog));
  FlightComputer fc2(cfg);
  const uint32_t offset = sim.t_ms();  // new boot's clock starts here
  fc2.resume(rec, 0);
  int fires = 0;
  uint32_t fire_t = 0;
  while (sim.t_ms() < 200000 && !sim.landed()) {
    const SimSample s = sim.step(kDt);
    const SmOutput out = fc2.tick(s.t_ms - offset, true, s.pressure_pa, true, s.accel_g);
    if (out.fire) {
      ++fires;
      fire_t = s.t_ms;
      sim.deploy();
    }
  }
  TEST_ASSERT_EQUAL_INT(1, fires);
  TEST_ASSERT_TRUE(fire_t + 300 >= sim.apogee_ms());
  TEST_ASSERT_TRUE(fire_t <= sim.apogee_ms() + 1500);
}

static void test_reset_after_deploy_never_fires_again() {
  SimParams sp = nominal_sim();
  FlightComputer::Config cfg = default_config();
  FlightComputer fc(cfg);
  SimFlight sim(sp);
  fc.begin(0);
  bool fired = false;
  while (!fired && sim.t_ms() < 60000) {
    const SimSample s = sim.step(kDt);
    if (fc.settle_done(s.t_ms)) fc.finish_init(true);
    if (fc.tick(s.t_ms, true, s.pressure_pa, true, s.accel_g).fire) {
      fired = true;
      sim.deploy();
    }
  }
  TEST_ASSERT_TRUE(fired);
  const FlightRecord rec = fc.record(sim.t_ms());
  TEST_ASSERT_EQUAL_UINT8(1, rec.fired);

  FlightComputer fc2(cfg);
  fc2.resume(rec, 0);
  const uint32_t offset = sim.t_ms();
  int fires = 0;
  while (sim.t_ms() < 200000 && !sim.landed()) {
    const SimSample s = sim.step(kDt);
    if (fc2.tick(s.t_ms - offset, true, s.pressure_pa, true, s.accel_g).fire) ++fires;
  }
  TEST_ASSERT_EQUAL_INT(0, fires);
}

static void test_config_validation() {
  FlightConfig c;
  TEST_ASSERT_NULL(validate(c));
  c.launch_accel_g = 1.1f;  // would trigger on the pad
  TEST_ASSERT_NOT_NULL(validate(c));
  c = FlightConfig{};
  c.backup_deploy_ms = 1000;  // shorter than the apogee lockout
  TEST_ASSERT_NOT_NULL(validate(c));
  c = FlightConfig{};
  c.apogee_lockout_ms = 100;
  TEST_ASSERT_NOT_NULL(validate(c));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_nominal_flight);
  RUN_TEST(test_high_power_flight);
  RUN_TEST(test_many_noise_seeds);
  RUN_TEST(test_ten_minutes_on_the_pad_no_launch);
  RUN_TEST(test_knock_on_the_pad_is_not_a_launch);
  RUN_TEST(test_wind_gust_on_the_pad_is_not_a_launch);
  RUN_TEST(test_hop_below_min_deploy_altitude_does_not_fire);
  RUN_TEST(test_backup_timer_fires_when_baro_dies);
  RUN_TEST(test_no_backup_timer_and_dead_baro_never_fires);
  RUN_TEST(test_dead_imu_still_deploys_on_baro_alone);
  RUN_TEST(test_failed_selftest_never_arms_or_fires);
  RUN_TEST(test_short_fast_flight_respects_apogee_lockout);
  RUN_TEST(test_reset_during_coast_resumes_and_deploys_once);
  RUN_TEST(test_reset_after_deploy_never_fires_again);
  RUN_TEST(test_config_validation);
  return UNITY_END();
}
