// Attitude filter and IMU-to-rocket axis mapping.
#include <math.h>
#include <unity.h>

#include "fc/ahrs.h"

using namespace fc;

void setUp() {}
void tearDown() {}

namespace {
constexpr float kPi = 3.14159265f;

Vec3 cross(const Vec3& a, const Vec3& b) {
  return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Rotate an earth-frame vector into the rocket frame for a rocket tilted by
// `tilt` degrees about the rocket X axis (nose toward -Y earth when tilting).
Vec3 earth_to_body_tilt_x(const Vec3& e, float tilt) {
  const float c = cosf(tilt * kPi / 180), s = sinf(tilt * kPi / 180);
  // body = Rx(tilt)^T * earth
  return Vec3{e.x, c * e.y + s * e.z, -s * e.y + c * e.z};
}

const Vec3 kUp{0, 0, 1};
// Earth field in NWU: north and pointing down (northern hemisphere, ~60 deg dip).
const Vec3 kField{20.0f, 0.0f, -40.0f};

void settle(Ahrs& a, const Vec3& acc, const Vec3& mag, bool mag_ok, float seconds) {
  for (int i = 0; i < static_cast<int>(seconds * 100); ++i) a.update(0.01f, Vec3{}, acc, mag, mag_ok);
}
}  // namespace

static void test_axis_mapping_is_proper_rotation() {
  const Axis axes[] = {Axis::PosX, Axis::NegX, Axis::PosY, Axis::NegY, Axis::PosZ, Axis::NegZ};
  for (Axis ax : axes) {
    const Vec3 ex = to_rocket_frame(Vec3{1, 0, 0}, ax);
    const Vec3 ey = to_rocket_frame(Vec3{0, 1, 0}, ax);
    const Vec3 ez = to_rocket_frame(Vec3{0, 0, 1}, ax);
    // Images of the basis vectors form a right-handed orthonormal set.
    const Vec3 c = cross(ex, ey);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, dot(c, ez));
    // Rocket Z equals the configured "up" component used by the estimator.
    const Vec3 v{0.3f, -0.5f, 0.8f};
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, axis_component(v, ax), to_rocket_frame(v, ax).z);
  }
}

static void test_vertical_at_rest_converges() {
  Ahrs a;
  a.set_fast(true);
  // Start from a wrong attitude: pretend we were lying on the side.
  for (int i = 0; i < 100; ++i) a.update(0.01f, Vec3{90, 0, 0}, Vec3{0, 0, 3}, Vec3{}, false);
  TEST_ASSERT_TRUE(tilt_deg(a.q()) > 60.0f);
  settle(a, kUp, kField, true, 3.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, tilt_deg(a.q()));
}

static void test_tilted_at_rest() {
  const float tilts[] = {10.0f, 30.0f, 80.0f};
  for (float tilt : tilts) {
    Ahrs a;
    a.set_fast(true);
    settle(a, earth_to_body_tilt_x(kUp, tilt), earth_to_body_tilt_x(kField, tilt), true, 5.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, tilt, tilt_deg(a.q()));
  }
}

static void test_no_magnetometer_still_gives_tilt() {
  Ahrs a;
  a.set_fast(true);
  settle(a, earth_to_body_tilt_x(kUp, 25.0f), Vec3{}, false, 5.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 25.0f, tilt_deg(a.q()));
}

static void test_boost_ignores_accelerometer() {
  Ahrs a;
  a.set_fast(true);
  settle(a, kUp, kField, true, 3.0f);
  a.set_fast(false);
  // 10 g along the nose with a sideways component: must not tilt the estimate.
  for (int i = 0; i < 200; ++i) a.update(0.01f, Vec3{}, Vec3{3.0f, 0.0f, 10.0f}, kField, true);
  TEST_ASSERT_FALSE(a.corrected());
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, tilt_deg(a.q()));
}

static void test_gyro_rotation_is_integrated() {
  Ahrs a;
  a.set_fast(true);
  settle(a, kUp, kField, true, 3.0f);
  // Pitch over 45 deg about rocket X in 1 s, under thrust (accel gated off).
  for (int i = 0; i < 100; ++i) a.update(0.01f, Vec3{45, 0, 0}, Vec3{0, 0, 5}, Vec3{}, false);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 45.0f, tilt_deg(a.q()));
  // Spin about the nose axis does not change the tilt.
  for (int i = 0; i < 100; ++i) a.update(0.01f, Vec3{0, 0, 720}, Vec3{0, 0, 5}, Vec3{}, false);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 45.0f, tilt_deg(a.q()));
}

static void test_quaternion_stays_normalized() {
  Ahrs a;
  for (int i = 0; i < 10000; ++i)
    a.update(0.01f, Vec3{123, -456, 789}, Vec3{0.2f, 0.1f, 0.97f}, kField, true);
  const Quat& q = a.q();
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_axis_mapping_is_proper_rotation);
  RUN_TEST(test_vertical_at_rest_converges);
  RUN_TEST(test_tilted_at_rest);
  RUN_TEST(test_no_magnetometer_still_gives_tilt);
  RUN_TEST(test_boost_ignores_accelerometer);
  RUN_TEST(test_gyro_rotation_is_integrated);
  RUN_TEST(test_quaternion_stays_normalized);
  return UNITY_END();
}
