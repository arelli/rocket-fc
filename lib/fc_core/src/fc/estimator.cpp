#include "fc/estimator.h"

namespace fc {

float axis_component(const Vec3& v, Axis a) {
  switch (a) {
    case Axis::PosX: return v.x;
    case Axis::NegX: return -v.x;
    case Axis::PosY: return v.y;
    case Axis::NegY: return -v.y;
    case Axis::PosZ: return v.z;
    case Axis::NegZ: return -v.z;
  }
  return 0.0f;
}

const char* to_string(Axis a) {
  switch (a) {
    case Axis::PosX: return "+X";
    case Axis::NegX: return "-X";
    case Axis::PosY: return "+Y";
    case Axis::NegY: return "-Y";
    case Axis::PosZ: return "+Z";
    case Axis::NegZ: return "-Z";
  }
  return "?";
}

void Estimator::begin_calibration() {
  cal_sum_ = 0;
  cal_n_ = 0;
}

void Estimator::add_calibration_sample(float p) {
  if (p > 30000.0f && p < 120000.0f) {
    cal_sum_ += p;
    ++cal_n_;
  }
}

bool Estimator::finish_calibration() {
  if (cal_n_ == 0) return false;
  ground_pa_ = static_cast<float>(cal_sum_ / cal_n_);
  filter_.reset(0.0f, 0.0f);
  raw_alt_ = 0.0f;
  return true;
}

void Estimator::set_ground(float ground_pa, float alt_m) {
  ground_pa_ = ground_pa;
  filter_.reset(alt_m, 0.0f);
  raw_alt_ = alt_m;
}

void Estimator::update(float dt, bool baro_valid, float p, bool imu_valid, const Vec3& accel_g) {
  accel_up_g_ = imu_valid ? axis_component(accel_g, p_.up_axis) : 0.0f;
  // Vertical acceleration, assuming the rocket flies close to vertical.
  const float a = (accel_up_g_ - 1.0f) * kGravity;
  filter_.predict(dt, a, imu_valid);
  if (baro_valid && ground_pa_ > 0.0f) {
    raw_alt_ = pressure_to_altitude_m(p, ground_pa_);
    filter_.correct(raw_alt_);
  }
}

void Estimator::track_pad_drift(float dt, float p) {
  if (!(ground_pa_ > 0.0f) || !(dt > 0.0f)) return;
  // Only follow slow changes: ignore anything that looks like real movement.
  if (raw_alt_ > 3.0f || raw_alt_ < -3.0f) return;
  const float k = dt / (p_.pad_drift_tau_s + dt);
  ground_pa_ += k * (p - ground_pa_);
}

}  // namespace fc
