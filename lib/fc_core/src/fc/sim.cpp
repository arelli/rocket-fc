#include "fc/sim.h"

#include "fc/altitude.h"

namespace fc {

SimFlight::SimFlight(const SimParams& p) : p_(p), rng_(p.seed ? p.seed : 1) {}

float SimFlight::gauss() {
  // Irwin-Hall approximation of N(0,1) from a xorshift32 generator.
  float s = 0;
  for (int i = 0; i < 12; ++i) {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    s += static_cast<float>(rng_) / 4294967296.0f;
  }
  return s - 6.0f;
}

void SimFlight::deploy() { deployed_ = true; }

SimSample SimFlight::step(uint32_t dt_ms) {
  const double g = kGravity;
  for (uint32_t i = 0; i < dt_ms; ++i) {
    ++t_ms_;
    const double dt = 0.001;
    if (t_ms_ < p_.ignition_ms || landed_) {
      acc_ = 0;
      vel_ = 0;
      continue;
    }
    const double tb = (t_ms_ - p_.ignition_ms) / 1000.0;
    const double thrust = tb < p_.burn_s ? p_.thrust_accel_mps2 : 0.0;
    double drag;
    if (deployed_ && vel_ < 0) {
      const double kc = g / (static_cast<double>(p_.descent_mps) * p_.descent_mps);
      drag = kc * vel_ * vel_;  // parachute, opposing the (downward) motion
    } else {
      drag = p_.drag_k * vel_ * vel_ * (vel_ > 0 ? -1.0 : 1.0);
    }
    acc_ = thrust - g + drag;
    if (!launched_ && acc_ <= 0) {  // still sitting on the pad
      acc_ = 0;
      continue;
    }
    launched_ = true;
    const double v_old = vel_;
    vel_ += acc_ * dt;
    alt_ += vel_ * dt;
    if (!apogee_seen_ && v_old > 0 && vel_ <= 0) {
      apogee_seen_ = true;
      apogee_alt_ = static_cast<float>(alt_);
      apogee_ms_ = t_ms_;
    }
    if (alt_ <= 0 && apogee_seen_) {
      alt_ = 0;
      vel_ = 0;
      acc_ = 0;
      landed_ = true;
    }
  }

  SimSample s;
  s.t_ms = t_ms_;
  s.alt_m = static_cast<float>(alt_);
  s.vel_mps = static_cast<float>(vel_);
  const float meas_alt = s.alt_m + baro_offset_m_ + p_.baro_noise_m * gauss();
  s.pressure_pa = altitude_to_pressure_pa(meas_alt, p_.ground_pa);
  // Axial specific force: what an accelerometer on the rocket axis reads.
  const float f = static_cast<float>((acc_ + g) / g);
  s.accel_g.x = f + accel_spike_g_ + p_.accel_noise_g * gauss();
  s.accel_g.y = p_.accel_noise_g * gauss();
  s.accel_g.z = p_.accel_noise_g * gauss();
  return s;
}

}  // namespace fc
