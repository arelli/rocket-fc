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

#include "fc/ahrs.h"
#include "fc/battery.h"
#include "fc/flight_computer.h"
#include "fc/gps.h"
#include "fc/indicators.h"
#include "fc/mount.h"
#include "fc/param_shell.h"
#include "fc/radio_budget.h"
#include "fc/remote_fire.h"
#include "fc/settings.h"
#include "fc/sim.h"
#include "fc/telemetry.h"
#include "fc/time_sync.h"
#include "fc/uplink.h"
#include "hal/bmp280.h"
#include "hal/gps_uart.h"
#include "hal/io.h"
#include "hal/log_sink.h"
#include "hal/mpu9250.h"
#include "hal/pyro_channel.h"
#include "hal/radio_link.h"
#include "hal/settings_store.h"
#include "console_out.h"
#include "flash_log_sink.h"
#include "ram_log.h"

class App {
 public:
  App();
  void setup();
  void loop();

 private:
  // --- setup / init ----------------------------------------------------------
  void load_settings();
  void apply_settings();
  void init_sensors();
  void retry_imu(uint32_t now);
  void init_radio();
  void init_log();
  void start_countdown(uint32_t now);
  void try_resume(uint32_t now);
  void run_selftest(uint32_t now);

  // --- periodic work -----------------------------------------------------------
  void control_tick(uint32_t now);
  void slow_tick(uint32_t now);
  void read_sensors(uint32_t now);
  void update_attitude(uint32_t now);
  void handle_output(const fc::SmOutput& out, uint32_t now);
  void on_state_change(fc::FlightState from, fc::FlightState to, uint32_t now);
  void update_battery(uint32_t now);
  void update_continuity(uint32_t now);
  void update_indicators(uint32_t now);
  void update_radio(uint32_t now);
  void receive_uplink(uint32_t now);
  void handle_remote_fire(const fc::tlm::Uplink& u, uint32_t now);
  fc::RemoteFireEnd remote_conditions() const;
  void check_remote_fire(uint32_t now);
  // Ground station connectivity check and manual ejection (setting manual_eject).
  void handle_ping(const fc::tlm::Uplink& u, uint32_t now);
  void handle_text(const fc::tlm::Text& t, uint32_t now);
  // Rocket WiFi (log download through the ground station, app_wifi.cpp).
  void wifi_command(const char* a, const char* b, const char* c, const char* d, uint32_t now);
  void wifi_poll(uint32_t now);
  void wifi_stop(const char* why);
  bool wifi_allowed() const;
  void handle_eject(const fc::tlm::Uplink& u, uint32_t now);
  fc::RemoteFireEnd eject_conditions() const;
  void check_eject(uint32_t now);
  // Listens to the ground station in flight (manual ejection enabled, or a
  // simulated flight in bench mode).
  bool listen_in_flight() const { return set_.manual_eject || bench_; }
  bool warning_armed() const { return remote_.armed() || eject_.armed(); }
  void set_bench(bool on, int32_t why, uint32_t now, const char* source);
  void start_sag();
  void update_sag(uint32_t now);
  void init_gps();
  void update_flash_log(uint32_t now);
  void flash_log_drain();
  void flog_command(const char* sub, const char* arg, uint32_t now);
  void poll_gps(uint32_t now);
  void update_gps(uint32_t now);
  fc::tlm::Gps gps_snapshot(uint32_t now) const;
  uint32_t gps_period_ms() const;
  void update_log(uint32_t now);
  void pad_report(uint32_t now);
  [[noreturn]] void shutdown(const char* why);

  // --- telemetry -------------------------------------------------------------
  void event(fc::EventCode code, int32_t arg, uint32_t now);
  void event_at(fc::EventCode code, int32_t arg, uint32_t t_ms);
  void sync_time(uint64_t utc_ms, uint32_t now, const char* source);
  void format_utc(uint32_t t_ms, char* buf, size_t cap) const;
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
  void dump_log(Print& o);
  void upright_finish(uint32_t now);
  void flog_list(Print& o);
  void flog_dump(Print& o, uint32_t session);  // 0 = all
  bool safe_to_interrupt() const;
 public:
  void pyro_tick() { pyro_.update(millis()); }  // keep pulse timing during long console work
 private:
  void start_sim(bool live, uint32_t now);

  // --- hardware ----------------------------------------------------------------
  hal::PyroChannel pyro_;
  hal::PwmOut led_;
  hal::DigitalOut buzzer_;
  hal::AnalogIn batt_adc_;
  hal::AnalogIn pyro_adc_;
  hal::Mpu9250 imu_;
  hal::Bmp280 baro_;
  hal::RadioLink radio_;
  hal::GpsUart gps_uart_;

  // --- settings (fc/settings.h) ----------------------------------------------------
  // set_ is loaded at boot and never changes until the next boot. The console
  // "param" command edits and saves a separate copy (params_).
  fc::Settings set_;
  fc::ParamShell params_{false};
  hal::SettingsStore store_;
  uint32_t set_crc_ = 0;
  bool config_ok_ = true;           // stored settings loaded and valid
  char config_problem_[160] = {0};  // why not (self-test FAULT 3)
  fc::Axis up_ = fc::Axis::PosX;
  // "imu upright": accelerometer samples being collected (1 s) to find the nose axis.
  bool upright_active_ = false;
  bool upright_radio_ = false;  // asked by radio: send the result there too
  fc::UprightSampler upright_;
  uint32_t rx_window_ms_ = 150;     // listen after each TX on the ground
  uint32_t rx_window_flight_ms_ = 100;  // ...and in flight with manual ejection
  // Console by radio: output frame being sent, last command (repeats ignored).
  char text_buf_[fc::tlm::kMaxText];
  size_t text_len_ = 0;
  uint8_t buf2_[fc::tlm::kMaxTextFrameLen];
  uint32_t airtime_text_us_ = 0;
  uint16_t last_text_seq_ = 0;
  bool have_text_seq_ = false;
  // WiFi client (rocket joins the ground station's access point to send logs).
  bool wifi_on_ = false;
  bool wifi_joined_ = false;
  uint32_t wifi_since_ = 0;
  void* web_ = nullptr;  // WebServer (app_wifi.cpp)

  // Bench mode: test commands (sim, test fire, remote fire). RAM only: off at
  // every boot, switched on/off over USB or by radio, off at launch detection.
  bool bench_ = false;

  // --- logic -------------------------------------------------------------------
  fc::FlightComputer fc_;
  fc::BatteryMonitor batt_;
  fc::DutyCycleLimiter duty_;
  fc::BeepSequencer beeper_;
  fc::Ahrs ahrs_;               // attitude for telemetry/logging only
  fc::GroundLink ground_;       // ground station heartbeat (pad beeps only)
  fc::TimeSync clock_;          // real time, from the ground station or USB
  fc::RemoteFire remote_;       // remote test fire by radio (bench mode only)
  fc::RemoteFire eject_;        // manual ejection by radio (manual_eject, or bench mode)
  uint32_t sound_until_ = 0;    // ping answer beep: sounds even with buzzer_on = 0
  // Optional GPS (gps_mode): position for recovery and the log, never for decisions.
  fc::NmeaParser gps_;
  uint8_t gps_state_ = 0;       // 0 no data, 1 data without fix, 2 fix (GPS event on change)
  uint32_t gps_logged_ = 0;     // GGA count last written to the log
  uint32_t t_gps_tx_ = 0;
  uint32_t airtime_gps_us_ = 0;
  bool gps_raw_ = false;        // echo NMEA to the console ("gps raw on")
  bool gps_none_reported_ = false;
  uint32_t airtime_up_ms_ = 0;
  bool ground_was_ = false;
  uint32_t t_ahrs_ = 0;

  // Log sinks: every telemetry/event frame goes to each of them.
  // RAM log (this boot) and flash log (kept after power-off).
  RamLog ram_log_;
  FlashLogSink flog_;           // persistent copy in flash (flog_on)
  bool flog_ok_ = false;
  uint32_t t_flog_prep_ = 0;
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
  uint32_t tick_n_ = 0;        // control ticks since boot
  uint32_t t_imu_retry_ = 0;

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
  // Battery voltage during a pyro pulse (sag), sampled as fast as the loop runs.
  bool sag_active_ = false;
  float sag_pre_v_ = 0, sag_min_v_ = 0, sag_sum_ = 0;
  uint32_t sag_n_ = 0, sag_last_us_ = 0;

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
  char cmd_[112];
  size_t cmd_len_ = 0;
  bool csv_stream_ = false;

  // Simulated flight (bench mode): replaces the sensor readings until reboot.
  fc::SimFlight* sim_ = nullptr;
  bool sim_live_ = false;  // also pulse the real pyro output
  uint32_t sim_last_ms_ = 0;
  bool sim_reported_ = false;
  bool simulating() const;
};
