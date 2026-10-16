// Minimal register access for an I2C device, with error counting.
#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace hal {

class I2cDevice {
 public:
  I2cDevice() = default;
  I2cDevice(TwoWire* wire, uint8_t addr) : wire_(wire), addr_(addr) {}

  bool present();
  bool write8(uint8_t reg, uint8_t value);
  bool read(uint8_t reg, uint8_t* buf, size_t n);
  bool read8(uint8_t reg, uint8_t* value) { return read(reg, value, 1); }

  uint8_t addr() const { return addr_; }
  uint32_t errors() const { return errors_; }

 private:
  TwoWire* wire_ = nullptr;
  uint8_t addr_ = 0;
  uint32_t errors_ = 0;
};

}  // namespace hal
