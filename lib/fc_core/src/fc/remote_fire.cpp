#include "fc/remote_fire.h"

namespace fc {

const char* to_string(RemoteFireEnd r) {
  switch (r) {
    case RemoteFireEnd::Fired: return "fired";
    case RemoteFireEnd::NotBenchMode: return "not in bench mode";
    case RemoteFireEnd::NotOnPad: return "not in PAD state";
    case RemoteFireEnd::NoContinuity: return "no igniter continuity (arm link out?)";
    case RemoteFireEnd::BadPulse: return "pulse length out of range";
    case RemoteFireEnd::NotArmed: return "not armed";
    case RemoteFireEnd::BadCode: return "wrong code";
    case RemoteFireEnd::Timeout: return "fire window expired";
    case RemoteFireEnd::Cancelled: return "cancelled";
    case RemoteFireEnd::Busy: return "pyro output busy";
  }
  return "?";
}

uint32_t remote_fire_code(uint32_t nonce, uint32_t key) {
  uint64_t z = (static_cast<uint64_t>(key) << 32 | nonce) + 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  z ^= z >> 31;
  return static_cast<uint32_t>(z ^ (z >> 32));
}

RemoteFireEnd RemoteFire::arm(uint32_t now_ms, uint16_t pulse_ms, uint32_t nonce,
                              RemoteFireEnd may_arm) {
  armed_ = false;
  if (may_arm != RemoteFireEnd::Fired) return may_arm;
  if (pulse_ms == 0 || pulse_ms > kMaxPulseMs) return RemoteFireEnd::BadPulse;
  armed_ = true;
  nonce_ = nonce;
  pulse_ms_ = pulse_ms;
  t_arm_ = now_ms;
  return RemoteFireEnd::Fired;
}

RemoteFireEnd RemoteFire::fire(uint32_t now_ms, uint32_t code, uint16_t pulse_ms,
                               RemoteFireEnd may_fire) {
  if (!armed_) return RemoteFireEnd::NotArmed;
  armed_ = false;  // one attempt per arming, whatever happens
  if (now_ms - t_arm_ >= kWindowMs) return RemoteFireEnd::Timeout;
  if (may_fire != RemoteFireEnd::Fired) return may_fire;
  if (pulse_ms != pulse_ms_) return RemoteFireEnd::BadPulse;
  if (code != remote_fire_code(nonce_, key_)) return RemoteFireEnd::BadCode;
  return RemoteFireEnd::Fired;
}

bool RemoteFire::expired(uint32_t now_ms) {
  if (armed_ && now_ms - t_arm_ >= kWindowMs) {
    armed_ = false;
    return true;
  }
  return false;
}

}  // namespace fc
