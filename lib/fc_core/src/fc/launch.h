// Ground station launch channel (motor igniter): countdown and firing logic.
// Pure logic, tested on the PC (test/test_launch). Hardware and wiring:
// docs/HARDWARE.md "Ground station". The ARM switch also breaks the igniter
// current in hardware; this logic is the second layer.
//
//   start  -> refused unless armed, igniter continuity, and (in FLIGHT mode
//             with need_pad) the rocket heard in PAD
//   countdown (countdown_ms, may be 0): one tick per second; aborted by
//             abort(), ARM off, continuity lost or the rocket condition lost
//   firing  -> gate on for pulse_ms (cut at once if ARM goes off)
#pragma once

#include <stdint.h>

namespace fc {

enum class LaunchEnd : uint8_t {
  None = 0,          // started / running
  Fired = 1,         // the pulse was delivered
  NotArmed = 2,      // ARM switch off
  NoContinuity = 3,  // no igniter connected (or open)
  RocketNotReady = 4,  // FLIGHT mode: rocket not heard in PAD
  Aborted = 5,       // abort command or button
  Busy = 6,          // already counting down / firing
};
const char* to_string(LaunchEnd e);

enum class LaunchState : uint8_t { Idle, Countdown, Firing };

struct LaunchInputs {
  bool armed = false;         // ARM switch on (sensed)
  bool continuity = false;    // igniter connected
  bool bench = false;         // station mode switch in BENCH: no rocket condition
  bool need_pad = true;       // setting gs_launch_need_pad
  bool rocket_ready = false;  // rocket heard recently, in PAD, in flight mode
};

class LaunchControl {
 public:
  struct Output {
    bool gate = false;          // drive the igniter MOSFET
    int32_t tick_s = -1;        // countdown: seconds left, on each new second (-1 = none)
    bool fired_now = false;     // the pulse started in this update
    LaunchEnd ended = LaunchEnd::None;  // Fired or the abort reason, once
  };

  // Why a launch may not start now (None = it may).
  static LaunchEnd check(const LaunchInputs& in);
  LaunchEnd start(uint32_t now_ms, const LaunchInputs& in, uint32_t countdown_ms);
  void abort() { abort_req_ = true; }
  Output update(uint32_t now_ms, const LaunchInputs& in, uint32_t pulse_ms);

  LaunchState state() const { return state_; }
  uint32_t remaining_ms(uint32_t now_ms) const;

 private:
  LaunchState state_ = LaunchState::Idle;
  uint32_t t0_ = 0, countdown_ms_ = 0;
  int32_t last_tick_ = -1;
  bool abort_req_ = false;
};

}  // namespace fc
