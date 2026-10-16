#include "hal/bmp280.h"

namespace hal {

namespace {
constexpr uint8_t kRegCalib = 0x88;  // 24 bytes: T1..T3, P1..P9
constexpr uint8_t kRegChipId = 0xD0;
constexpr uint8_t kRegReset = 0xE0;
constexpr uint8_t kRegCtrlMeas = 0xF4;
constexpr uint8_t kRegConfig = 0xF5;
constexpr uint8_t kRegPressMsb = 0xF7;  // press[3], temp[3]

// ctrl_meas: osrs_t=x1 (001), osrs_p = osr + 1 (x1..x16), mode=normal (11)
uint8_t ctrl_meas(uint8_t osr) { return static_cast<uint8_t>((0b001 << 5) | ((osr + 1) << 2) | 0b11); }
// config: t_sb=0.5 ms (000), filter = iir (off, 2, 4, 8, 16), spi3w off
uint8_t config_reg(uint8_t iir) { return static_cast<uint8_t>((0b000 << 5) | (iir << 2)); }

uint16_t u16le(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
int16_t s16le(const uint8_t* p) { return static_cast<int16_t>(u16le(p)); }
}  // namespace

bool Bmp280::begin(TwoWire* wire, uint8_t iir, uint8_t osr) {
  iir_ = iir > 4 ? 2 : iir;
  osr_ = osr > 4 ? 2 : osr;
  ok_ = try_addr(wire, 0x76) || try_addr(wire, 0x77);
  return ok_;
}

bool Bmp280::try_addr(TwoWire* wire, uint8_t addr) {
  dev_ = I2cDevice(wire, addr);
  if (!dev_.present()) return false;
  if (!dev_.read8(kRegChipId, &chip_id_)) return false;
  if (chip_id_ != 0x58 && chip_id_ != 0x60) return false;  // BMP280 / BME280

  dev_.write8(kRegReset, 0xB6);
  delay(5);
  uint8_t c[24];
  if (!dev_.read(kRegCalib, c, sizeof(c))) return false;
  t1_ = u16le(&c[0]);
  t2_ = s16le(&c[2]);
  t3_ = s16le(&c[4]);
  p1_ = u16le(&c[6]);
  p2_ = s16le(&c[8]);
  p3_ = s16le(&c[10]);
  p4_ = s16le(&c[12]);
  p5_ = s16le(&c[14]);
  p6_ = s16le(&c[16]);
  p7_ = s16le(&c[18]);
  p8_ = s16le(&c[20]);
  p9_ = s16le(&c[22]);
  if (t1_ == 0 || p1_ == 0) return false;  // calibration not readable

  if (!dev_.write8(kRegConfig, config_reg(iir_))) return false;  // config before mode
  if (!dev_.write8(kRegCtrlMeas, ctrl_meas(osr_))) return false;
  delay(osr_ >= 3 ? 50 : 20);  // first conversion
  return true;
}

bool Bmp280::read(float* pressure_pa, float* temp_c) {
  if (!ok_) return false;
  uint8_t b[6];
  if (!dev_.read(kRegPressMsb, b, sizeof(b))) return false;
  const int32_t adc_p = (static_cast<int32_t>(b[0]) << 12) | (b[1] << 4) | (b[2] >> 4);
  const int32_t adc_t = (static_cast<int32_t>(b[3]) << 12) | (b[4] << 4) | (b[5] >> 4);
  if (adc_p == 0x80000 || adc_t == 0x80000) return false;  // no conversion yet / skipped

  // Datasheet 8.1, double precision (only ~20 flops at 100 Hz).
  double v1 = (adc_t / 16384.0 - t1_ / 1024.0) * t2_;
  double v2 = (adc_t / 131072.0 - t1_ / 8192.0) * (adc_t / 131072.0 - t1_ / 8192.0) * t3_;
  const double t_fine = v1 + v2;
  *temp_c = static_cast<float>(t_fine / 5120.0);

  v1 = t_fine / 2.0 - 64000.0;
  v2 = v1 * v1 * p6_ / 32768.0;
  v2 = v2 + v1 * p5_ * 2.0;
  v2 = v2 / 4.0 + p4_ * 65536.0;
  v1 = (p3_ * v1 * v1 / 524288.0 + p2_ * v1) / 524288.0;
  v1 = (1.0 + v1 / 32768.0) * p1_;
  if (v1 == 0.0) return false;
  double p = 1048576.0 - adc_p;
  p = (p - v2 / 4096.0) * 6250.0 / v1;
  v1 = p9_ * p * p / 2147483648.0;
  v2 = p * p8_ / 32768.0;
  p = p + (v1 + v2 + p7_) / 16.0;
  *pressure_pa = static_cast<float>(p);
  return true;
}

void Bmp280::sleep() {
  if (ok_) dev_.write8(kRegCtrlMeas, 0x00);  // mode bits 00 = sleep
}

}  // namespace hal
