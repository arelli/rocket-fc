// Single-cell Li-ion / LiPo battery model and low-voltage supervision.
//
// The cell is wired straight to the XIAO's BAT pads, and the XIAO has no
// over-discharge cutoff, so the firmware is the only thing standing between a
// forgotten, switched-on rocket and a deeply discharged (damaged) cell.
#pragma once

#include <stdint.h>

namespace fc {

// Rough state of charge [0..100 %] from resting voltage. Under load the
// voltage sags, so treat this as an indication, not a fuel gauge.
float liion_percent(float volts);

class BatteryMonitor {
 public:
  enum class Level : uint8_t { Unknown, Ok, Low, Critical };

  struct Params {
    float low_v = 3.60f;       // warn
    float critical_v = 3.40f;  // shut down when safe (pad / landed)
    float hysteresis_v = 0.05f;
    uint32_t debounce_ms = 3000;  // ignore short sags (radio TX, pyro pulse)
  };

  BatteryMonitor() : BatteryMonitor(Params{}) {}
  explicit BatteryMonitor(const Params& p) : p_(p) {}

  // Returns true when the level changed.
  bool update(float volts, uint32_t t_ms);

  Level level() const { return level_; }
  float volts() const { return volts_; }

 private:
  Level classify(float v) const;

  Params p_;
  Level level_ = Level::Unknown;
  Level pending_ = Level::Unknown;
  uint32_t pending_since_ = 0;
  float volts_ = 0;
};

const char* to_string(BatteryMonitor::Level l);

}  // namespace fc
