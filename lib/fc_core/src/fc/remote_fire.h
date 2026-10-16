// Remote test fire of the pyro output over the radio (ejection charge ground
// test), only in BENCH MODE (off at every boot; see docs/FLIGHT_LOGIC.md).
//
// Protocol (docs/TELEMETRY.md, "Remote test fire"):
//   1. ground -> FireArm(pulse_ms)
//   2. rocket checks it may arm (bench mode, PAD, igniter continuity), picks a
//      random nonce, reports REMOTE_ARMED(nonce) and sounds a warning beep
//   3. ground -> Fire(code = remote_fire_code(nonce, key), pulse_ms) within
//      kWindowMs; the rocket fires once and forgets the nonce
// Anything else (timeout, wrong code, other pulse length, conditions lost,
// FireDisarm) disarms. The code proves the request comes from a ground
// station built with the same key and answers this particular arming. It is
// NOT cryptographically strong: the physical arm link is the real safety.
#pragma once

#include <stdint.h>

namespace fc {

// Why a remote fire request was refused or an arming ended (event argument).
enum class RemoteFireEnd : uint8_t {
  Fired = 0,
  NotBenchMode = 1,  // value 1 was "flight build" before bench mode replaced the bench build
  NotOnPad = 2,
  NoContinuity = 3,
  BadPulse = 4,
  NotArmed = 5,
  BadCode = 6,
  Timeout = 7,
  Cancelled = 8,
  Busy = 9,
};

const char* to_string(RemoteFireEnd r);

// Keyed mixing of the nonce (splitmix-style). Same function on both ends.
uint32_t remote_fire_code(uint32_t nonce, uint32_t key);
// Code of a bench mode on/off request by radio (fixed per key and direction).
inline uint32_t bench_mode_code(bool on, uint32_t key) {
  return remote_fire_code(0xBE4C4D00u | (on ? 1u : 0u), key);
}

class RemoteFire {
 public:
  static constexpr uint32_t kWindowMs = 10000;
  static constexpr uint16_t kMaxPulseMs = 2000;

  explicit RemoteFire(uint32_t key) : key_(key) {}

  // Step 1. `nonce` must be random. Returns Fired (= OK) when armed, otherwise
  // the refusal reason. `may_arm` is the caller's verdict on the conditions
  // (bench mode, PAD, continuity, output idle): Fired = all fine.
  RemoteFireEnd arm(uint32_t now_ms, uint16_t pulse_ms, uint32_t nonce, RemoteFireEnd may_arm);

  // Step 2. `may_fire`: the same conditions, checked again now. Returns Fired
  // if the caller must pulse the output now; any other result disarms.
  RemoteFireEnd fire(uint32_t now_ms, uint32_t code, uint16_t pulse_ms, RemoteFireEnd may_fire);

  // Call periodically: returns true (and disarms) when the window has expired.
  bool expired(uint32_t now_ms);
  void disarm() { armed_ = false; }

  bool armed() const { return armed_; }
  uint32_t nonce() const { return nonce_; }
  uint16_t pulse_ms() const { return pulse_ms_; }
  uint32_t remaining_ms(uint32_t now_ms) const {
    return armed_ && now_ms - t_arm_ < kWindowMs ? kWindowMs - (now_ms - t_arm_) : 0;
  }

 private:
  uint32_t key_;
  bool armed_ = false;
  uint32_t nonce_ = 0;
  uint16_t pulse_ms_ = 0;
  uint32_t t_arm_ = 0;
};

}  // namespace fc
