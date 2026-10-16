#include "hal/i2c_device.h"

namespace hal {

bool I2cDevice::present() {
  if (!wire_) return false;
  wire_->beginTransmission(addr_);
  return wire_->endTransmission() == 0;
}

bool I2cDevice::write8(uint8_t reg, uint8_t value) {
  if (!wire_) return false;
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  wire_->write(value);
  if (wire_->endTransmission() != 0) {
    ++errors_;
    return false;
  }
  return true;
}

bool I2cDevice::read(uint8_t reg, uint8_t* buf, size_t n) {
  if (!wire_) return false;
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) {  // repeated start
    ++errors_;
    return false;
  }
  const size_t got = wire_->requestFrom(static_cast<uint16_t>(addr_), n, true);
  if (got != n) {
    while (wire_->available()) wire_->read();
    ++errors_;
    return false;
  }
  for (size_t i = 0; i < n; ++i) buf[i] = static_cast<uint8_t>(wire_->read());
  return true;
}

}  // namespace hal
