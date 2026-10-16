#include "fc/frame_ring.h"

#include <string.h>

namespace fc {

void FrameRing::attach(uint8_t* mem, size_t bytes) {
  mem_ = mem;
  slots_ = mem ? bytes / kSlot : 0;
  clear();
}

void FrameRing::clear() {
  head_ = 0;
  count_ = 0;
  overwritten_ = 0;
}

bool FrameRing::push(const uint8_t* frame, size_t len) {
  if (!ok() || len == 0 || len > kSlot - 1) return false;
  uint8_t* s = mem_ + head_ * kSlot;
  s[0] = static_cast<uint8_t>(len);
  memcpy(s + 1, frame, len);
  head_ = (head_ + 1) % slots_;
  if (count_ < slots_) {
    ++count_;
  } else {
    ++overwritten_;
  }
  return true;
}

size_t FrameRing::get(size_t i, uint8_t* out, size_t cap) const {
  if (i >= count_) return 0;
  const size_t oldest = (head_ + slots_ - count_) % slots_;
  const uint8_t* s = mem_ + ((oldest + i) % slots_) * kSlot;
  const size_t len = s[0];
  if (len > cap) return 0;
  memcpy(out, s + 1, len);
  return len;
}

size_t FrameRing::pop(uint8_t* out, size_t cap) {
  if (count_ == 0) return 0;
  const size_t len = get(0, out, cap);
  --count_;
  return len;
}

}  // namespace fc
