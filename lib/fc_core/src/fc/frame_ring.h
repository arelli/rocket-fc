// Fixed-slot ring buffer of telemetry/event/GPS frames: the onboard RAM log of
// the current boot, and the queue in front of the flash log. When full, the
// oldest frames are overwritten.
// The memory is supplied by the caller (PSRAM on the XIAO ESP32-S3).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fc {

class FrameRing {
 public:
  static constexpr size_t kSlot = 64;  // 1 length byte + up to 63 frame bytes

  void attach(uint8_t* mem, size_t bytes);
  bool ok() const { return slots_ > 0; }

  // Returns false if the frame is too long or no memory is attached.
  bool push(const uint8_t* frame, size_t len);

  // Copies the i-th oldest frame; returns its length (0 if out of range).
  size_t get(size_t i, uint8_t* out, size_t cap) const;

  // Removes the oldest frame and copies it out; returns its length (0 = empty).
  size_t pop(uint8_t* out, size_t cap);

  void clear();
  size_t count() const { return count_; }
  size_t capacity() const { return slots_; }
  uint32_t overwritten() const { return overwritten_; }

 private:
  uint8_t* mem_ = nullptr;
  size_t slots_ = 0;
  size_t head_ = 0;  // next slot to write
  size_t count_ = 0;
  uint32_t overwritten_ = 0;
};

}  // namespace fc
