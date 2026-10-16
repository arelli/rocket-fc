// Ground -> rocket heartbeat link (docs/TELEMETRY.md, "Uplink").
//
// Protocol: whenever the ground station receives a rocket frame while the
// rocket is on the ground, it may answer with a heartbeat (at most once per
// `min_interval_ms`). The rocket listens for a short window after each of its
// transmissions, so the reply arrives while it is receiving.
//
// The heartbeat carries only the pad beep period. It cannot arm, fire, or
// change anything the flight logic uses, and the rocket ignores it in flight.
#pragma once

#include <stdint.h>

#include "fc/types.h"

namespace fc {

// Rocket side: is the ground station hearing us (and we it)?
class GroundLink {
 public:
  explicit GroundLink(uint32_t timeout_ms = 5000) : timeout_ms_(timeout_ms) {}

  void heard(uint32_t now_ms, int8_t rssi_dbm, uint8_t beep_period_s) {
    last_ms_ = now_ms;
    have_ = true;
    rssi_ = rssi_dbm;
    beep_period_s_ = beep_period_s;
  }
  bool connected(uint32_t now_ms) const { return have_ && now_ms - last_ms_ < timeout_ms_; }
  // Period to use for the pad status beeps now: 0 = silent (no link, or disabled).
  uint8_t pad_beep_period_s(uint32_t now_ms) const {
    return connected(now_ms) ? beep_period_s_ : 0;
  }
  int8_t rssi_dbm(uint32_t now_ms) const { return connected(now_ms) ? rssi_ : -128; }

 private:
  uint32_t timeout_ms_;
  uint32_t last_ms_ = 0;
  bool have_ = false;
  int8_t rssi_ = -128;
  uint8_t beep_period_s_ = 0;
};

// Ground side: should we answer this rocket frame with a heartbeat?
class UplinkPolicy {
 public:
  explicit UplinkPolicy(uint32_t min_interval_ms = 900) : min_interval_ms_(min_interval_ms) {}

  // Call when a valid rocket frame arrives. Returns true if a heartbeat
  // should be sent now (and records that it was).
  bool on_rocket_frame(FlightState rocket_state, uint32_t now_ms) {
    if (!enabled_) return false;
    if (rocket_state == FlightState::Boost || rocket_state == FlightState::Coast ||
        rocket_state == FlightState::Descent)
      return false;  // never transmit at a flying rocket: it is not listening
    if (sent_ && now_ms - last_ms_ < min_interval_ms_) return false;
    sent_ = true;
    last_ms_ = now_ms;
    return true;
  }
  void set_enabled(bool on) { enabled_ = on; }
  bool enabled() const { return enabled_; }

 private:
  uint32_t min_interval_ms_;
  uint32_t last_ms_ = 0;
  bool sent_ = false;
  bool enabled_ = true;
};

}  // namespace fc
