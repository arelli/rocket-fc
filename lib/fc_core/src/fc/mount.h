// "This is upright": find which IMU axis points to the nose from the
// accelerometer while the rocket stands still, nose up (console command
// "imu upright"; UI button "Set upright"). The result is the imu_up_axis
// setting. Pure logic, tested in test/test_mount.
#pragma once

#include <stdint.h>

#include "fc/estimator.h"
#include "fc/types.h"

namespace fc {

// Averages accelerometer samples (IMU frame, g) and their spread.
class UprightSampler {
 public:
  void reset() { *this = UprightSampler{}; }
  void add(const Vec3& a);
  uint32_t count() const { return n_; }
  Vec3 mean() const;
  float spread_g() const;  // RMS deviation from the mean (vibration / movement)

 private:
  uint32_t n_ = 0;
  double sx_ = 0, sy_ = 0, sz_ = 0, sxx_ = 0, syy_ = 0, szz_ = 0;
};

struct UprightResult {
  bool ok = false;
  Axis axis = Axis::PosX;   // the IMU axis pointing to the nose
  float off_deg = 0;        // angle between gravity and that axis (IMU mounting tilt)
  float magnitude_g = 0;
  const char* why = "";     // when !ok
};

// Refuses when the rocket was moving (spread above 0.05 g), the reading is not
// about 1 g (0.85-1.15), or no axis is within max_off_deg of vertical.
UprightResult find_up_axis(const Vec3& mean_g, float spread_g, float max_off_deg = 25.0f);

}  // namespace fc
