#include "fc/flight_sm.h"

namespace fc {

namespace {
int32_t cm(float m) {
  const float v = m * 100.0f;
  if (v > 2.0e9f) return 2000000000;
  if (v < -2.0e9f) return -2000000000;
  return static_cast<int32_t>(v);
}
}  // namespace

const char* validate(const FlightConfig& c) {
  if (!(c.launch_accel_g >= 1.5f)) return "launch_accel_g must be >= 1.5 g (pad reads ~1 g)";
  if (c.launch_accel_ms < 20) return "launch_accel_ms must be >= 20 ms";
  if (!(c.launch_alt_m >= 10.0f)) return "launch_alt_m must be >= 10 m";
  if (c.launch_alt_ms < 100) return "launch_alt_ms must be >= 100 ms";
  if (c.apogee_lockout_ms < 500) return "apogee_lockout_ms must be >= 500 ms";
  if (c.apogee_confirm_ms < 20) return "apogee_confirm_ms must be >= 20 ms";
  if (!(c.apogee_drop_m > 0.0f)) return "apogee_drop_m must be > 0";
  if (!(c.min_deploy_alt_m >= 0.0f)) return "min_deploy_alt_m must be >= 0";
  if (c.backup_deploy_ms != 0 && c.backup_deploy_ms <= c.apogee_lockout_ms)
    return "backup_deploy_ms must be 0 (off) or longer than apogee_lockout_ms";
  if (c.landed_ms < 1000) return "landed_ms must be >= 1000 ms";
  if (!(c.landed_window_m > 0.0f)) return "landed_window_m must be > 0";
  return nullptr;
}

void FlightStateMachine::start(bool selftest_ok) {
  if (state_ != FlightState::Init) return;
  enter(selftest_ok ? FlightState::Pad : FlightState::Fault);
}

bool FlightStateMachine::pyro_armed() const {
  return launched_ && !fired_ && !apogee_passed_ &&
         (state_ == FlightState::Boost || state_ == FlightState::Coast);
}

void FlightStateMachine::resume(FlightState s, uint32_t now_ms, uint32_t ms_since_launch,
                                bool fired, float max_alt_m) {
  state_ = s;
  launched_ = (s == FlightState::Boost || s == FlightState::Coast ||
               s == FlightState::Descent || s == FlightState::Landed);
  fired_ = fired;
  apogee_passed_ = fired || s == FlightState::Descent || s == FlightState::Landed;
  t_launch_ = now_ms - ms_since_launch;
  max_alt_ = max_alt_m;
  peak_vel_ = 0;
  land_ref_valid_ = false;
}

void FlightStateMachine::fire(SmOutput& out, EventCode code, float alt_m) {
  if (fired_) return;  // rule 5: once per flight
  fired_ = true;
  out.fire = true;
  out.add(code, cm(alt_m));
}

void FlightStateMachine::check_deploy(const SmInput& in, SmOutput& out) {
  if (fired_ || apogee_passed_) return;
  const uint32_t since = in.t_ms - t_launch_;

  // Apogee path (rules 2-4).
  if (in.baro_ok && since >= cfg_.apogee_lockout_ms) {
    const bool falling = in.vel_mps <= 0.0f && (max_alt_ - in.alt_m) >= cfg_.apogee_drop_m;
    if (apogee_.update(falling, in.t_ms, cfg_.apogee_confirm_ms)) {
      apogee_passed_ = true;
      out.add(EventCode::Apogee, cm(max_alt_));
      if (max_alt_ >= cfg_.min_deploy_alt_m) {
        fire(out, EventCode::PyroFire, in.alt_m);
      } else {
        out.add(EventCode::ApogeeNoDeploy, cm(max_alt_));
      }
      enter(FlightState::Descent);
      return;
    }
  } else {
    apogee_.active = false;
  }

  // Backup timer (independent of the barometer).
  if (cfg_.backup_deploy_ms != 0 && since >= cfg_.backup_deploy_ms) {
    apogee_passed_ = true;
    fire(out, EventCode::PyroBackupFire, in.alt_m);
    enter(FlightState::Descent);
  }
}

bool FlightStateMachine::check_landed(const SmInput& in) {
  if (!in.baro_ok) {
    land_ref_valid_ = false;
    return false;
  }
  const float d = in.alt_m - land_ref_alt_;
  if (!land_ref_valid_ || d > cfg_.landed_window_m || d < -cfg_.landed_window_m) {
    land_ref_valid_ = true;
    land_ref_alt_ = in.alt_m;
    land_ref_t_ = in.t_ms;
    return false;
  }
  return in.t_ms - land_ref_t_ >= cfg_.landed_ms;
}

SmOutput FlightStateMachine::update(const SmInput& in) {
  SmOutput out;

  if (launched_ && in.baro_ok && in.alt_m > max_alt_) max_alt_ = in.alt_m;

  switch (state_) {
    case FlightState::Init:
    case FlightState::Fault:
    case FlightState::Landed:
      break;

    case FlightState::Pad: {
      const bool acc = launch_accel_.update(in.imu_ok && in.accel_up_g >= cfg_.launch_accel_g,
                                            in.t_ms, cfg_.launch_accel_ms);
      const bool alt = launch_alt_.update(in.baro_ok && in.alt_m >= cfg_.launch_alt_m, in.t_ms,
                                          cfg_.launch_alt_ms);
      if (acc || alt) {
        launched_ = true;
        // Back-date launch to when the condition started (better timing).
        t_launch_ = acc ? launch_accel_.since : launch_alt_.since;
        max_alt_ = in.baro_ok ? in.alt_m : 0.0f;
        peak_vel_ = in.vel_mps;
        enter(FlightState::Boost);
        out.add(EventCode::Launch, cm(in.alt_m));
        // A long sustain window can already exceed the lockout; check now.
        check_deploy(in, out);
      }
      break;
    }

    case FlightState::Boost: {
      if (in.vel_mps > peak_vel_) peak_vel_ = in.vel_mps;
      bool burnout;
      if (in.imu_ok) {
        burnout = burnout_.update(in.accel_up_g < cfg_.burnout_accel_g, in.t_ms, cfg_.burnout_ms);
      } else {
        burnout = in.baro_ok && in.vel_mps < peak_vel_ - 2.0f;
      }
      if (burnout) {
        enter(FlightState::Coast);
        out.add(EventCode::Burnout, cm(in.alt_m));
      }
      check_deploy(in, out);
      break;
    }

    case FlightState::Coast:
      check_deploy(in, out);
      if (state_ == FlightState::Coast && in.t_ms - t_launch_ >= cfg_.apogee_lockout_ms &&
          check_landed(in)) {
        // Came down without an apogee decision (e.g. no backup timer set).
        apogee_passed_ = true;
        enter(FlightState::Landed);
        out.add(EventCode::Landed, cm(max_alt_));
      }
      break;

    case FlightState::Descent:
      if (check_landed(in)) {
        enter(FlightState::Landed);
        out.add(EventCode::Landed, cm(max_alt_));
      }
      break;
  }
  return out;
}

}  // namespace fc
