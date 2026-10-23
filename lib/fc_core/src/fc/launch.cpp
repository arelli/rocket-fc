#include "fc/launch.h"

namespace fc {

const char* to_string(LaunchEnd e) {
  switch (e) {
    case LaunchEnd::None: return "ok";
    case LaunchEnd::Fired: return "fired";
    case LaunchEnd::NotArmed: return "not armed (ARM switch)";
    case LaunchEnd::NoContinuity: return "no igniter continuity";
    case LaunchEnd::RocketNotReady: return "rocket not ready (not heard in PAD)";
    case LaunchEnd::Aborted: return "aborted";
    case LaunchEnd::Busy: return "busy";
  }
  return "?";
}

LaunchEnd LaunchControl::check(const LaunchInputs& in) {
  if (!in.armed) return LaunchEnd::NotArmed;
  if (!in.continuity) return LaunchEnd::NoContinuity;
  if (!in.bench && in.need_pad && !in.rocket_ready) return LaunchEnd::RocketNotReady;
  return LaunchEnd::None;
}

LaunchEnd LaunchControl::start(uint32_t now_ms, const LaunchInputs& in, uint32_t countdown_ms) {
  if (state_ != LaunchState::Idle) return LaunchEnd::Busy;
  const LaunchEnd why = check(in);
  if (why != LaunchEnd::None) return why;
  state_ = LaunchState::Countdown;
  t0_ = now_ms;
  countdown_ms_ = countdown_ms;
  last_tick_ = -1;
  abort_req_ = false;
  return LaunchEnd::None;
}

uint32_t LaunchControl::remaining_ms(uint32_t now_ms) const {
  if (state_ != LaunchState::Countdown) return 0;
  const uint32_t el = now_ms - t0_;
  return el >= countdown_ms_ ? 0 : countdown_ms_ - el;
}

LaunchControl::Output LaunchControl::update(uint32_t now_ms, const LaunchInputs& in, uint32_t pulse_ms) {
  Output o;
  if (state_ == LaunchState::Countdown) {
    LaunchEnd why = abort_req_ ? LaunchEnd::Aborted : check(in);
    if (why != LaunchEnd::None) {
      state_ = LaunchState::Idle;
      abort_req_ = false;
      o.ended = why;
      return o;
    }
    const uint32_t rem = remaining_ms(now_ms);
    if (rem == 0) {
      state_ = LaunchState::Firing;
      t0_ = now_ms;
      o.fired_now = true;
    } else {
      const int32_t s = static_cast<int32_t>((rem + 999) / 1000);
      if (s != last_tick_) {
        last_tick_ = s;
        o.tick_s = s;
      }
      return o;
    }
  }
  if (state_ == LaunchState::Firing) {
    abort_req_ = false;  // too late to abort: the pulse is short
    if (!in.armed || now_ms - t0_ >= pulse_ms) {
      state_ = LaunchState::Idle;
      o.ended = LaunchEnd::Fired;
      return o;
    }
    o.gate = true;
  }
  return o;
}

}  // namespace fc
