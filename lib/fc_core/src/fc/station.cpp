#include "fc/station.h"

namespace fc {

PanelButton::Action PanelButton::update(bool raw_pressed, uint32_t now_ms, uint32_t long_ms) {
  if (raw_pressed != raw_) {
    raw_ = raw_pressed;
    t_raw_ = now_ms;
  }
  Action a = Action::None;
  if (raw_ != stable_ && now_ms - t_raw_ >= kDebounceMs) {
    stable_ = raw_;
    if (stable_) {
      t_press_ = t_raw_;  // the press started when the level first changed
      long_done_ = false;
    } else if (!long_done_ && t_raw_ - t_press_ < kShortMaxMs) {
      a = Action::Short;
    }
  }
  if (stable_ && !long_done_ && now_ms - t_press_ >= long_ms) {
    long_done_ = true;
    a = Action::Long;
  }
  return a;
}

uint32_t SearchBeep::interval_ms(float rssi_dbm, int32_t lo_dbm, int32_t hi_dbm) {
  if (hi_dbm <= lo_dbm) return kSlowMs;
  const float lo = static_cast<float>(lo_dbm), hi = static_cast<float>(hi_dbm);
  if (!(rssi_dbm > lo)) return kSlowMs;  // also NaN
  if (rssi_dbm >= hi) return kFastMs;
  const float f = (rssi_dbm - lo) / (hi - lo);
  return static_cast<uint32_t>(static_cast<float>(kSlowMs) - f * static_cast<float>(kSlowMs - kFastMs) + 0.5f);
}

}  // namespace fc
