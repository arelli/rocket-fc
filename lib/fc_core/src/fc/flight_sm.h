// Flight state machine: launch, burnout, apogee, deployment and landing logic.
//
// This is the only code that decides to fire the deployment charge. It is
// pure logic (no hardware access), so every decision can be unit-tested
// against simulated flights (test/test_flight_sm).
//
// Pyro safety rules enforced here (see docs/FLIGHT_LOGIC.md):
//   1. The pyro is software-armed only after launch has been detected.
//   2. Apogee deployment is locked out for `apogee_lockout_ms` after launch.
//   3. Apogee requires falling velocity AND a measurable altitude drop, held
//      for `apogee_confirm_ms`.
//   4. No deployment if the flight never reached `min_deploy_alt_m`.
//   5. The charge is commanded at most once per flight (also across resets).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "fc/types.h"

namespace fc {

struct FlightConfig {
  // --- Launch detection --------------------------------------------------
  float launch_accel_g = 2.5f;     // axial specific force (reads ~+1.0 g at rest)
  uint32_t launch_accel_ms = 100;  // ...held continuously for this long
  float launch_alt_m = 30.0f;      // baro fallback if the IMU is dead
  uint32_t launch_alt_ms = 500;

  // --- Burnout (informational: BOOST -> COAST) ---------------------------
  float burnout_accel_g = 0.5f;
  uint32_t burnout_ms = 100;

  // --- Apogee ------------------------------------------------------------
  uint32_t apogee_lockout_ms = 1500;  // no apogee decision before this (after launch)
  uint32_t apogee_confirm_ms = 100;   // conditions held continuously for this long
  float apogee_drop_m = 0.5f;         // altitude below the recorded maximum
  float min_deploy_alt_m = 10.0f;     // never deploy if max altitude is below this
  // Fire this long after the apogee decision (0 = at once). The charge stays
  // committed: it fires even if the rocket lands meanwhile, and after a reset
  // during the delay it fires at once.
  uint32_t deploy_delay_ms = 0;

  // --- Backup deployment timer --------------------------------------------
  // Fires the charge this long after launch if apogee was not detected.
  // 0 = disabled. Set it from your simulation: predicted apogee time + 1..2 s.
  uint32_t backup_deploy_ms = 0;

  // --- Landing -------------------------------------------------------------
  uint32_t landed_ms = 5000;     // altitude stays within the window this long
  float landed_window_m = 2.0f;
};

// Returns nullptr if the config is usable, otherwise a reason string.
const char* validate(const FlightConfig& c);

struct SmInput {
  uint32_t t_ms = 0;
  float alt_m = 0;       // filtered altitude AGL
  float vel_mps = 0;     // filtered vertical velocity (+ up)
  float accel_up_g = 0;  // specific force along the rocket's long axis (+ toward the nose)
  bool imu_ok = false;
  bool baro_ok = false;
};

struct SmEvent {
  EventCode code;
  int32_t arg;
};

struct SmOutput {
  bool fire = false;  // command the deployment charge NOW (at most once per flight)
  static constexpr size_t kMaxEvents = 4;
  SmEvent events[kMaxEvents];
  size_t n_events = 0;

  void add(EventCode c, int32_t arg) {
    if (n_events < kMaxEvents) events[n_events++] = SmEvent{c, arg};
  }
};

class FlightStateMachine {
 public:
  explicit FlightStateMachine(const FlightConfig& cfg) : cfg_(cfg) {}

  // Leave INIT after self-test: PAD if ok, FAULT otherwise (FAULT is final).
  void start(bool selftest_ok);

  SmOutput update(const SmInput& in);

  // Restore a flight in progress after an unexpected reset (see resume.h).
  void resume(FlightState s, uint32_t now_ms, uint32_t ms_since_launch, bool fired,
              float max_alt_m, bool deploy_pending = false);

  FlightState state() const { return state_; }
  bool pyro_armed() const;
  bool fired() const { return fired_; }
  // Apogee decided, charge waiting for deploy_delay_ms.
  bool deploy_pending() const { return deploy_pending_; }
  // The charge was fired outside this logic (manual ejection by radio): never
  // fire again; apogee and landing are still detected and logged.
  void note_manual_fire() {
    fired_ = true;
    deploy_pending_ = false;
  }
  float max_alt_m() const { return max_alt_; }
  bool launched() const { return launched_; }
  uint32_t ms_since_launch(uint32_t now_ms) const {
    return launched_ ? now_ms - t_launch_ : 0;
  }

 private:
  // A condition that must stay true continuously for some time.
  struct Hold {
    bool active = false;
    uint32_t since = 0;
    bool update(bool cond, uint32_t t, uint32_t hold_ms) {
      if (!cond) {
        active = false;
        return false;
      }
      if (!active) {
        active = true;
        since = t;
      }
      return t - since >= hold_ms;
    }
  };

  void enter(FlightState s) { state_ = s; }
  void check_deploy(const SmInput& in, SmOutput& out);
  bool check_landed(const SmInput& in);
  void fire(SmOutput& out, EventCode code, float alt_m);

  FlightConfig cfg_;
  FlightState state_ = FlightState::Init;
  bool launched_ = false;
  bool fired_ = false;
  bool apogee_passed_ = false;
  bool deploy_pending_ = false;
  uint32_t t_deploy_due_ = 0;
  uint32_t t_launch_ = 0;
  float max_alt_ = 0;
  float peak_vel_ = 0;

  Hold launch_accel_, launch_alt_, burnout_, apogee_;
  float land_ref_alt_ = 0;
  uint32_t land_ref_t_ = 0;
  bool land_ref_valid_ = false;
};

}  // namespace fc
