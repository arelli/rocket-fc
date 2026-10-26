// "Set upright": which IMU axis points to the nose.
#include <math.h>
#include <unity.h>

#include "fc/mount.h"

using namespace fc;

void setUp() {}
void tearDown() {}

static void test_each_axis() {
  const struct { Vec3 g; Axis a; } cases[] = {
      {{1, 0, 0}, Axis::PosX}, {{-1, 0, 0}, Axis::NegX}, {{0, 1, 0}, Axis::PosY},
      {{0, -1, 0}, Axis::NegY}, {{0, 0, 1}, Axis::PosZ}, {{0, 0, -1}, Axis::NegZ},
  };
  for (const auto& c : cases) {
    const UprightResult r = find_up_axis(c.g, 0.01f);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL(c.a, r.axis);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r.off_deg);
  }
}

static void test_tilted_mount_and_refusals() {
  // IMU tilted 10 deg from its -Y axis: accepted, angle reported.
  const float t = 10.0f / 57.29578f;
  UprightResult r = find_up_axis(Vec3{sinf(t), -cosf(t), 0}, 0.01f);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL(Axis::NegY, r.axis);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.0f, r.off_deg);
  // 45 deg between two axes: refused.
  r = find_up_axis(Vec3{0.7071f, 0.7071f, 0}, 0.01f);
  TEST_ASSERT_FALSE(r.ok);
  // Moving, or not 1 g (lying in a moving car, free fall, broken sensor).
  TEST_ASSERT_FALSE(find_up_axis(Vec3{0, 0, 1}, 0.2f).ok);
  TEST_ASSERT_FALSE(find_up_axis(Vec3{0, 0, 0.5f}, 0.01f).ok);
}

static void test_sampler() {
  UprightSampler s;
  for (int i = 0; i < 100; ++i) s.add(Vec3{0.01f * ((i % 2) ? 1 : -1), 0, 1.0f});
  TEST_ASSERT_EQUAL_UINT32(100, s.count());
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, s.mean().z);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.01f, s.spread_g());
  s.reset();
  TEST_ASSERT_EQUAL_UINT32(0, s.count());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_each_axis);
  RUN_TEST(test_tilted_mount_and_refusals);
  RUN_TEST(test_sampler);
  return UNITY_END();
}
