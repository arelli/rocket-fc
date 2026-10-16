// Bounds-checked little-endian serialization helpers.
// Packets are built field by field (never by casting packed structs), so the
// wire format is identical on the ESP32, on the ground station and on a PC.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fc {

class ByteWriter {
 public:
  ByteWriter(uint8_t* buf, size_t cap) : buf_(buf), cap_(cap) {}

  void u8(uint8_t v) { put(&v, 1); }
  void u16(uint16_t v) {
    uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
    put(b, 2);
  }
  void u32(uint32_t v) {
    uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                    static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
    put(b, 4);
  }
  void u64(uint64_t v) {
    u32(static_cast<uint32_t>(v));
    u32(static_cast<uint32_t>(v >> 32));
  }
  void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
  void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }

  size_t size() const { return len_; }
  bool ok() const { return ok_; }

 private:
  void put(const uint8_t* src, size_t n) {
    if (!ok_ || len_ + n > cap_) {
      ok_ = false;
      return;
    }
    for (size_t i = 0; i < n; ++i) buf_[len_ + i] = src[i];
    len_ += n;
  }

  uint8_t* buf_;
  size_t cap_;
  size_t len_ = 0;
  bool ok_ = true;
};

class ByteReader {
 public:
  ByteReader(const uint8_t* buf, size_t len) : buf_(buf), len_(len) {}

  uint8_t u8() {
    uint8_t v = 0;
    get(&v, 1);
    return v;
  }
  uint16_t u16() {
    uint8_t b[2] = {0, 0};
    get(b, 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
  }
  uint32_t u32() {
    uint8_t b[4] = {0, 0, 0, 0};
    get(b, 4);
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
  }
  uint64_t u64() {
    const uint64_t lo = u32();
    const uint64_t hi = u32();
    return lo | (hi << 32);
  }
  int16_t i16() { return static_cast<int16_t>(u16()); }
  int32_t i32() { return static_cast<int32_t>(u32()); }

  size_t pos() const { return pos_; }
  bool ok() const { return ok_; }

 private:
  void get(uint8_t* dst, size_t n) {
    if (!ok_ || pos_ + n > len_) {
      ok_ = false;
      return;
    }
    for (size_t i = 0; i < n; ++i) dst[i] = buf_[pos_ + i];
    pos_ += n;
  }

  const uint8_t* buf_;
  size_t len_;
  size_t pos_ = 0;
  bool ok_ = true;
};

// Saturating conversion of a scaled float into an integer field.
inline int16_t sat_i16(float v) {
  if (v != v) return 0;  // NaN
  if (v > 32767.0f) return 32767;
  if (v < -32768.0f) return -32768;
  return static_cast<int16_t>(v >= 0 ? v + 0.5f : v - 0.5f);
}
inline int32_t sat_i32(float v) {
  if (v != v) return 0;
  if (v > 2147483520.0f) return 2147483520;
  if (v < -2147483520.0f) return -2147483520;
  return static_cast<int32_t>(v >= 0 ? v + 0.5f : v - 0.5f);
}
inline uint16_t sat_u16(float v) {
  if (v != v || v < 0.0f) return 0;
  if (v > 65535.0f) return 65535;
  return static_cast<uint16_t>(v + 0.5f);
}
inline uint32_t sat_u32(float v) {
  if (v != v || v < 0.0f) return 0;
  if (v > 4294967040.0f) return 4294967040u;
  return static_cast<uint32_t>(v + 0.5f);
}

}  // namespace fc
