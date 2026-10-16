#include "fc/indicators.h"

namespace fc {

namespace {
// Each pattern is a list of (on, off) pairs repeated forever.
struct Pair {
  uint16_t on, off;
};
constexpr Pair kInit[] = {{50, 50}};
constexpr Pair kFault[] = {{100, 100}, {100, 100}, {100, 1000}};
constexpr Pair kPadSafe[] = {{100, 900}};
constexpr Pair kPadReady[] = {{100, 150}, {100, 650}};
constexpr Pair kLanded[] = {{50, 1950}};
constexpr Pair kLowBatt[] = {{500, 500}};

bool play(const Pair* p, size_t n, uint32_t t) {
  uint32_t period = 0;
  for (size_t i = 0; i < n; ++i) period += p[i].on + p[i].off;
  uint32_t x = t % period;
  for (size_t i = 0; i < n; ++i) {
    if (x < p[i].on) return true;
    x -= p[i].on;
    if (x < p[i].off) return false;
    x -= p[i].off;
  }
  return false;
}

template <size_t N>
bool play(const Pair (&p)[N], uint32_t t) {
  return play(p, N, t);
}
}  // namespace

bool led_on(LedMode mode, uint32_t t) {
  switch (mode) {
    case LedMode::Off: return false;
    case LedMode::Init: return play(kInit, t);
    case LedMode::Fault: return play(kFault, t);
    case LedMode::PadSafe: return play(kPadSafe, t);
    case LedMode::PadReady: return play(kPadReady, t);
    case LedMode::Flight: return true;
    case LedMode::Landed: return play(kLanded, t);
    case LedMode::LowBattery: return play(kLowBatt, t);
  }
  return false;
}

bool BeepSequencer::push(uint16_t on_ms, uint16_t off_ms) {
  if (count_ >= kCapacity) return false;
  q_[(head_ + count_) % kCapacity] = Segment{on_ms, off_ms};
  ++count_;
  return true;
}

bool BeepSequencer::beeps(uint8_t n, uint16_t on_ms, uint16_t off_ms, uint16_t gap_ms) {
  bool ok = true;
  for (uint8_t i = 0; i < n; ++i) ok &= push(on_ms, off_ms);
  if (gap_ms) ok &= pause(gap_ms);
  return ok;
}

bool BeepSequencer::digits(uint32_t value) {
  char buf[11];
  int n = 0;
  do {
    buf[n++] = static_cast<char>(value % 10);
    value /= 10;
  } while (value && n < 10);
  bool ok = true;
  for (int i = n - 1; i >= 0; --i) {
    const uint8_t d = static_cast<uint8_t>(buf[i]);
    if (d == 0) {
      ok &= push(600, 400);
    } else {
      for (uint8_t k = 0; k < d; ++k) ok &= push(150, 250);
    }
    ok &= pause(800);
  }
  return ok;
}

void BeepSequencer::clear() {
  head_ = count_ = 0;
  active_ = false;
}

bool BeepSequencer::update(uint32_t now) {
  for (;;) {
    if (!active_) {
      if (count_ == 0) return false;
      cur_ = q_[head_];
      head_ = (head_ + 1) % kCapacity;
      --count_;
      cur_start_ = now;
      active_ = true;
    }
    const uint32_t dt = now - cur_start_;
    if (dt < cur_.on_ms) return true;
    if (dt < static_cast<uint32_t>(cur_.on_ms) + cur_.off_ms) return false;
    // Segment finished; start the next one exactly where this one ended.
    active_ = false;
    const uint32_t end = cur_start_ + cur_.on_ms + cur_.off_ms;
    if (count_ == 0) return false;
    cur_ = q_[head_];
    head_ = (head_ + 1) % kCapacity;
    --count_;
    cur_start_ = end;
    active_ = true;
  }
}

}  // namespace fc
