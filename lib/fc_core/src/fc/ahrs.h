// Attitude estimation (Mahony complementary filter) for display and logging.
//
// The flight decisions do NOT use this: deployment depends only on the
// barometer/accelerometer estimator. The attitude is sent in telemetry so the
// ground station can show the rocket's orientation, and logged for analysis.
//
// Frames
//   rocket frame: +Z toward the nose; X, Y fixed to the airframe (see
//                 to_rocket_frame for how they follow from the IMU mounting)
//   earth frame:  NWU, x = magnetic north, y = west, z = up
//   q rotates rocket-frame vectors into the earth frame:
//       v_earth = q * v_rocket * conj(q)
//
// The accelerometer only measures gravity when the rocket is not accelerating,
// so its correction is used only while |a| is within `accel_gate_g` of 1 g
// (on the pad, under the parachute). During boost and coast the attitude is
// propagated from the gyroscope alone. The magnetometer (if present) fixes the
// heading and is used under the same condition.
#pragma once

#include <stdint.h>

#include "fc/estimator.h"
#include "fc/types.h"

namespace fc {

// Rotate an IMU-frame vector into the rocket frame, given which IMU axis
// points to the nose. The mapping is a proper rotation (right-handed).
Vec3 to_rocket_frame(const Vec3& imu, Axis up);

// Angle between the rocket's nose and vertical [deg] (0 = straight up).
float tilt_deg(const Quat& q);

class Ahrs {
 public:
  struct Params {
    float kp = 1.0f;            // correction gain [1/s] in normal operation
    float kp_fast = 10.0f;      // while fast convergence is on (power-up, at rest)
    float accel_gate_g = 0.15f; // use accel/mag only if | |a| - 1 g | below this
  };

  Ahrs() : Ahrs(Params{}) {}
  explicit Ahrs(const Params& p) : p_(p) {}

  void reset() { q_ = Quat{}; }
  void set_fast(bool on) { fast_ = on; }

  // gyro [deg/s], accel [g], mag [any unit] all in the rocket frame.
  void update(float dt_s, const Vec3& gyro_dps, const Vec3& accel_g, const Vec3& mag,
              bool mag_valid);

  const Quat& q() const { return q_; }
  bool corrected() const { return corrected_; }  // last update used accel

 private:
  Params p_;
  Quat q_;
  bool fast_ = false;
  bool corrected_ = false;
};

}  // namespace fc
