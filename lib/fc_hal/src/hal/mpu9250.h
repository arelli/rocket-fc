// InvenSense MPU-9250 / MPU-9255 / MPU-6500 / MPU-6050 driver (I2C).
//
// Ranges are fixed for rocketry: accelerometer +-16 g, gyroscope +-2000 deg/s,
// digital low-pass ~41 Hz. The AK8963 magnetometer inside the MPU-9250/9255 is
// read through I2C bypass mode and rotated into the accelerometer frame.
//
// Many boards sold as "MPU9250" actually carry an MPU-6500 (no magnetometer).
// The driver identifies the chip from WHO_AM_I and reports what it found.
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "fc/types.h"
#include "hal/i2c_device.h"

namespace hal {

class Mpu9250 {
 public:
  enum class Chip : uint8_t { None, Mpu6050, Mpu6500, Mpu9250, Mpu9255, Unknown };

  struct Sample {
    fc::Vec3 accel_g;   // +-16 g
    fc::Vec3 gyro_dps;  // +-2000 deg/s, bias removed
    fc::Vec3 mag_uT;    // latest magnetometer reading (accel frame)
    float temp_c = 0;
    bool mag_fresh = false;  // a new magnetometer sample arrived with this read
  };

  // Returns true if an accelerometer/gyro chip answered.
  bool begin(TwoWire* wire, uint8_t addr = 0x68);

  // Reads accel/gyro/temp (and the magnetometer when it has new data).
  bool read(Sample* s);

  void set_gyro_bias(const fc::Vec3& b) { gyro_bias_ = b; }

  Chip chip() const { return chip_; }
  uint8_t who_am_i() const { return who_; }
  bool has_mag() const { return mag_ok_; }
  const char* chip_name() const;
  uint32_t errors() const { return dev_.errors() + mag_.errors(); }

 private:
  bool init_mag();

  I2cDevice dev_;
  I2cDevice mag_;
  Chip chip_ = Chip::None;
  uint8_t who_ = 0;
  bool mag_ok_ = false;
  float mag_adj_[3] = {1, 1, 1};
  fc::Vec3 gyro_bias_;
  fc::Vec3 last_mag_;
};

}  // namespace hal
