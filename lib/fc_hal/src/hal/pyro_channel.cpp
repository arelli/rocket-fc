#include "hal/pyro_channel.h"

namespace hal {

void PyroChannel::begin(int gate_pin) {
  pin_ = gate_pin;
  digitalWrite(pin_, LOW);  // set the output latch low before enabling the driver
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
  armed_ = firing_ = fired_ = false;
}

void PyroChannel::start(uint32_t now_ms, uint32_t pulse_ms) {
  if (pulse_ms > kMaxPulseMs) pulse_ms = kMaxPulseMs;
  pulse_ms_ = pulse_ms;
  t_fire_ = now_ms;
  firing_ = true;
  fired_ = true;
  digitalWrite(pin_, HIGH);
}

bool PyroChannel::fire(uint32_t now_ms, uint32_t pulse_ms) {
  if (pin_ < 0 || !armed_ || fired_) return false;
  start(now_ms, pulse_ms);
  return true;
}

#ifdef FC_BENCH_BUILD
bool PyroChannel::bench_fire(uint32_t now_ms, uint32_t pulse_ms) {
  if (pin_ < 0 || firing_) return false;
  start(now_ms, pulse_ms);
  fired_ = false;  // bench pulses do not consume the flight shot
  return true;
}
#endif

void PyroChannel::update(uint32_t now_ms) {
  if (pin_ < 0) return;
  if (firing_ && now_ms - t_fire_ >= pulse_ms_) firing_ = false;
  // Re-assert the pin every iteration: the output can only be HIGH while a
  // pulse is in progress.
  digitalWrite(pin_, firing_ ? HIGH : LOW);
}

void PyroChannel::force_off() {
  firing_ = false;
  if (pin_ >= 0) digitalWrite(pin_, LOW);
}

}  // namespace hal
