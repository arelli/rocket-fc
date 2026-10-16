// Deployment charge output (low-side N-MOSFET, see docs/HARDWARE.md).
//
// Layers of protection, outermost first:
//   1. Physical arm link in the igniter circuit (fit it on the pad only).
//   2. 100k gate pull-down: the MOSFET stays off while the ESP32 boots,
//      resets or sleeps (GPIOs float during those times).
//   3. begin() drives the gate low before anything else runs.
//   4. Software arming: fire() is refused unless the flight state machine has
//      armed the channel (it only does so after launch detection).
//   5. Single shot and a hard pulse limit enforced here, independent of the
//      caller.
#pragma once

#include <Arduino.h>

namespace hal {

class PyroChannel {
 public:
  static constexpr uint32_t kMaxPulseMs = 2000;

  // Call FIRST in setup().
  void begin(int gate_pin);

  void set_armed(bool armed) { armed_ = armed; }

  // Starts the pulse if armed and not fired before. Returns true if started.
  bool fire(uint32_t now_ms, uint32_t pulse_ms);

  // Test pulse without flight arming (nichrome/LED/ejection tests). The
  // caller allows it only in bench mode on the ground. Does not use up the
  // flight shot.
  bool bench_fire(uint32_t now_ms, uint32_t pulse_ms);

  // Ends the pulse when its time is up. Call every loop iteration.
  void update(uint32_t now_ms);

  void force_off();

  bool armed() const { return armed_; }
  bool firing() const { return firing_; }
  bool fired() const { return fired_; }
  uint32_t fire_time() const { return t_fire_; }

 private:
  void start(uint32_t now_ms, uint32_t pulse_ms);

  int pin_ = -1;
  bool armed_ = false;
  bool firing_ = false;
  bool fired_ = false;
  uint32_t t_fire_ = 0;
  uint32_t pulse_ms_ = 0;
};

}  // namespace hal
