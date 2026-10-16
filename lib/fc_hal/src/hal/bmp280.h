// Bosch BMP280 barometer driver (I2C), configured for rocketry:
// normal mode, pressure x4 / temperature x1 oversampling, IIR filter x4,
// 0.5 ms standby  ->  a new sample about every 13 ms (~75 Hz).
// Compensation formulas: BMP280 datasheet section 8.1 (floating point).
// Also accepts a BME280 (chip id 0x60); humidity is not used.
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "hal/i2c_device.h"

namespace hal {

class Bmp280 {
 public:
  // Probes 0x76 then 0x77. iir: filter 0..4 (off, 2, 4, 8, 16);
  // osr: pressure oversampling 0..4 (x1, x2, x4, x8, x16).
  bool begin(TwoWire* wire, uint8_t iir = 2, uint8_t osr = 2);
  bool read(float* pressure_pa, float* temp_c);
  // Sleep mode (no measurements, ~0.1 uA). Needs begin() again to wake.
  void sleep();

  uint8_t address() const { return dev_.addr(); }
  uint8_t chip_id() const { return chip_id_; }
  bool ok() const { return ok_; }
  uint32_t errors() const { return dev_.errors(); }

 private:
  bool try_addr(TwoWire* wire, uint8_t addr);

  I2cDevice dev_;
  uint8_t iir_ = 2, osr_ = 2;
  uint8_t chip_id_ = 0;
  bool ok_ = false;
  uint16_t t1_ = 0;
  int16_t t2_ = 0, t3_ = 0;
  uint16_t p1_ = 0;
  int16_t p2_ = 0, p3_ = 0, p4_ = 0, p5_ = 0, p6_ = 0, p7_ = 0, p8_ = 0, p9_ = 0;
};

}  // namespace hal
