// The complete decision pipeline, independent of hardware:
//   raw sensor values -> Estimator -> FlightStateMachine -> fire command.
// The firmware and the unit tests drive exactly this class, so what is tested
// is what flies.
#pragma once

#include <stdint.h>

#include "fc/estimator.h"
#include "fc/flight_sm.h"
#include "fc/resume.h"

namespace fc {

class FlightComputer {
 public:
  struct Config {
    FlightConfig flight;
    Estimator::Params estimator;
    uint32_t settle_ms = 5000;  // INIT countdown: keep the rocket still
    uint32_t calib_ms = 2000;   // last part of the countdown averages ground pressure
  };

  explicit FlightComputer(const Config& c) : cfg_(c), est_(c.estimator), sm_(c.flight) {}

  void begin(uint32_t t_ms);

  // True once the INIT countdown has elapsed (time to run the self-test).
  bool settle_done(uint32_t t_ms) const {
    return sm_.state() == FlightState::Init && t_ms - t_begin_ >= cfg_.settle_ms;
  }
  // Seconds left in the countdown (for the countdown beeps).
  uint32_t settle_remaining_ms(uint32_t t_ms) const;

  // Ends INIT: fixes the ground reference and enters PAD or FAULT.
  // Returns false (and enters FAULT) if no barometer samples were collected.
  bool finish_init(bool selftest_ok);

  // Resume a flight after an unexpected reset.
  void resume(const FlightRecord& rec, uint32_t now_ms);

  // One control-loop iteration.
  SmOutput tick(uint32_t t_ms, bool baro_ok, float pressure_pa, bool imu_ok,
                const Vec3& accel_g);

  // Snapshot for the RTC-RAM flight record.
  FlightRecord record(uint32_t now_ms) const;

  // The charge was fired by a manual command (see FlightStateMachine).
  void note_manual_fire() { sm_.note_manual_fire(); }

  const Estimator& estimator() const { return est_; }
  const FlightStateMachine& sm() const { return sm_; }
  FlightState state() const { return sm_.state(); }

 private:
  Config cfg_;
  Estimator est_;
  FlightStateMachine sm_;
  uint32_t t_begin_ = 0;
  uint32_t t_last_ = 0;
  bool have_last_ = false;
};

}  // namespace fc
