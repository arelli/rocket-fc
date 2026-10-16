// Flight computer application: wires the hardware drivers (lib/fc_hal) to the
// tested flight logic (lib/fc_core). All decisions about the flight are made
// in fc::FlightComputer; this file only moves data and drives outputs.
//
// Loop structure (non-blocking, single task):
//   every 10 ms   read IMU + baro -> FlightComputer::tick -> pyro / events
//   every 100 ms  battery, pyro continuity, LED mode, console
//   continuously  pyro pulse timing, buzzer sequencer, LED, radio TX
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "fc/battery.h"
#include "fc/flight_computer.h"
#include "fc/indicators.h"
#include "fc/radio_budget.h"
#include "fc/sim.h"
#include "fc/telemetry.h"
#include "hal/bmp280.h"
#include "hal/io.h"
#include "hal/log_sink.h"
#include "hal/mpu9250.h"
#include "hal/pyro_channel.h"
#include "hal/radio_link.h"
#include "ram_log.h"

class App {
 public:
  App();
  void setup();
  void loop();

 private:
  // --- setup / init ----------------------------------------------------------
  void init_sensors();
  void init_radio();
  void init_log();
  void start_countdown(uint32_t now);
  void try_resume(uint32_t now);
  void run_selftest(uint32_t now);

  // --- periodic work -----------------------------------------------------------
  void control_tick(uint32_t now);
  void slow_tick(uint32_t now);
  void read_sensors(uint32_t now);
  void handle_output(const fc::SmOutput& out, uint32_t now);
  void on_state_change(fc::FlightState from, fc::FlightState to, uint32_t now);
  void update_battery(uint32_t now);
  void update_continuity(uint32_t now);
  void update_indicators(uint32_t now);
  void update_radio(uint32_t now);
  void update_log(uint32_t now);
  void pad_report(uint32_t now);
  [[noreturn]] void shutdown(const char* why);

  // --- telemetry -------------------------------------------------------------
  void event(fc::EventCode code, int32_t arg, uint32_t now);
  fc::tlm::Telemetry snapshot(uint32_t now) const;
  uint8_t flags() const;
  void add_sink(hal::LogSink* s);
  void write_log(const uint8_t* frame, size_t len);
  uint32_t telemetry_period_ms() const;

  // --- console (app_console.cpp) ----------------------------------------------
  void console_poll(uint32_t now);
  void console_exec(char* line, uint32_t now);
  void print_status(uint32_t now);
  void print_config();
  void print_csv_line(uint32_t now);
  void i2c_scan();
  void dump_log();
  bool safe_to_interrupt() const;
#ifdef FC_BENCH_BUILD
  void start_sim(bool live, uint32_t now);
#endif

  // --- hardware ----------------------------------------------------------------
  hal::PyroChannel pyro_;
  hal::DigitalOut led_;
  hal::DigitalOut buzzer_;
  hal::AnalogIn batt_adc_;
  hal::AnalogIn pyro_adc_;
  hal::Mpu9250 imu_;
  hal::Bmp280 baro_;
  hal::RadioLink radio_;

  // --- logic -------------------------------------------------------------------
  fc::FlightComputer fc_;
  fc::BatteryMonitor batt_;
  fc::DutyCycleLimiter duty_;
  fc::BeepSequencer beeper_;

  // Log sinks: every telemetry/event frame goes to each of them.
  // Onboard RAM log now; an SD card sink can be added next to it.
  RamLog ram_log_;
  static constexpr size_t kMaxSinks = 4;
  hal::LogSink* sinks_[kMaxSinks] = {};
  size_t n_sinks_ = 0;

  // --- sensor state ----------------------------------------------------------------
  hal::Mpu9250::Sample imu_s_;
  bool imu_ok_ = false;    // last read succeeded
  bool baro_ok_ = false;   // last read succeeded
  float pressure_pa_ = 0;
  float temp_c_ = 0;
  uint16_t imu_fail_run_ = 0, baro_fail_run_ = 0;
  uint8_t lost_reported_ = 0;  // SensorBits already reported lost
  uint32_t baro_good_ = 0;     // successful baro reads since boot

  // Calibration accumulators (INIT countdown)
  fc::Vec3 gyro_sum_;
  float up_sum_ = 0;
  uint32_t cal_n_ = 0;
  uint32_t last_countdown_s_ = 0;

  // --- power / pyro --------------------------------------------------------------
  float vbat_ = 0;
  bool have_battery_ = false;
  float vpyro_ = 0;
  bool continuity_ = false;
  bool watch_open_ = false;  // after a pulse: report when the bridge burns through
  uint32_t critical_since_ = 0;
  bool critical_timing_ = false;

  // --- status --------------------------------------------------------------------
  fc::FlightState prev_state_ = fc::FlightState::Init;
  fc::ResetCause reset_cause_ = fc::ResetCause::PowerOn;
  bool resumed_ = false;
  uint8_t warnings_ = 0;
  fc::FaultCode fault_ = fc::FaultCode::None;

  // --- timing --------------------------------------------------------------------
  uint32_t t_tick_ = 0, t_slow_ = 0, t_tlm_ = 0, t_log_ = 0, t_report_ = 0;
  uint32_t t_csv_ = 0;
  uint32_t loop_max_us_ = 0;   // longest control tick (diagnostics)

  // --- telemetry ------------------------------------------------------------------
  static constexpr size_t kEventQueue = 16;
  fc::tlm::Event evq_[kEventQueue];
  size_t evq_head_ = 0, evq_count_ = 0;
  uint32_t ev_dropped_ = 0;
  uint16_t radio_seq_ = 0, log_seq_ = 0;
  uint32_t airtime_tlm_us_ = 0, airtime_evt_us_ = 0;
  uint32_t tx_denied_ = 0;

  // --- console ------------------------------------------------------------------
  char cmd_[64];
  size_t cmd_len_ = 0;
  bool csv_stream_ = false;

#ifdef FC_BENCH_BUILD
  // Simulated flight (bench build only): replaces the sensor readings.
  fc::SimFlight* sim_ = nullptr;
  bool sim_live_ = false;  // also pulse the real pyro output
  uint32_t sim_last_ms_ = 0;
  bool sim_reported_ = false;
#endif
  bool simulating() const;
};
