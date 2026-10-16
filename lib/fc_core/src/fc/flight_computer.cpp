#include "fc/flight_computer.h"

namespace fc {

void FlightComputer::begin(uint32_t t_ms) {
  t_begin_ = t_ms;
  have_last_ = false;
  est_.begin_calibration();
}

uint32_t FlightComputer::settle_remaining_ms(uint32_t t_ms) const {
  const uint32_t el = t_ms - t_begin_;
  return el >= cfg_.settle_ms ? 0 : cfg_.settle_ms - el;
}

bool FlightComputer::finish_init(bool selftest_ok) {
  const bool cal_ok = est_.finish_calibration();
  sm_.start(selftest_ok && cal_ok);
  return cal_ok;
}

void FlightComputer::resume(const FlightRecord& rec, uint32_t now_ms) {
  est_.set_ground(rec.ground_pa, 0.0f);
  sm_.resume(static_cast<FlightState>(rec.state), now_ms, rec.ms_since_launch, rec.fired != 0,
             rec.max_alt_m, (rec.flags & kRecDeployPending) != 0);
  // Altitude will converge from the first barometer readings.
  have_last_ = false;
}

SmOutput FlightComputer::tick(uint32_t t_ms, bool baro_ok, float p, bool imu_ok,
                              const Vec3& accel_g) {
  const float dt = have_last_ ? (t_ms - t_last_) / 1000.0f : 0.0f;
  t_last_ = t_ms;
  const bool first = !have_last_;
  have_last_ = true;

  if (sm_.state() == FlightState::Init) {
    const uint32_t el = t_ms - t_begin_;
    if (baro_ok && el + cfg_.calib_ms >= cfg_.settle_ms) est_.add_calibration_sample(p);
    // No altitude or velocity until calibrated: dt = 0 skips the integration
    // (only the axial acceleration is updated, for the self-test).
    est_.update(0.0f, false, p, imu_ok, accel_g);
    return SmOutput{};
  }

  if (first && baro_ok && est_.calibrated()) {
    // After a resume, snap the filter to the measured altitude.
    est_.set_ground(est_.ground_pa(), pressure_to_altitude_m(p, est_.ground_pa()));
  }
  est_.update(dt, baro_ok, p, imu_ok, accel_g);

  if (sm_.state() == FlightState::Pad && baro_ok) {
    const float f = est_.accel_up_g();
    const bool still = !imu_ok || (f > 0.8f && f < 1.2f);
    if (still) est_.track_pad_drift(dt, p);
  }

  SmInput in;
  in.t_ms = t_ms;
  in.alt_m = est_.alt_m();
  in.vel_mps = est_.vel_mps();
  in.accel_up_g = est_.accel_up_g();
  in.imu_ok = imu_ok;
  in.baro_ok = baro_ok && est_.calibrated();
  return sm_.update(in);
}

FlightRecord FlightComputer::record(uint32_t now_ms) const {
  FlightRecord r;
  r.state = static_cast<uint8_t>(sm_.state());
  r.fired = sm_.fired() ? 1 : 0;
  r.flags = sm_.deploy_pending() ? kRecDeployPending : 0;
  r.ground_pa = est_.ground_pa();
  r.max_alt_m = sm_.max_alt_m();
  r.ms_since_launch = sm_.ms_since_launch(now_ms);
  r.seal();
  return r;
}

}  // namespace fc
