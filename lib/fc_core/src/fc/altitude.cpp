#include "fc/altitude.h"

#include <math.h>

namespace fc {

namespace {
constexpr float kExp = 0.190263f;  // R*L/(g*M) for the standard atmosphere
}

float pressure_to_altitude_m(float p_pa, float p0_pa) {
  if (p_pa <= 0.0f || p0_pa <= 0.0f) return 0.0f;
  return 44330.0f * (1.0f - powf(p_pa / p0_pa, kExp));
}

float altitude_to_pressure_pa(float alt_m, float p0_pa) {
  return p0_pa * powf(1.0f - alt_m / 44330.0f, 1.0f / kExp);
}

void AltitudeFilter::reset(float alt_m, float vel_mps) {
  h_ = alt_m;
  v_ = vel_mps;
  p00_ = params_.baro_sigma_m * params_.baro_sigma_m;
  p01_ = p10_ = 0.0f;
  p11_ = 1.0f;
}

void AltitudeFilter::predict(float dt, float a, bool have_accel) {
  if (!(dt > 0.0f)) return;
  if (dt > 1.0f) dt = 1.0f;  // never extrapolate across long gaps
  if (!have_accel) a = 0.0f;

  h_ += v_ * dt + 0.5f * a * dt * dt;
  v_ += a * dt;

  const float sa = have_accel ? params_.accel_sigma_mps2 : params_.no_imu_sigma_mps2;
  const float q = sa * sa;
  const float dt2 = dt * dt;
  const float q00 = q * dt2 * dt2 * 0.25f;
  const float q01 = q * dt2 * dt * 0.5f;
  const float q11 = q * dt2;

  const float n00 = p00_ + dt * (p10_ + p01_) + dt2 * p11_ + q00;
  const float n01 = p01_ + dt * p11_ + q01;
  const float n10 = p10_ + dt * p11_ + q01;
  const float n11 = p11_ + q11;
  p00_ = n00;
  p01_ = n01;
  p10_ = n10;
  p11_ = n11;
}

void AltitudeFilter::correct(float z) {
  const float r = params_.baro_sigma_m * params_.baro_sigma_m;
  const float s = p00_ + r;
  if (!(s > 0.0f)) return;
  const float k0 = p00_ / s;
  const float k1 = p10_ / s;
  const float y = z - h_;
  h_ += k0 * y;
  v_ += k1 * y;

  const float n00 = (1.0f - k0) * p00_;
  const float n01 = (1.0f - k0) * p01_;
  const float n10 = p10_ - k1 * p00_;
  const float n11 = p11_ - k1 * p01_;
  p00_ = n00;
  p01_ = n01;
  p10_ = n10;
  p11_ = n11;
}

}  // namespace fc
