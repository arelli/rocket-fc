#include "fc/ahrs.h"

#include <math.h>

namespace fc {

namespace {
constexpr float kDegToRad = 0.017453292519943295f;
constexpr float kRadToDeg = 57.29577951308232f;
}  // namespace

Vec3 to_rocket_frame(const Vec3& v, Axis up) {
  // Each row: rocket X, Y, Z in terms of IMU x, y, z. All have det = +1.
  switch (up) {
    case Axis::PosX: return Vec3{v.y, v.z, v.x};
    case Axis::NegX: return Vec3{v.y, -v.z, -v.x};
    case Axis::PosY: return Vec3{v.z, v.x, v.y};
    case Axis::NegY: return Vec3{v.z, -v.x, -v.y};
    case Axis::PosZ: return v;
    case Axis::NegZ: return Vec3{v.x, -v.y, -v.z};
  }
  return v;
}

float tilt_deg(const Quat& q) {
  float c = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);  // nose z-component in earth frame
  if (c > 1.0f) c = 1.0f;
  if (c < -1.0f) c = -1.0f;
  return acosf(c) * kRadToDeg;
}

void Ahrs::update(float dt, const Vec3& gyro_dps, const Vec3& acc, const Vec3& mag,
                  bool mag_valid) {
  if (!(dt > 0.0f) || dt > 0.5f) return;
  float q0 = q_.w, q1 = q_.x, q2 = q_.y, q3 = q_.z;
  float gx = gyro_dps.x * kDegToRad, gy = gyro_dps.y * kDegToRad, gz = gyro_dps.z * kDegToRad;

  float ex = 0, ey = 0, ez = 0;
  const float an = sqrtf(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);
  corrected_ = an > 0.0f && fabsf(an - 1.0f) < p_.accel_gate_g;
  if (corrected_) {
    const float ax = acc.x / an, ay = acc.y / an, az = acc.z / an;
    // Earth "up" expressed in the rocket frame (what the accelerometer should read).
    const float vx = 2.0f * (q1 * q3 - q0 * q2);
    const float vy = 2.0f * (q0 * q1 + q2 * q3);
    const float vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;
    ex += ay * vz - az * vy;
    ey += az * vx - ax * vz;
    ez += ax * vy - ay * vx;

    const float mn = sqrtf(mag.x * mag.x + mag.y * mag.y + mag.z * mag.z);
    if (mag_valid && mn > 0.0f) {
      const float mx = mag.x / mn, my = mag.y / mn, mz = mag.z / mn;
      // Field in the earth frame, then its horizontal part rotated onto north.
      const float hx = 2.0f * (mx * (0.5f - q2 * q2 - q3 * q3) + my * (q1 * q2 - q0 * q3) +
                               mz * (q1 * q3 + q0 * q2));
      const float hy = 2.0f * (mx * (q1 * q2 + q0 * q3) + my * (0.5f - q1 * q1 - q3 * q3) +
                               mz * (q2 * q3 - q0 * q1));
      const float bx = sqrtf(hx * hx + hy * hy);
      const float bz = 2.0f * (mx * (q1 * q3 - q0 * q2) + my * (q2 * q3 + q0 * q1) +
                               mz * (0.5f - q1 * q1 - q2 * q2));
      // Expected field direction in the rocket frame.
      const float wx = 2.0f * (bx * (0.5f - q2 * q2 - q3 * q3) + bz * (q1 * q3 - q0 * q2));
      const float wy = 2.0f * (bx * (q1 * q2 - q0 * q3) + bz * (q0 * q1 + q2 * q3));
      const float wz = 2.0f * (bx * (q0 * q2 + q1 * q3) + bz * (0.5f - q1 * q1 - q2 * q2));
      ex += my * wz - mz * wy;
      ey += mz * wx - mx * wz;
      ez += mx * wy - my * wx;
    }
    const float kp = fast_ ? p_.kp_fast : p_.kp;
    gx += kp * ex;
    gy += kp * ey;
    gz += kp * ez;
  }

  // Integrate q' = 0.5 q (0, g).
  const float h = 0.5f * dt;
  const float a0 = q0, a1 = q1, a2 = q2, a3 = q3;
  q0 += (-a1 * gx - a2 * gy - a3 * gz) * h;
  q1 += (a0 * gx + a2 * gz - a3 * gy) * h;
  q2 += (a0 * gy - a1 * gz + a3 * gx) * h;
  q3 += (a0 * gz + a1 * gy - a2 * gx) * h;
  const float n = sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
  if (n > 0.0f) {
    q_.w = q0 / n;
    q_.x = q1 / n;
    q_.y = q2 / n;
    q_.z = q3 / n;
  }
}

}  // namespace fc
