#include "fc/battery.h"

namespace fc {

namespace {
struct Point {
  float v;
  float pct;
};
// Typical resting curve of a Li-ion cell (piecewise linear).
constexpr Point kCurve[] = {
    {3.30f, 0.0f},  {3.40f, 2.0f},  {3.50f, 5.0f},  {3.60f, 12.0f},
    {3.70f, 28.0f}, {3.80f, 45.0f}, {3.90f, 62.0f}, {4.00f, 78.0f},
    {4.10f, 90.0f}, {4.20f, 100.0f},
};
constexpr int kN = sizeof(kCurve) / sizeof(kCurve[0]);
}  // namespace

float liion_percent(float v) {
  if (!(v > kCurve[0].v)) return 0.0f;
  if (v >= kCurve[kN - 1].v) return 100.0f;
  for (int i = 1; i < kN; ++i) {
    if (v <= kCurve[i].v) {
      const float f = (v - kCurve[i - 1].v) / (kCurve[i].v - kCurve[i - 1].v);
      return kCurve[i - 1].pct + f * (kCurve[i].pct - kCurve[i - 1].pct);
    }
  }
  return 100.0f;
}

BatteryMonitor::Level BatteryMonitor::classify(float v) const {
  // Hysteresis: once in a worse level, require a higher voltage to recover.
  const float h = p_.hysteresis_v;
  switch (level_) {
    case Level::Critical:
      if (v < p_.critical_v + h) return Level::Critical;
      return v < p_.low_v + h ? Level::Low : Level::Ok;
    case Level::Low:
      if (v < p_.critical_v) return Level::Critical;
      return v < p_.low_v + h ? Level::Low : Level::Ok;
    default:
      if (v < p_.critical_v) return Level::Critical;
      return v < p_.low_v ? Level::Low : Level::Ok;
  }
}

bool BatteryMonitor::update(float volts, uint32_t t_ms) {
  volts_ = volts;
  const Level now = classify(volts);
  if (level_ == Level::Unknown) {  // first reading is taken as-is
    level_ = now;
    pending_ = now;
    return true;
  }
  if (now == level_) {
    pending_ = now;
    return false;
  }
  if (now != pending_) {
    pending_ = now;
    pending_since_ = t_ms;
    return false;
  }
  if (t_ms - pending_since_ >= p_.debounce_ms) {
    level_ = now;
    return true;
  }
  return false;
}

const char* to_string(BatteryMonitor::Level l) {
  switch (l) {
    case BatteryMonitor::Level::Unknown: return "unknown";
    case BatteryMonitor::Level::Ok: return "ok";
    case BatteryMonitor::Level::Low: return "low";
    case BatteryMonitor::Level::Critical: return "critical";
  }
  return "?";
}

}  // namespace fc
