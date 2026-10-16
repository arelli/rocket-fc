// Barometric altitude and the altitude/velocity estimator used for launch,
// apogee and landing detection.
#pragma once

#include <stdint.h>

namespace fc {

constexpr float kGravity = 9.80665f;

// International barometric formula (troposphere). Returns altitude [m] of
// pressure `p_pa` relative to the reference pressure `p0_pa`.
float pressure_to_altitude_m(float p_pa, float p0_pa);

// Inverse of pressure_to_altitude_m (used by the simulator and tests).
float altitude_to_pressure_pa(float alt_m, float p0_pa);

// Two-state Kalman filter, state = [altitude, vertical velocity].
//
//   predict: driven by measured vertical acceleration (gravity removed) when an
//            IMU is available, otherwise by a = 0 with larger process noise.
//   correct: barometric altitude.
//
// Process noise uses the white-noise-acceleration model
//   Q = sigma_a^2 * [dt^4/4, dt^3/2; dt^3/2, dt^2].
class AltitudeFilter {
 public:
  struct Params {
    float baro_sigma_m = 0.5f;       // baro altitude noise (1 sigma)
    float accel_sigma_mps2 = 4.0f;   // accel model error when using the IMU input
    float no_imu_sigma_mps2 = 20.0f; // model error when flying on baro alone
  };

  AltitudeFilter() : AltitudeFilter(Params{}) {}
  explicit AltitudeFilter(const Params& p) : params_(p) {}

  void reset(float alt_m, float vel_mps = 0.0f);

  // dt_s: time since the previous predict. accel_mps2: vertical acceleration
  // (positive up, gravity removed). have_accel=false selects the baro-only model.
  void predict(float dt_s, float accel_mps2, bool have_accel);
  void correct(float baro_alt_m);

  float alt() const { return h_; }
  float vel() const { return v_; }
  float alt_var() const { return p00_; }

 private:
  Params params_;
  float h_ = 0, v_ = 0;
  float p00_ = 1, p01_ = 0, p10_ = 0, p11_ = 1;
};

}  // namespace fc
