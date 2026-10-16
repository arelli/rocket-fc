// A deliberately simple 1-D rocket flight simulator that produces the same
// signals the real sensors do (pressure + axial accelerometer, with noise).
// Used by the unit tests and by the on-device (bench mode) "sim" command, so
// the complete chain sensor -> estimator -> state machine -> pyro can be
// exercised without flying.
#pragma once

#include <stdint.h>

#include "fc/types.h"

namespace fc {

struct SimParams {
  uint32_t ignition_ms = 5000;     // time on the pad before the motor lights
  float burn_s = 1.2f;             // motor burn time
  float thrust_accel_mps2 = 60.0f; // thrust / mass
  float drag_k = 0.0015f;          // drag deceleration = k * v^2   [1/m]
  float descent_mps = 6.0f;        // parachute descent rate after deploy()
  float ground_pa = 101325.0f;
  float baro_noise_m = 0.3f;       // 1 sigma
  float accel_noise_g = 0.03f;     // 1 sigma
  uint32_t seed = 12345;
};

struct SimSample {
  uint32_t t_ms;
  float alt_m;        // truth
  float vel_mps;      // truth
  float pressure_pa;  // measured (noisy)
  Vec3 accel_g;       // measured body accel, +X toward the nose (noisy)
};

class SimFlight {
 public:
  explicit SimFlight(const SimParams& p);

  // Advance the simulation by dt_ms and return the sensor readings.
  SimSample step(uint32_t dt_ms);

  // The flight computer fired the charge: switch to parachute descent.
  void deploy();

  // Inject a disturbance for robustness tests.
  void add_pressure_offset_m(float m) { baro_offset_m_ = m; }
  void add_accel_spike_g(float g) { accel_spike_g_ = g; }

  float apogee_alt_m() const { return apogee_alt_; }
  uint32_t apogee_ms() const { return apogee_ms_; }
  bool landed() const { return landed_; }
  bool deployed() const { return deployed_; }
  uint32_t t_ms() const { return t_ms_; }

 private:
  float gauss();

  SimParams p_;
  uint32_t t_ms_ = 0;
  double alt_ = 0, vel_ = 0, acc_ = 0;
  bool deployed_ = false;
  bool launched_ = false;
  bool landed_ = false;
  float apogee_alt_ = 0;
  uint32_t apogee_ms_ = 0;
  bool apogee_seen_ = false;
  float baro_offset_m_ = 0;
  float accel_spike_g_ = 0;
  uint32_t rng_;
};

}  // namespace fc
