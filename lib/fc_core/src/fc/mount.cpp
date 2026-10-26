#include "fc/mount.h"

#include <math.h>

namespace fc {

void UprightSampler::add(const Vec3& a) {
  ++n_;
  sx_ += a.x;
  sy_ += a.y;
  sz_ += a.z;
  sxx_ += static_cast<double>(a.x) * a.x;
  syy_ += static_cast<double>(a.y) * a.y;
  szz_ += static_cast<double>(a.z) * a.z;
}

Vec3 UprightSampler::mean() const {
  if (!n_) return Vec3{};
  return Vec3{static_cast<float>(sx_ / n_), static_cast<float>(sy_ / n_), static_cast<float>(sz_ / n_)};
}

float UprightSampler::spread_g() const {
  if (n_ < 2) return 0.0f;
  const Vec3 m = mean();
  const double v = (sxx_ / n_ - double(m.x) * m.x) + (syy_ / n_ - double(m.y) * m.y) + (szz_ / n_ - double(m.z) * m.z);
  return v > 0 ? static_cast<float>(sqrt(v)) : 0.0f;
}

UprightResult find_up_axis(const Vec3& g, float spread_g, float max_off_deg) {
  UprightResult r;
  r.magnitude_g = sqrtf(g.x * g.x + g.y * g.y + g.z * g.z);
  if (spread_g > 0.05f) {
    r.why = "the rocket was moving: hold it still";
    return r;
  }
  if (r.magnitude_g < 0.85f || r.magnitude_g > 1.15f) {
    r.why = "accelerometer does not read about 1 g";
    return r;
  }
  // Standing still nose up, the accelerometer reads +1 g along the nose axis.
  static const Axis kAxes[] = {Axis::PosX, Axis::NegX, Axis::PosY, Axis::NegY, Axis::PosZ, Axis::NegZ};
  float best = -2.0f;
  for (Axis a : kAxes) {
    const float c = axis_component(g, a) / r.magnitude_g;
    if (c > best) {
      best = c;
      r.axis = a;
    }
  }
  r.off_deg = acosf(best > 1.0f ? 1.0f : best) * 57.29578f;
  if (r.off_deg > max_off_deg) {
    r.why = "no IMU axis is near vertical: mount the IMU aligned with the rocket";
    return r;
  }
  r.ok = true;
  return r;
}

}  // namespace fc
