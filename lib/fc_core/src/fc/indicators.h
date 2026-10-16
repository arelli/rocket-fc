// Status LED patterns (continuous state) and buzzer sequences (changes,
// errors, countdowns, recovery beacon). Pure timing logic: the HAL only turns
// pins on/off as told. Meanings are documented in docs/INDICATORS.md.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fc {

enum class LedMode : uint8_t {
  Off,
  Init,        // fast blink: calibrating, keep the rocket still
  Fault,       // 3 quick flashes, pause: refusing to fly
  PadSafe,     // 1 flash / s: ready, no igniter continuity
  PadReady,    // 2 flashes / s: ready, igniter continuity OK
  Flight,      // solid on
  Landed,      // short flash every 2 s
  LowBattery,  // slow 50 % blink
};

// Is the LED on at time t for this mode?
bool led_on(LedMode mode, uint32_t t_ms);

// A FIFO of on/off segments played back on the buzzer.
class BeepSequencer {
 public:
  static constexpr size_t kCapacity = 96;

  // Append one beep followed by silence. Returns false if the queue is full.
  bool push(uint16_t on_ms, uint16_t off_ms);
  bool pause(uint16_t ms) { return push(0, ms); }

  // n identical beeps, then `gap_ms` of silence.
  bool beeps(uint8_t n, uint16_t on_ms = 120, uint16_t off_ms = 180, uint16_t gap_ms = 600);

  // Beep out a number digit by digit (MSD first): digit d -> d short beeps,
  // 0 -> one long beep; a longer pause separates digits.
  bool digits(uint32_t value);

  void clear();
  bool busy() const { return count_ > 0 || active_; }

  // Advance playback; returns whether the buzzer should sound now.
  bool update(uint32_t now_ms);

 private:
  struct Segment {
    uint16_t on_ms;
    uint16_t off_ms;
  };
  Segment q_[kCapacity];
  size_t head_ = 0, count_ = 0;
  bool active_ = false;
  Segment cur_{0, 0};
  uint32_t cur_start_ = 0;
};

}  // namespace fc
