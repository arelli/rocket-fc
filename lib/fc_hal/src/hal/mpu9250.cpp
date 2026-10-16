#include "hal/mpu9250.h"

namespace hal {

namespace {
// MPU registers
constexpr uint8_t kSmplrtDiv = 0x19;
constexpr uint8_t kConfig = 0x1A;
constexpr uint8_t kGyroConfig = 0x1B;
constexpr uint8_t kAccelConfig = 0x1C;
constexpr uint8_t kAccelConfig2 = 0x1D;  // not on MPU-6050
constexpr uint8_t kIntPinCfg = 0x37;
constexpr uint8_t kAccelXoutH = 0x3B;
constexpr uint8_t kUserCtrl = 0x6A;
constexpr uint8_t kPwrMgmt1 = 0x6B;
constexpr uint8_t kWhoAmI = 0x75;

// AK8963 magnetometer (behind the MPU-9250, I2C address 0x0C in bypass mode)
constexpr uint8_t kAkAddr = 0x0C;
constexpr uint8_t kAkWia = 0x00;  // reads 0x48
constexpr uint8_t kAkSt1 = 0x02;
constexpr uint8_t kAkHxl = 0x03;  // HXL..HZH, then ST2 at 0x09
constexpr uint8_t kAkCntl1 = 0x0A;
constexpr uint8_t kAkAsax = 0x10;

constexpr float kAccelLsbPerG = 2048.0f;  // +-16 g
constexpr float kGyroLsbPerDps = 16.4f;   // +-2000 deg/s
constexpr float kMagUtPerLsb = 0.15f;     // 16-bit output

int16_t be16(const uint8_t* p) { return static_cast<int16_t>((p[0] << 8) | p[1]); }
int16_t le16(const uint8_t* p) { return static_cast<int16_t>((p[1] << 8) | p[0]); }
}  // namespace

const char* Mpu9250::chip_name() const {
  switch (chip_) {
    case Chip::None: return "none";
    case Chip::Mpu6050: return "MPU-6050";
    case Chip::Mpu6500: return "MPU-6500 (no magnetometer)";
    case Chip::Mpu9250: return "MPU-9250";
    case Chip::Mpu9255: return "MPU-9255";
    case Chip::Unknown: return "unknown MPU";
  }
  return "?";
}

bool Mpu9250::begin(TwoWire* wire, uint8_t addr) {
  dev_ = I2cDevice(wire, addr);
  mag_ = I2cDevice(wire, kAkAddr);
  chip_ = Chip::None;
  mag_ok_ = false;
  if (!dev_.present()) return false;

  dev_.write8(kPwrMgmt1, 0x80);  // device reset
  delay(100);
  dev_.write8(kPwrMgmt1, 0x01);  // wake, PLL clock
  delay(10);
  if (!dev_.read8(kWhoAmI, &who_)) return false;
  switch (who_) {
    case 0x68: chip_ = Chip::Mpu6050; break;
    case 0x70: chip_ = Chip::Mpu6500; break;
    case 0x71: chip_ = Chip::Mpu9250; break;
    case 0x73: chip_ = Chip::Mpu9255; break;
    default: chip_ = Chip::Unknown; break;
  }

  bool ok = true;
  ok &= dev_.write8(kConfig, 0x03);       // gyro DLPF ~41 Hz, 1 kHz internal rate
  ok &= dev_.write8(kSmplrtDiv, 0x00);    // 1 kHz output rate
  ok &= dev_.write8(kGyroConfig, 0x18);   // +-2000 deg/s
  ok &= dev_.write8(kAccelConfig, 0x18);  // +-16 g
  if (chip_ != Chip::Mpu6050) ok &= dev_.write8(kAccelConfig2, 0x03);  // accel DLPF ~41 Hz
  ok &= dev_.write8(kUserCtrl, 0x00);     // internal I2C master off
  ok &= dev_.write8(kIntPinCfg, 0x02);    // I2C bypass: expose the AK8963 on the bus
  if (!ok) {
    chip_ = Chip::None;
    return false;
  }
  delay(10);
  if (chip_ == Chip::Mpu9250 || chip_ == Chip::Mpu9255 || chip_ == Chip::Unknown) {
    mag_ok_ = init_mag();
  }
  return true;
}

bool Mpu9250::init_mag() {
  uint8_t wia = 0;
  if (!mag_.read8(kAkWia, &wia) || wia != 0x48) return false;
  mag_.write8(kAkCntl1, 0x00);  // power down
  delay(10);
  mag_.write8(kAkCntl1, 0x0F);  // fuse ROM access
  delay(10);
  uint8_t asa[3];
  if (!mag_.read(kAkAsax, asa, 3)) return false;
  for (int i = 0; i < 3; ++i) mag_adj_[i] = (static_cast<float>(asa[i]) - 128.0f) / 256.0f + 1.0f;
  mag_.write8(kAkCntl1, 0x00);
  delay(10);
  if (!mag_.write8(kAkCntl1, 0x16)) return false;  // 16-bit, continuous 100 Hz
  delay(10);
  return true;
}

bool Mpu9250::read(Sample* s) {
  if (chip_ == Chip::None) return false;
  uint8_t b[14];
  if (!dev_.read(kAccelXoutH, b, sizeof(b))) return false;
  s->accel_g.x = be16(&b[0]) / kAccelLsbPerG;
  s->accel_g.y = be16(&b[2]) / kAccelLsbPerG;
  s->accel_g.z = be16(&b[4]) / kAccelLsbPerG;
  const int16_t t = be16(&b[6]);
  s->temp_c = (chip_ == Chip::Mpu6050) ? t / 340.0f + 36.53f : t / 333.87f + 21.0f;
  s->gyro_dps.x = be16(&b[8]) / kGyroLsbPerDps - gyro_bias_.x;
  s->gyro_dps.y = be16(&b[10]) / kGyroLsbPerDps - gyro_bias_.y;
  s->gyro_dps.z = be16(&b[12]) / kGyroLsbPerDps - gyro_bias_.z;

  s->mag_fresh = false;
  if (mag_ok_) {
    uint8_t st1 = 0;
    if (mag_.read8(kAkSt1, &st1) && (st1 & 0x01)) {
      uint8_t m[7];  // HXL..HZH + ST2 (reading ST2 releases the data registers)
      if (mag_.read(kAkHxl, m, sizeof(m)) && !(m[6] & 0x08)) {  // skip magnetic overflow
        const float mx = le16(&m[0]) * kMagUtPerLsb * mag_adj_[0];
        const float my = le16(&m[2]) * kMagUtPerLsb * mag_adj_[1];
        const float mz = le16(&m[4]) * kMagUtPerLsb * mag_adj_[2];
        // AK8963 axes relative to the accelerometer: X<->Y swapped, Z inverted.
        last_mag_.x = my;
        last_mag_.y = mx;
        last_mag_.z = -mz;
        s->mag_fresh = true;
      }
    }
  }
  s->mag_uT = last_mag_;
  return true;
}

void Mpu9250::sleep() {
  if (mag_ok_) mag_.write8(kAkCntl1, 0x00);              // AK8963 power-down
  if (chip_ != Chip::None) dev_.write8(kPwrMgmt1, 0x40);  // SLEEP bit
}

}  // namespace hal
