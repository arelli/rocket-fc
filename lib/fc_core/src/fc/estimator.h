// Turns raw sensor readings into what the flight logic needs:
// altitude above the pad, vertical velocity and axial acceleration.
#pragma once

#include <stdint.h>

#include "fc/altitude.h"
#include "fc/types.h"

namespace fc {

// Which IMU axis points toward the rocket's nose (set by how the board is mounted).
enum class Axis : uint8_t { PosX, NegX, PosY, NegY, PosZ, NegZ };

float axis_component(const Vec3& v, Axis a);
const char* to_string(Axis a);

class Estimator {
 public:
  struct Params {
    AltitudeFilter::Params filter;
    Axis up_axis = Axis::PosX;
    float pad_drift_tau_s = 60.0f;  // ground pressure tracking time constant on the pad
  };

  Estimator() : Estimator(Params{}) {}
  explicit Estimator(const Params& p) : p_(p), filter_(p.filter) {}

  // --- Ground calibration (INIT) -------------------------------------------
  void begin_calibration();
  void add_calibration_sample(float pressure_pa);
  // Returns false if no samples were collected.
  bool finish_calibration();
  // Restore a known reference (resume after reset).
  void set_ground(float ground_pa, float alt_m);

  // --- Every tick ------------------------------------------------------------
  // dt_s: time since the previous call.
  void update(float dt_s, bool baro_valid, float pressure_pa, bool imu_valid,
              const Vec3& accel_g);

  // On the pad only: slowly follow weather-induced pressure drift so the
  // rocket does not "climb" while it waits. Call after update().
  void track_pad_drift(float dt_s, float pressure_pa);

  float alt_m() const { return filter_.alt(); }
  float vel_mps() const { return filter_.vel(); }
  float raw_alt_m() const { return raw_alt_; }
  float accel_up_g() const { return accel_up_g_; }
  float ground_pa() const { return ground_pa_; }
  bool calibrated() const { return ground_pa_ > 0.0f; }

 private:
  Params p_;
  AltitudeFilter filter_;
  double cal_sum_ = 0;
  uint32_t cal_n_ = 0;
  float ground_pa_ = 0;
  float raw_alt_ = 0;
  float accel_up_g_ = 0;
};

}  // namespace fc
