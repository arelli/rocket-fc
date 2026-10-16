// Flight configuration: every tunable number of the flight computer.
//
// Change values here (not in the code), commit, and the git hash stamped into
// the firmware identifies exactly which configuration flew. After changing
// anything in the "flight logic" section, run the tests:  pio test -e native
// (they use the same defaults through fc::FlightConfig) and re-read
// docs/FLIGHT_LOGIC.md.
#pragma once

#include <stdint.h>

#include "fc/battery.h"
#include "fc/flight_computer.h"

namespace cfg {

// ---------------------------------------------------------------------------
// Mounting: which MPU9250 axis points toward the nose of the rocket.
// Check on the bench: stand the rocket nose-up, the console "status" command
// must show accel_up close to +1.00 g.
constexpr fc::Axis kImuUpAxis = fc::Axis::PosX;

// ---------------------------------------------------------------------------
// Flight logic
constexpr uint32_t kSettleMs = 5000;  // power-on countdown (keep the rocket still)
constexpr uint32_t kCalibMs = 2000;   // ...last 2 s average the ground pressure

// Backup deployment: fire this long after launch if apogee was not detected
// (e.g. barometer failure). 0 = disabled. Take your simulator's (OpenRocket)
// apogee time and add 1-2 s. Too short = deployment at high speed.
constexpr uint32_t kBackupDeployMs = 0;

inline fc::FlightComputer::Config flight_computer() {
  fc::FlightComputer::Config c;
  c.settle_ms = kSettleMs;
  c.calib_ms = kCalibMs;
  c.estimator.up_axis = kImuUpAxis;
  c.estimator.filter.baro_sigma_m = 0.5f;
  c.estimator.filter.accel_sigma_mps2 = 4.0f;
  c.estimator.filter.no_imu_sigma_mps2 = 20.0f;
  c.flight.launch_accel_g = 2.5f;
  c.flight.launch_accel_ms = 100;
  c.flight.launch_alt_m = 30.0f;
  c.flight.launch_alt_ms = 500;
  c.flight.burnout_accel_g = 0.5f;
  c.flight.burnout_ms = 100;
  c.flight.apogee_lockout_ms = 1500;
  c.flight.apogee_confirm_ms = 100;
  c.flight.apogee_drop_m = 0.5f;
  c.flight.min_deploy_alt_m = 10.0f;
  c.flight.backup_deploy_ms = kBackupDeployMs;
  c.flight.landed_ms = 5000;
  c.flight.landed_window_m = 2.0f;
  return c;
}

// ---------------------------------------------------------------------------
// Pyro
constexpr uint32_t kPyroPulseMs = 1000;  // nichrome on-time (driver caps it at 2000)
// Continuity: drain voltage above this fraction of the battery voltage means
// the arm link is fitted and the bridge wire is intact.
constexpr float kContinuityFraction = 0.6f;

// ---------------------------------------------------------------------------
// Battery (cell soldered to the XIAO BAT pads, behind the power switch)
// Divider ratio = (R_top + R_bottom) / R_bottom. Calibrate: measure the cell
// with a multimeter, compare with "status", adjust.
constexpr float kBattDividerRatio = 2.0f;
constexpr float kPyroDividerRatio = 2.0f;
inline fc::BatteryMonitor::Params battery() {
  fc::BatteryMonitor::Params p;
  p.low_v = 3.60f;
  p.critical_v = 3.40f;
  p.hysteresis_v = 0.05f;
  p.debounce_ms = 3000;
  return p;
}
// On the pad or after landing, a battery critical for this long puts the
// computer into deep sleep to protect the cell (the XIAO has no cutoff).
constexpr uint32_t kBattShutdownMs = 30000;

// ---------------------------------------------------------------------------
// Timing
constexpr uint32_t kLoopMs = 10;          // 100 Hz control loop
constexpr uint32_t kSlowMs = 100;         // 10 Hz: ADC, battery, continuity
constexpr uint32_t kTlmPadMs = 1000;      // telemetry period on the pad
constexpr uint32_t kTlmFlightMs = 200;    // ...in flight (5 Hz; duty limiter permitting)
constexpr uint32_t kTlmLandedMs = 5000;   // ...after landing (beacon)
constexpr uint32_t kLogFlightMs = 20;     // onboard log rate in flight (50 Hz)
constexpr uint32_t kLogPadMs = 1000;      // ...on the pad / after landing
constexpr uint32_t kPadReportMs = 10000;  // status beeps: fault code, landed altitude
// Pad status beeps (continuity) sound only while the ground station is heard;
// their period comes from its heartbeat (set in the ground station UI).
constexpr uint32_t kUplinkTimeoutMs = 5000;  // ground counts as lost after this silence
constexpr uint32_t kUplinkRxWindowMs = 150;  // on the ground: listen this long after each TX
constexpr uint32_t kWatchdogS = 2;        // reset if the loop stalls this long

}  // namespace cfg
