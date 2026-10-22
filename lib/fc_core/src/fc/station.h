// Ground station panel logic (push button, search mode beep rate): pure
// logic, tested on the PC (test/test_station). Wiring: docs/HARDWARE.md.
#pragma once

#include <stdint.h>

namespace fc {

// Debounced push button with a short press and one long hold action.
//   Short: released after less than kShortMaxMs (reported on release)
//   Long:  held for long_ms (reported once, while still held)
// A release between kShortMaxMs and long_ms does nothing (changed my mind).
class PanelButton {
 public:
  static constexpr uint32_t kDebounceMs = 30;
  static constexpr uint32_t kShortMaxMs = 1000;

  enum class Action : uint8_t { None, Short, Long };

  // `raw_pressed`: the pin level (true = pressed). Call every loop.
  Action update(bool raw_pressed, uint32_t now_ms, uint32_t long_ms);

  bool pressed() const { return stable_; }
  // How long the current press has lasted (0 when released).
  uint32_t held_ms(uint32_t now_ms) const { return stable_ ? now_ms - t_press_ : 0; }

 private:
  bool raw_ = false;
  bool stable_ = false;
  bool long_done_ = false;
  uint32_t t_raw_ = 0;
  uint32_t t_press_ = 0;
};

// Search mode: interval between beeps for a signal strength. At or below
// lo_dbm: kSlowMs; at or above hi_dbm: kFastMs; linear in dBm between.
struct SearchBeep {
  static constexpr uint32_t kSlowMs = 1500;
  static constexpr uint32_t kFastMs = 80;
  static uint32_t interval_ms(float rssi_dbm, int32_t lo_dbm, int32_t hi_dbm);
};

}  // namespace fc
