#include "app.h"

#include <esp_system.h>
#include <time.h>

#include "board_pins.h"
#include "fc/crc16.h"
#include "fc/csv.h"
#include "flight_config.h"
#include "hal/system.h"
#include "radio_config.h"

using fc::EventCode;
using fc::FlightState;

namespace {

bool in_flight(FlightState s) {
  return s == FlightState::Boost || s == FlightState::Coast || s == FlightState::Descent;
}

fc::Vec3 along(fc::Axis a, float v) {
  fc::Vec3 r;
  switch (a) {
    case fc::Axis::PosX: r.x = v; break;
    case fc::Axis::NegX: r.x = -v; break;
    case fc::Axis::PosY: r.y = v; break;
    case fc::Axis::NegY: r.y = -v; break;
    case fc::Axis::PosZ: r.z = v; break;
    case fc::Axis::NegZ: r.z = -v; break;
  }
  return r;
}

}  // namespace

// The members are rebuilt from the stored settings in apply_settings().
App::App()
    : fc_(fc::flight_computer_config(fc::Settings{})),
      batt_(fc::battery_params(fc::Settings{})),
      duty_(0.08f, 72000),
      ground_(5000),
      remote_(0),
      eject_(0) {}

ConsoleOut con;

bool App::simulating() const { return sim_ != nullptr; }

// =============================================================================
// Setup
// =============================================================================

void App::setup() {
  // Pyro gate low before anything else (the 100k pull-down holds it until now).
  pyro_.begin(pins::kPyroGate);
  buzzer_.begin(pins::kBuzzer, true, true);  // buzzer straight on the pin

  Serial.begin(115200);
  Serial.setTxTimeoutMs(5);  // never let a stalled USB host block the control loop

  reset_cause_ = hal::reset_cause();
  con.printf("\nrocket-fc %s  reset: %s\n", FC_GIT_REV, hal::reset_reason_name());
  load_settings();
  apply_settings();
  led_.begin(pins::kStatusLed, 0, static_cast<uint8_t>(set_.led_bright_pct));
  if (set_.manual_eject)
    con.println("WARNING: manual_eject = 1: the ground station can fire the ejection charge IN FLIGHT "
                   "(two-step coded command). Set manual_eject = 0 if you do not intend to use it.");
  if (fc::rf_key_is_default(set_))
    con.println("WARNING: rf_key is the public default: set your own pairing key on both kits (docs/BUILD.md).");

  batt_adc_.begin(pins::kBattSense, set_.batt_divider);
  pyro_adc_.begin(pins::kPyroSense, set_.pyro_divider);

  Wire.begin(pins::kI2cSda, pins::kI2cScl, 400000);
  Wire.setTimeOut(5);  // ms: a hung sensor must not stall the loop
  init_sensors();
  init_radio();
  init_gps();
  init_log();

  const uint32_t now = millis();
  duty_.reset(now);
  const fc::LoraModulation m = fc::lora_modulation(set_);
  airtime_tlm_us_ = fc::lora_airtime_us(m, fc::tlm::kTelemetryLen);
  airtime_evt_us_ = fc::lora_airtime_us(m, fc::tlm::kEventLen);
  airtime_up_ms_ = fc::lora_airtime_us(m, fc::tlm::kUplinkLen) / 1000;
  airtime_gps_us_ = fc::lora_airtime_us(m, fc::tlm::kGpsLen);
  // The ground station replies 15 ms after our frame; its reply must fit in the window.
  // On the ground the longest reply is a console command (TextCmd); in flight
  // (manual ejection) a fixed-length uplink.
  airtime_text_us_ = fc::lora_airtime_us(m, fc::tlm::kMaxTextFrameLen);
  const uint32_t cmd_ms = fc::lora_airtime_us(m, fc::tlm::text_frame_len(true, fc::tlm::kMaxCmdText)) / 1000;
  const uint32_t need = 15 + cmd_ms + 60;
  rx_window_ms_ = set_.uplink_rx_ms > need ? set_.uplink_rx_ms : need;
  rx_window_flight_ms_ = 15 + airtime_up_ms_ + 60;

  event(EventCode::Boot, static_cast<int32_t>(FC_GIT_REV_U32), now);
  event(EventCode::Config, static_cast<int32_t>(set_crc_), now);
  try_resume(now);
  if (!resumed_) start_countdown(now);
  // Bench mode at boot (setting bench_at_boot), never when resuming a flight.
  if (set_.bench_at_boot && !resumed_) set_bench(true, 1, now, "bench_at_boot");
  else con.println("mode: FLIGHT (bench mode off)");

  update_battery(now);
  hal::watchdog_begin(cfg::kWatchdogS);
  t_tick_ = t_slow_ = t_report_ = now;
  con.println("type 'help' for commands");
}

// Stored settings over the defaults. If they cannot be used, the computer runs
// on the defaults and refuses to fly (self-test FAULT 3) until they are fixed.
void App::load_settings() {
  static char text[hal::SettingsStore::kMaxText];
  bool read_ok = true;
  const size_t n = store_.load(text, sizeof(text), &read_ok);
  fc::Settings stored;
  fc::LoadReport rep;
  if (n) fc::settings_from_text(stored, text, n, &rep);
  config_ok_ = true;
  if (!read_ok) {
    config_ok_ = false;
    snprintf(config_problem_, sizeof(config_problem_), "settings could not be read from flash");
  } else if (rep.errors) {
    config_ok_ = false;
    snprintf(config_problem_, sizeof(config_problem_), "stored setting rejected: %s", rep.first_problem);
  } else if (!fc::validate(stored, config_problem_, sizeof(config_problem_))) {
    config_ok_ = false;
  }
  if (rep.unknown) con.printf("settings: %u stored value(s) unknown to this firmware, ignored (%s)\n",
                                 rep.unknown, rep.first_problem);
  set_ = config_ok_ ? stored : fc::Settings{};
  params_.init(set_, stored);
  set_crc_ = fc::settings_crc(set_);
  if (!config_ok_) {
    con.printf("SETTINGS INVALID: %s\n  running on the defaults; the computer will refuse to fly.\n"
                  "  Fix with 'param set ...' + 'param save' (or 'param reset all' + 'param save'), reboot.\n",
                  config_problem_);
  }
  // List what differs from the defaults: the boot log then shows the configuration.
  const fc::Settings defaults;
  size_t changed = 0;
  for (size_t i = 0; i < fc::param_count(); ++i) {
    const fc::ParamDef& d = fc::param_def(i);
    if (fc::param_equal(set_, defaults, d)) continue;
    char v[32];
    fc::format_param(set_, d, v, sizeof(v));
    con.printf("  setting %s = %s\n", d.key, v);
    ++changed;
  }
  con.printf("settings: %u changed from the defaults, crc %08lX\n", static_cast<unsigned>(changed),
                static_cast<unsigned long>(set_crc_));
}

void App::apply_settings() {
  up_ = fc::up_axis(set_);
  fc_ = fc::FlightComputer(fc::flight_computer_config(set_));
  batt_ = fc::BatteryMonitor(fc::battery_params(set_));
  duty_ = fc::DutyCycleLimiter(set_.radio_duty_pct / 100.0f, set_.radio_burst_s * 1000u);
  ground_ = fc::GroundLink(set_.uplink_timeout_ms);
  remote_ = fc::RemoteFire(set_.rf_key);
  eject_ = fc::RemoteFire(set_.rf_key);
  ahrs_ = fc::Ahrs(fc::ahrs_params(set_));
}

void App::init_sensors() {
  // List everything on the bus first: tells wiring faults from driver faults.
  con.print("I2C bus:");
  int found = 0;
  for (uint8_t a = 1; a < 127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      con.printf(" 0x%02X", a);
      ++found;
    }
  }
  con.println(found ? "  (MPU-9250 = 0x68/0x69, BMP280 = 0x76/0x77)" : " nothing (check SDA/SCL/3V3/GND)");

  if (imu_.begin(&Wire, 0x68, set_.imu_dlpf) || imu_.begin(&Wire, 0x69, set_.imu_dlpf)) {
    con.printf("IMU: %s (WHO_AM_I 0x%02X)%s\n", imu_.chip_name(), imu_.who_am_i(),
                  imu_.has_mag() ? " + AK8963 magnetometer" : ", no magnetometer");
  } else if (imu_.who_am_i() != 0) {
    con.printf("IMU: answered (WHO_AM_I 0x%02X) but could not be configured\n", imu_.who_am_i());
  } else {
    con.println("IMU: not found at 0x68 or 0x69");
  }
  if (baro_.begin(&Wire, set_.baro_iir, set_.baro_osr)) {
    con.printf("Baro: %s at 0x%02X\n", baro_.chip_id() == 0x60 ? "BME280" : "BMP280",
                  baro_.address());
  } else {
    con.println("Baro: not found");
  }
}

// An IMU that was missing at power-up (loose wire, connected later) is looked
// for again every 2 s while the rocket is on the ground. Never in flight.
void App::retry_imu(uint32_t now) {
  if (imu_.chip() != hal::Mpu9250::Chip::None) return;
  const FlightState s = fc_.state();
  if (s != FlightState::Pad && s != FlightState::Fault && s != FlightState::Landed) return;
  if (simulating() || now - t_imu_retry_ < 2000) return;
  t_imu_retry_ = now;
  if (imu_.begin(&Wire, 0x68, set_.imu_dlpf) || imu_.begin(&Wire, 0x69, set_.imu_dlpf)) {
    con.printf("IMU: found after boot: %s (WHO_AM_I 0x%02X)%s\n", imu_.chip_name(),
                  imu_.who_am_i(), imu_.has_mag() ? " + AK8963 magnetometer" : ", no magnetometer");
    con.println("     gyro bias was not calibrated: restart with the rocket still for best attitude");
  }
}

void App::init_radio() {
  const hal::RadioLink::Pins p{pins::kLoraSck,  pins::kLoraMiso,  pins::kLoraMosi,
                               pins::kLoraNss,  pins::kLoraDio1,  pins::kLoraReset,
                               pins::kLoraBusy, pins::kLoraRfSw};
  const hal::RadioLink::Params r{set_.radio_freq_mhz,
                                 fc::radio_bw_khz(set_),
                                 static_cast<uint8_t>(set_.radio_sf),
                                 static_cast<uint8_t>(set_.radio_cr),
                                 static_cast<uint8_t>(set_.radio_sync),
                                 static_cast<int8_t>(set_.radio_power_dbm),
                                 static_cast<uint16_t>(set_.radio_preamble),
                                 radio_cfg::kTcxoVolts,
                                 set_.radio_rx_boost};
  if (radio_.begin(p, r)) {
    radio_.set_listen(true);  // hear ground station heartbeats between transmissions
    radio_.start_receive();
    con.printf("Radio: SX1262 %.3f MHz SF%lu BW%.1f, %ld dBm%s%s\n",
                  static_cast<double>(set_.radio_freq_mhz), static_cast<unsigned long>(set_.radio_sf),
                  static_cast<double>(fc::radio_bw_khz(set_)), static_cast<long>(set_.radio_power_dbm),
                  set_.radio_rx_boost ? ", RX boost" : "",
                  fc::radio_within_eu_rules(set_) ? "" : "  [outside EU band rules: radio_unlock]");
  } else {
    con.printf("Radio: init failed (RadioLib error %d)\n", radio_.last_error());
  }
}

void App::init_gps() {
  if (set_.gps_mode == 0) {
    con.println("GPS: off (gps_mode)");
    return;
  }
  gps_uart_.begin(pins::kGpsRx, fc::gps_baud(set_));
  con.printf("GPS: listening on D7 at %lu baud (gps_mode %s)\n", static_cast<unsigned long>(fc::gps_baud(set_)),
                set_.gps_mode == 2 ? "required" : "auto");
}

void App::init_log() {
  ram_log_.begin();
  add_sink(&ram_log_);
  if (set_.flog_on) {
    flog_ok_ = flog_.begin();
    if (flog_ok_) {
      add_sink(&flog_);
      const size_t n = flog_.log().stored_sessions();
      con.printf("Log: flash-log %u KB, session %lu, %u earlier session(s) stored ('flog')\n",
                    static_cast<unsigned>(flog_.log().sectors() * 4), static_cast<unsigned long>(flog_.log().session()),
                    static_cast<unsigned>(n));
    } else {
      con.println("Log: flash-log FAILED (no 'flog' partition: flash with this project's platformio.ini)");
    }
  }
  for (size_t i = 0; i < n_sinks_; ++i) {
    con.printf("Log: %s %s\n", sinks_[i]->name(), sinks_[i]->ok() ? "ok" : "FAILED");
  }
}

void App::add_sink(hal::LogSink* s) {
  if (n_sinks_ < kMaxSinks) sinks_[n_sinks_++] = s;
}

void App::try_resume(uint32_t now) {
  const fc::FlightRecord rec = hal::load_flight_record();
  if (!fc::should_resume(rec, reset_cause_)) {
    hal::clear_flight_record();
    return;
  }
  fc_.resume(rec, now);
  resumed_ = true;
  prev_state_ = fc_.state();
  con.printf("RESUMED flight in %s, %lu ms after launch, fired=%u\n", to_string(fc_.state()),
                static_cast<unsigned long>(rec.ms_since_launch), rec.fired);
  event(EventCode::Resumed, static_cast<int32_t>(rec.ms_since_launch), now);
}

void App::start_countdown(uint32_t now) {
  fc_.begin(now);
  prev_state_ = FlightState::Init;
  gyro_sum_ = fc::Vec3{};
  up_sum_ = 0;
  cal_n_ = 0;
  last_countdown_s_ = 0;
  con.printf("Keep the rocket still: calibrating for %lu s\n",
                static_cast<unsigned long>(set_.settle_ms / 1000));
}

// =============================================================================
// Main loop
// =============================================================================

void App::loop() {
  const uint32_t now = millis();

  pyro_.update(now);  // end the pulse on time, whatever else happens
  update_sag(now);
  poll_gps(now);

  if (now - t_tick_ >= cfg::kLoopMs) {
    // Fixed-rate schedule; if we fell far behind, resynchronize instead of bursting.
    t_tick_ = (now - t_tick_ > 10 * cfg::kLoopMs) ? now : t_tick_ + cfg::kLoopMs;
    const uint32_t t0 = micros();
    control_tick(now);
    const uint32_t dt = micros() - t0;
    if (dt > loop_max_us_) loop_max_us_ = dt;
  }

  if (now - t_slow_ >= cfg::kSlowMs) {
    t_slow_ = now;
    slow_tick(now);
  }

  update_radio(now);
  update_flash_log(now);
  update_indicators(now);
  console_poll(now);
  wifi_poll(now);
  hal::watchdog_feed();
}

void App::control_tick(uint32_t now) {
  read_sensors(now);

  const FlightState before = fc_.state();

  if (before == FlightState::Init) {
    // Countdown beeps (one per second) and IMU calibration in the final window.
    const uint32_t rem = fc_.settle_remaining_ms(now);
    const uint32_t s = (rem + 999) / 1000;
    if (s != last_countdown_s_) {
      last_countdown_s_ = s;
      if (s > 0 && set_.beep_countdown) beeper_.push(40, 0);
    }
    if (imu_ok_ && rem <= set_.calib_ms) {
      gyro_sum_.x += imu_s_.gyro_dps.x;
      gyro_sum_.y += imu_s_.gyro_dps.y;
      gyro_sum_.z += imu_s_.gyro_dps.z;
      up_sum_ += fc::axis_component(imu_s_.accel_g, up_);
      ++cal_n_;
    }
  }

  if (upright_active_ && imu_ok_ && !sim_) {
    upright_.add(imu_s_.accel_g);
    if (upright_.count() >= 100) upright_finish(now);  // 1 s at 100 Hz
  }

  update_attitude(now);
  const fc::SmOutput out = fc_.tick(now, baro_ok_, pressure_pa_, imu_ok_, imu_s_.accel_g);

  if (before == FlightState::Init && fc_.settle_done(now)) run_selftest(now);

  // Driver-level interlock: a fire command is only accepted once launch has
  // been detected. (The state machine may detect launch and command the
  // charge in the same tick, e.g. a baro launch held longer than the lockout,
  // so this deliberately uses launched() rather than the previous tick's arm
  // state.) Between commands the driver follows the state machine's arming.
  pyro_.set_armed(fc_.sm().launched());
  handle_output(out, now);
  pyro_.set_armed(fc_.sm().pyro_armed());

  if (fc_.state() != prev_state_) {
    on_state_change(prev_state_, fc_.state(), now);
    prev_state_ = fc_.state();
  }

  // Keep the RTC-RAM record current so a reset in flight can resume.
  if (!simulating()) hal::save_flight_record(fc_.record(now));

  update_log(now);
}

void App::update_attitude(uint32_t now) {
  const float dt = t_ahrs_ ? (now - t_ahrs_) / 1000.0f : 0.0f;
  t_ahrs_ = now;
  if (!imu_ok_) return;
  // Converge quickly while the rocket stands still during the countdown.
  ahrs_.set_fast(fc_.state() == FlightState::Init);
  ahrs_.update(dt, fc::to_rocket_frame(imu_s_.gyro_dps, up_), fc::to_rocket_frame(imu_s_.accel_g, up_),
               fc::to_rocket_frame(imu_s_.mag_uT, up_), imu_.has_mag());
}

void App::read_sensors(uint32_t now) {
  if (sim_) {
    const fc::SimSample s = sim_->step(now - sim_last_ms_);
    sim_last_ms_ = now;
    imu_s_ = hal::Mpu9250::Sample{};
    // The simulator reports accel along +X = nose; map it to the configured axis.
    imu_s_.accel_g = along(up_, s.accel_g.x);
    pressure_pa_ = s.pressure_pa;
    temp_c_ = 20.0f;
    imu_ok_ = baro_ok_ = true;
    ++baro_good_;
    return;
  }

  // Report a sensor as lost after 0.5 s of consecutive failures, once per
  // outage. A lost sensor is retried only every 10th tick, so a disconnected
  // device (I2C timeouts) cannot slow the control loop down.
  constexpr uint16_t kLostTicks = 50;
  ++tick_n_;
  const bool retry_tick = (tick_n_ % 10) == 0;
  auto bump = [](uint16_t run, bool ok) -> uint16_t {
    return ok ? 0 : static_cast<uint16_t>(run < 0xFFFF ? run + 1 : run);
  };

  if (imu_.chip() != hal::Mpu9250::Chip::None) {
    if (imu_fail_run_ < kLostTicks || retry_tick) {
      imu_ok_ = imu_.read(&imu_s_);
      imu_fail_run_ = bump(imu_fail_run_, imu_ok_);
    }
  } else {
    imu_ok_ = false;
  }

  if (baro_.ok()) {
    if (baro_fail_run_ < kLostTicks || retry_tick) {
      float p, t;
      baro_ok_ = baro_.read(&p, &t);
      if (baro_ok_) {
        pressure_pa_ = p;
        temp_c_ = t;
        ++baro_good_;
      }
      baro_fail_run_ = bump(baro_fail_run_, baro_ok_);
    }
  } else {
    baro_ok_ = false;
  }

  auto check = [&](uint16_t run, uint8_t bit) {
    if (run >= kLostTicks && !(lost_reported_ & bit)) {
      lost_reported_ |= bit;
      event(EventCode::SensorLost, bit, now);
    } else if (run == 0) {
      lost_reported_ &= static_cast<uint8_t>(~bit);
    }
  };
  if (imu_.chip() != hal::Mpu9250::Chip::None) check(imu_fail_run_, fc::kSensorImu);
  if (baro_.ok()) check(baro_fail_run_, fc::kSensorBaro);
}

void App::run_selftest(uint32_t now) {
  fault_ = fc::FaultCode::None;
  warnings_ = 0;

  // Fatal checks (FAULT: never arms, never fires). The order is the priority.
  if (!config_ok_) {
    fault_ = fc::FaultCode::ConfigInvalid;
    con.printf("SELF-TEST: settings invalid: %s\n", config_problem_);
  } else if (baro_good_ < 10 || !baro_ok_) {
    fault_ = fc::FaultCode::BaroMissing;
  } else if (!(pressure_pa_ > set_.st_p_min_pa && pressure_pa_ < set_.st_p_max_pa)) {
    fault_ = fc::FaultCode::BaroImplausible;
  } else if (set_.st_imu_required && !imu_ok_) {
    fault_ = fc::FaultCode::ImuMissing;
  } else if (set_.st_radio_required && !radio_.ok()) {
    fault_ = fc::FaultCode::RadioFail;
  } else if (set_.st_min_batt_v > 0.0f && have_battery_ && vbat_ < set_.st_min_batt_v) {
    fault_ = fc::FaultCode::BattLow;
  } else if (set_.gps_mode == 2 && gps_state_ == 0) {
    fault_ = fc::FaultCode::GpsMissing;
  }

  float up = 0;
  if (!imu_ok_) {
    warnings_ |= fc::kWarnImuMissing;
  } else {
    if (set_.st_warn_mag && !imu_.has_mag() && !simulating()) warnings_ |= fc::kWarnMagMissing;
    if (cal_n_ > 0) {
      const float n = static_cast<float>(cal_n_);
      up = up_sum_ / n;
      if (set_.st_axis_check && (up < 1.0f - set_.st_axis_tol_g || up > 1.0f + set_.st_axis_tol_g))
        warnings_ |= fc::kWarnImuAxis;
      imu_.set_gyro_bias(fc::Vec3{gyro_sum_.x / n, gyro_sum_.y / n, gyro_sum_.z / n});
    }
  }
  if (!radio_.ok()) warnings_ |= fc::kWarnRadioFail;
  if (have_battery_ && vbat_ < set_.batt_low_v) warnings_ |= fc::kWarnBattLow;
  if (set_.st_warn_backup && set_.backup_deploy_ms == 0) warnings_ |= fc::kWarnNoBackupTimer;
  if (set_.gps_mode == 1 && gps_state_ == 0) warnings_ |= fc::kWarnGpsMissing;
  if (set_.manual_eject) warnings_ |= fc::kWarnManualEject;

  const bool cal_ok = fc_.finish_init(fault_ == fc::FaultCode::None);
  if (!cal_ok && fault_ == fc::FaultCode::None) fault_ = fc::FaultCode::BaroMissing;

  beeper_.clear();
  if (fault_ != fc::FaultCode::None) {
    event(EventCode::SelfTestFail, static_cast<int32_t>(fault_), now);
    beeper_.beeps(static_cast<uint8_t>(fault_), 600, 300, 1500);
    con.printf("SELF-TEST FAILED: fault %u -> FAULT state, will not fly\n",
                  static_cast<unsigned>(fault_));
  } else {
    event(EventCode::SelfTestOk, warnings_, now);
    if (set_.beep_selftest) beeper_.beeps(2, 150, 150, 1000);
    con.printf("SELF-TEST OK: ground %.1f Pa, warnings 0x%02X\n",
                  static_cast<double>(fc_.estimator().ground_pa()), warnings_);
  }
  if (warnings_ & fc::kWarnImuMissing) con.println("  warning: IMU not responding");
  if (warnings_ & fc::kWarnMagMissing) con.println("  warning: no magnetometer");
  if (warnings_ & fc::kWarnImuAxis)
    con.printf("  warning: up axis reads %.2f g (expected ~1.00): check imu_up_axis / orientation\n",
                  static_cast<double>(up));
  if (warnings_ & fc::kWarnRadioFail) con.println("  warning: radio not working");
  if (warnings_ & fc::kWarnBattLow) con.println("  warning: battery low");
  if (warnings_ & fc::kWarnNoBackupTimer)
    con.println("  note: backup deployment timer disabled (backup_deploy_ms = 0)");
  if (warnings_ & fc::kWarnGpsMissing)
    con.println("  note: no GPS data on D7 (no module? set gps_mode = off to silence this)");
  if (warnings_ & fc::kWarnManualEject)
    con.println("  WARNING: manual ejection by radio ENABLED (manual_eject = 1): the ground station "
                   "can fire the charge in flight");
  t_report_ = now;
}

void App::handle_output(const fc::SmOutput& out, uint32_t now) {
  for (size_t i = 0; i < out.n_events; ++i) event(out.events[i].code, out.events[i].arg, now);
  if (!out.fire) return;

  if (sim_) {
    sim_->deploy();
    if (!sim_live_) {
      con.println("[sim] FIRE command (pyro output not driven; use 'sim live' to drive it)");
      return;
    }
  }
  start_sag();
  if (pyro_.fire(now, set_.pyro_pulse_ms)) {
    watch_open_ = continuity_;  // only meaningful if the circuit was closed
    con.printf("[%lu] PYRO FIRED (%lu ms pulse)\n", static_cast<unsigned long>(now),
                  static_cast<unsigned long>(set_.pyro_pulse_ms));
  } else {
    sag_active_ = false;
    con.printf("[%lu] PYRO fire command REFUSED by driver (armed=%d fired=%d)\n",
                  static_cast<unsigned long>(now), pyro_.armed(), pyro_.fired());
  }
}

void App::on_state_change(FlightState from, FlightState to, uint32_t now) {
  con.printf("[%lu] state %s -> %s\n", static_cast<unsigned long>(now), to_string(from),
                to_string(to));
  if (to == FlightState::Boost) {
    beeper_.clear();  // no buzzer during flight
    if (wifi_on_) wifi_stop("launch");
    t_tlm_ = 0;       // send telemetry right away
    // A real launch ends bench mode (a simulated one does not).
    if (bench_ && !simulating()) set_bench(false, 2, now, "launch detected");
  }
  if (to == FlightState::Landed) {
    for (size_t i = 0; i < n_sinks_; ++i) sinks_[i]->flush();
    t_report_ = now - set_.report_s * 1000u;  // start the altitude beacon now
  }
}

// =============================================================================
// Slow tasks: power, continuity, beeps
// =============================================================================

void App::slow_tick(uint32_t now) {
  check_remote_fire(now);
  check_eject(now);
  update_gps(now);
  const bool heard = ground_.connected(now);
  if (heard != ground_was_) {
    ground_was_ = heard;
    event(EventCode::GroundLink, heard ? 1 : 0, now);
  }
  retry_imu(now);
  update_battery(now);
  update_continuity(now);
  pad_report(now);
  if (csv_stream_) print_csv_line(now);
  if (sim_ && !sim_reported_ && fc_.state() == FlightState::Landed) {
    sim_reported_ = true;
    con.printf("[sim] complete: true apogee %.1f m at t=%lu ms; computer max %.1f m\n",
                  static_cast<double>(sim_->apogee_alt_m()),
                  static_cast<unsigned long>(sim_->apogee_ms()),
                  static_cast<double>(fc_.sm().max_alt_m()));
  }
}

void App::update_battery(uint32_t now) {
  vbat_ = batt_adc_.read_volts();
  // Below batt_present_v there is no cell (running from USB with the switch off).
  have_battery_ = vbat_ > set_.batt_present_v;
  if (!have_battery_) {
    critical_timing_ = false;
    return;
  }
  if (batt_.update(vbat_, now)) {
    const int32_t mv = static_cast<int32_t>(vbat_ * 1000.0f);
    if (batt_.level() == fc::BatteryMonitor::Level::Low) event(EventCode::BattLow, mv, now);
    if (batt_.level() == fc::BatteryMonitor::Level::Critical)
      event(EventCode::BattCritical, mv, now);
  }

  // The XIAO has no over-discharge cutoff: protect the cell when it is safe to.
  // Never while a computer is connected over USB: it is on the bench, and
  // without a cell the charger output on BAT+ can read as a flat battery.
  const bool usb_host = static_cast<bool>(Serial);
  const bool safe = !in_flight(fc_.state()) && !pyro_.firing() && !simulating() && !usb_host;
  if (batt_.level() == fc::BatteryMonitor::Level::Critical && safe && set_.batt_sleep_s > 0) {
    if (!critical_timing_) {
      critical_timing_ = true;
      critical_since_ = now;
    } else if (now - critical_since_ >= set_.batt_sleep_s * 1000u) {
      shutdown("battery critical");
    }
  } else {
    critical_timing_ = false;
  }
}

void App::update_continuity(uint32_t now) {
  vpyro_ = pyro_adc_.read_volts();
  if (pyro_.firing()) return;  // the MOSFET pulls the drain low while firing
  continuity_ = have_battery_ && vpyro_ >= set_.continuity_frac * vbat_;

  // After a pulse, report if (and when) the bridge wire burned through. Only
  // within 10 s of the pulse, so removing the arm link later is not reported.
  if (watch_open_) {
    const uint32_t since = now - pyro_.fire_time();
    if (!continuity_) {
      watch_open_ = false;
      event(EventCode::PyroOpen, static_cast<int32_t>(since), now);
    } else if (since > hal::PyroChannel::kMaxPulseMs + 10000) {
      watch_open_ = false;
    }
  }
}

void App::pad_report(uint32_t now) {
  if (beeper_.busy()) return;
  uint32_t period_ms = set_.report_s * 1000u;
  if (fc_.state() == FlightState::Pad) {
    // pad_beeps: 0 = while the ground station is heard (its period), 1 = always, 2 = never.
    uint32_t s = 0;
    if (set_.pad_beeps == 0) s = ground_.pad_beep_period_s(now);
    else if (set_.pad_beeps == 1) s = set_.pad_beep_s;
    if (s == 0) return;
    period_ms = s * 1000u;
  }
  if (fc_.state() == FlightState::Landed && !set_.landed_beacon) return;
  if (now - t_report_ < period_ms) return;
  switch (fc_.state()) {
    case FlightState::Pad:
      // 3 short = igniter continuity, 1 long = open circuit / arm link out.
      if (continuity_) {
        beeper_.beeps(3, static_cast<uint16_t>(set_.beep_short_ms), 120, 0);
      } else {
        beeper_.beeps(1, static_cast<uint16_t>(set_.beep_long_ms), 0, 0);
      }
      break;
    case FlightState::Fault:
      beeper_.beeps(static_cast<uint8_t>(fault_), 600, 300, 0);
      break;
    case FlightState::Landed:
      // Recovery beacon: maximum altitude in metres, digit by digit.
      beeper_.beeps(1, 1000, 800, 0);
      beeper_.digits(static_cast<uint32_t>(fc_.sm().max_alt_m() + 0.5f));
      break;
    default:
      return;
  }
  t_report_ = now;
}

void App::update_indicators(uint32_t now) {
  fc::LedMode m = fc::LedMode::Off;
  switch (fc_.state()) {
    case FlightState::Init: m = fc::LedMode::Init; break;
    case FlightState::Fault: m = fc::LedMode::Fault; break;
    case FlightState::Pad: m = continuity_ ? fc::LedMode::PadReady : fc::LedMode::PadSafe; break;
    case FlightState::Boost:
    case FlightState::Coast:
    case FlightState::Descent: m = set_.led_flight ? fc::LedMode::Flight : fc::LedMode::Off; break;
    case FlightState::Landed: m = fc::LedMode::Landed; break;
  }
  if (warning_armed()) m = fc::LedMode::Init;  // fast blink: remote fire / manual eject armed
  const bool low = batt_.level() == fc::BatteryMonitor::Level::Low ||
                   batt_.level() == fc::BatteryMonitor::Level::Critical;
  if (low && set_.led_low_batt && !warning_armed() &&
      (fc_.state() == FlightState::Pad || fc_.state() == FlightState::Landed)) {
    m = fc::LedMode::LowBattery;
  }
  led_.set(set_.led_on && fc::led_on(m, now));
  // Fire warnings and the ping answer sound even with the buzzer switched off.
  const bool sound = beeper_.update(now);
  buzzer_.set(sound && (set_.buzzer_on || warning_armed() ||
                        static_cast<int32_t>(sound_until_ - now) > 0));
}

void App::shutdown(const char* why) {
  con.printf("SHUTDOWN: %s (vbat %.2f V). Power cycle to restart.\n", why,
                static_cast<double>(vbat_));
  pyro_.force_off();
  buzzer_.set(false);
  led_.set(false);
  radio_.sleep();
  imu_.sleep();
  baro_.sleep();
  hal::clear_flight_record();
  Serial.flush();
  delay(50);
  hal::deep_sleep_forever();
}

// =============================================================================
// Telemetry and logging
// =============================================================================

uint8_t App::flags() const {
  uint8_t f = 0;
  if (continuity_) f |= fc::tlm::kFlagPyroContinuity;
  if (pyro_.armed()) f |= fc::tlm::kFlagPyroArmed;
  if (fc_.sm().fired() || pyro_.fired()) f |= fc::tlm::kFlagPyroFired;
  if (imu_ok_) f |= fc::tlm::kFlagImuOk;
  if (imu_ok_ && imu_.has_mag()) f |= fc::tlm::kFlagMagOk;
  if (baro_ok_) f |= fc::tlm::kFlagBaroOk;
  if (batt_.level() == fc::BatteryMonitor::Level::Low ||
      batt_.level() == fc::BatteryMonitor::Level::Critical)
    f |= fc::tlm::kFlagBattLow;
  if (resumed_) f |= fc::tlm::kFlagResumed;
  return f;
}

fc::tlm::Telemetry App::snapshot(uint32_t now) const {
  fc::tlm::Telemetry t;
  t.state = fc_.state();
  t.t_ms = now;
  t.alt_m = fc_.estimator().alt_m();
  t.vel_mps = fc_.estimator().vel_mps();
  // Rocket frame (+Z toward the nose), see fc/ahrs.h.
  t.accel_g = fc::to_rocket_frame(imu_s_.accel_g, up_);
  t.gyro_dps = fc::to_rocket_frame(imu_s_.gyro_dps, up_);
  t.mag_uT = fc::to_rocket_frame(imu_s_.mag_uT, up_);
  t.q = imu_ok_ ? ahrs_.q() : fc::Quat{};
  t.link = static_cast<uint8_t>((ground_.connected(now) ? fc::tlm::kLinkGroundHeard : 0) |
                                (clock_.synced() ? fc::tlm::kLinkTimeSynced : 0) |
                                (static_cast<bool>(Serial) ? fc::tlm::kLinkUsbHost : 0) |
                                (bench_ ? fc::tlm::kLinkBenchMode : 0) |
                                (gps_state_ >= 1 ? fc::tlm::kLinkGpsData : 0) |
                                (gps_state_ == 2 ? fc::tlm::kLinkGpsFix : 0) |
                                (set_.manual_eject ? fc::tlm::kLinkManualEject : 0));
  t.uplink_rssi_dbm = ground_.rssi_dbm(now);
  t.beep_period_s = static_cast<uint8_t>(set_.pad_beeps == 0   ? ground_.pad_beep_period_s(now)
                                         : set_.pad_beeps == 1 ? set_.pad_beep_s
                                                               : 0);
  t.pressure_pa = pressure_pa_;
  t.temp_c = temp_c_;
  t.batt_v = vbat_;
  t.flags = flags();
  return t;
}

void App::event(EventCode code, int32_t arg, uint32_t now) { event_at(code, arg, now); }

void App::event_at(EventCode code, int32_t arg, uint32_t now) {
  fc::tlm::Event e;
  e.state = fc_.state();
  e.t_ms = now;
  e.code = code;
  e.arg = arg;

  // Log immediately.
  e.seq = log_seq_++;
  uint8_t buf[fc::tlm::kMaxFrameLen];
  const size_t n = fc::tlm::encode(e, buf, sizeof(buf));
  write_log(buf, n);

  // Queue for the radio (events go before telemetry). If full, drop the oldest.
  if (evq_count_ == kEventQueue) {
    evq_head_ = (evq_head_ + 1) % kEventQueue;
    --evq_count_;
    ++ev_dropped_;
  }
  evq_[(evq_head_ + evq_count_) % kEventQueue] = e;
  ++evq_count_;

  con.printf("[%lu] EVENT %s arg=%ld\n", static_cast<unsigned long>(now), to_string(code),
                static_cast<long>(arg));
}

void App::write_log(const uint8_t* frame, size_t len) {
  if (len == 0) return;
  for (size_t i = 0; i < n_sinks_; ++i) sinks_[i]->write_frame(frame, len);
}

void App::update_log(uint32_t now) {
  const uint32_t period = in_flight(fc_.state()) ? set_.log_flight_ms : set_.log_pad_ms;
  if (now - t_log_ < period) return;
  t_log_ = now;
  fc::tlm::Telemetry t = snapshot(now);
  t.seq = log_seq_++;
  uint8_t buf[fc::tlm::kMaxFrameLen];
  write_log(buf, fc::tlm::encode(t, buf, sizeof(buf)));
}

uint32_t App::telemetry_period_ms() const {
  // Faster round trips during a remote test fire / manual ejection on the ground.
  if (warning_armed() && !in_flight(fc_.state())) return 500;
  switch (fc_.state()) {
    case FlightState::Boost:
    case FlightState::Coast:
    case FlightState::Descent: return set_.tlm_flight_ms;
    case FlightState::Landed: return set_.tlm_landed_ms;
    default: return set_.tlm_pad_ms;
  }
}

void App::receive_uplink(uint32_t now) {
  uint8_t buf[fc::tlm::kMaxTextFrameLen + 8];
  float rssi = 0, snr = 0;
  bool crc_error = false;
  const size_t n = radio_.receive(buf, sizeof(buf), &rssi, &snr, &crc_error);
  if (n == 0 || crc_error) return;
  fc::tlm::FrameType type;
  if (fc::tlm::frame_type(buf, n, &type) != fc::tlm::DecodeResult::Ok) return;
  if (type == fc::tlm::FrameType::TextCmd) {
    if (in_flight(fc_.state())) return;  // the console never runs commands by radio in flight
    static fc::tlm::Text t;
    if (fc::tlm::decode(buf, n, &t) == fc::tlm::DecodeResult::Ok) handle_text(t, now);
    return;
  }
  fc::tlm::Uplink u;
  if (fc::tlm::decode(buf, n, &u) != fc::tlm::DecodeResult::Ok) return;
  using K = fc::tlm::UplinkKind;
  const bool ping_or_eject = u.kind == K::Ping || u.kind == K::EjectArm || u.kind == K::Eject;
  // In flight only a ping or the manual ejection, and only if enabled.
  if (in_flight(fc_.state()) && !(ping_or_eject && listen_in_flight())) return;
  if (u.kind == K::Ping) {
    handle_ping(u, now);
    return;
  }
  if (u.kind == K::EjectArm || u.kind == K::Eject) {
    handle_eject(u, now);
    return;
  }
  if (u.kind != K::Heartbeat) {
    handle_remote_fire(u, now);
    return;
  }
  const float r = rssi < -127.0f ? -127.0f : rssi > 0.0f ? 0.0f : rssi;
  ground_.heard(now, static_cast<int8_t>(r), u.beep_period_s);
  // The time was stamped when the heartbeat started; it arrived one airtime later.
  if (u.utc_ms) sync_time(u.utc_ms + airtime_up_ms_, now, "ground station");
}

// --- Remote test fire (ejection charge ground test), see fc/remote_fire.h ---

fc::RemoteFireEnd App::remote_conditions() const {
  if (!bench_) return fc::RemoteFireEnd::NotBenchMode;  // flight mode never fires by radio
  if (fc_.state() != FlightState::Pad || simulating()) return fc::RemoteFireEnd::NotOnPad;
  if (pyro_.firing()) return fc::RemoteFireEnd::Busy;
  if (!continuity_) return fc::RemoteFireEnd::NoContinuity;
  return fc::RemoteFireEnd::Fired;  // = all conditions met
}

void App::handle_remote_fire(const fc::tlm::Uplink& u, uint32_t now) {
  using fc::RemoteFireEnd;
  switch (u.kind) {
    case fc::tlm::UplinkKind::FireArm: {
      const RemoteFireEnd r = remote_.arm(now, u.pulse_ms, esp_random(), remote_conditions());
      if (r != RemoteFireEnd::Fired) {
        event(EventCode::RemoteRefused, static_cast<int32_t>(r), now);
        con.printf("REMOTE FIRE: arm refused: %s\n", fc::to_string(r));
        return;
      }
      event(EventCode::RemoteArmed, static_cast<int32_t>(remote_.nonce()), now);
      con.printf("REMOTE FIRE: ARMED for %u ms pulse, fire window %lu s\n", remote_.pulse_ms(),
                    static_cast<unsigned long>(fc::RemoteFire::kWindowMs / 1000));
      // Warning to anyone near the rocket: beep for the whole window.
      beeper_.clear();
      for (uint32_t i = 0; i < fc::RemoteFire::kWindowMs / 500; ++i) beeper_.push(250, 250);
      return;
    }
    case fc::tlm::UplinkKind::Fire: {
      const RemoteFireEnd r = remote_.fire(now, u.code, u.pulse_ms, remote_conditions());
      beeper_.clear();
      if (r != RemoteFireEnd::Fired) {
        event(EventCode::RemoteRefused, static_cast<int32_t>(r), now);
        con.printf("REMOTE FIRE: fire refused: %s\n", fc::to_string(r));
        return;
      }
      watch_open_ = continuity_;
      start_sag();
      if (pyro_.bench_fire(now, u.pulse_ms)) {
        event(EventCode::BenchFire, u.pulse_ms, now);
        con.printf("REMOTE FIRE: FIRED %u ms\n", u.pulse_ms);
      } else {
        sag_active_ = false;
      }
      return;
    }
    case fc::tlm::UplinkKind::FireDisarm:
      if (remote_.armed()) {
        remote_.disarm();
        beeper_.clear();
        event(EventCode::RemoteDisarmed, static_cast<int32_t>(RemoteFireEnd::Cancelled), now);
        con.println("REMOTE FIRE: disarmed from the ground station");
      }
      return;
    case fc::tlm::UplinkKind::BenchMode: {
      const bool on = u.pulse_ms != 0;
      if (u.code != fc::bench_mode_code(on, set_.rf_key)) {
        event(EventCode::RemoteRefused, static_cast<int32_t>(RemoteFireEnd::BadCode), now);
        con.println("BENCH MODE by radio: refused, wrong code (pairing key rf_key differs?)");
        return;
      }
      if (!on && simulating()) {
        con.println("BENCH MODE by radio: off refused while a simulation runs (reboot ends it)");
        event(EventCode::BenchMode, 1, now);
        return;
      }
      set_bench(on, on ? 1 : 0, now, "radio");
      return;
    }
    default:
      return;
  }
}

// --- Persistent flash log -----------------------------------------------------------
// Writing flash stalls the processor (page writes ~0.1-1 ms, a sector erase
// tens of ms). It is done only when that cannot matter: never between launch
// and deployment (BOOST, COAST, delayed charge pending), never during a pyro
// pulse or its battery measurement. The queue (2.7 min at 50 Hz) holds the
// flight meanwhile; it is written under the parachute. On the pad, sectors are
// erased ahead so that writing in DESCENT rarely needs an erase.
void App::update_flash_log(uint32_t now) {
  if (!flog_ok_) return;
  const FlightState s = fc_.state();
  if (s == FlightState::Boost || s == FlightState::Coast || fc_.sm().deploy_pending() || pyro_.firing() ||
      sag_active_)
    return;
  if (s == FlightState::Pad && now - t_flog_prep_ >= 1000) {
    t_flog_prep_ = now;
    if (flog_.prepare(64)) return;  // one erase per second at most; nothing more this round
  }
  flog_.service(2000, true);
}

void App::flash_log_drain() {
  while (flog_ok_ && flog_.backlog() > 0) {
    flog_.service(20000, true);
    hal::watchdog_feed();
    pyro_.update(millis());
  }
}

// --- Optional GPS ------------------------------------------------------------------
// Bytes are parsed as they arrive (a few per loop); status, time and logging
// run at 10 Hz. Nothing here affects the flight decisions.
void App::poll_gps(uint32_t now) {
  char c;
  for (int i = 0; i < 64 && gps_uart_.read(&c); ++i) {
    gps_.feed(c, now);
    if (c == '\n' && gps_raw_) con.printf("[gps] %s\n", gps_.last_line());
  }
}

void App::update_gps(uint32_t now) {
  if (!gps_uart_.started()) return;
  const uint32_t last = gps_.last_sentence_ms();
  const bool data = last != 0 && now - last < 3000;
  const bool fix = data && gps_.fix().valid() && now - gps_.last_gga_ms() < 3000;
  const uint8_t st = fix ? 2 : data ? 1 : 0;
  if (st != gps_state_) {
    static const char* const kNames[] = {"no data (lost)", "receiving, no fix yet", "position fix"};
    con.printf("[%lu] GPS: %s, %u satellites\n", static_cast<unsigned long>(now), kNames[st],
                  gps_.fix().sats);
    gps_state_ = st;
    event(EventCode::GpsStatus, st, now);
  }
  if (!data && !gps_none_reported_ && now > 8000) {
    gps_none_reported_ = true;
    con.printf("GPS: no data on D7 yet (%lu bytes rejected). Module wired? gps_baud right?\n",
                  static_cast<unsigned long>(gps_.checksum_errors()));
  }
  // Real time from the GPS if nothing better came first (TimeSync keeps the first).
  if (set_.gps_time && fix && gps_.time_valid() && !clock_.synced() && gps_.fix().utc_s) {
    const uint64_t utc = static_cast<uint64_t>(gps_.fix().utc_s) * 1000u + gps_.fix().utc_ms +
                         (now - gps_.last_gga_ms());
    sync_time(utc, now, "GPS");
  }
  // Log every new position report (about 1 Hz).
  if (gps_.gga_count() != gps_logged_ && data) {
    gps_logged_ = gps_.gga_count();
    fc::tlm::Gps g = gps_snapshot(now);
    g.seq = log_seq_++;
    uint8_t buf[fc::tlm::kMaxFrameLen];
    write_log(buf, fc::tlm::encode(g, buf, sizeof(buf)));
  }
}

fc::tlm::Gps App::gps_snapshot(uint32_t now) const {
  const fc::GpsFix& f = gps_.fix();
  fc::tlm::Gps g;
  g.state = fc_.state();
  g.t_ms = now;
  g.quality = gps_state_ == 2 ? f.quality : 0;
  g.sats = f.sats;
  g.hdop = f.hdop;
  g.lat_e7 = f.lat_e7;  // last known position, also after the fix is lost
  g.lon_e7 = f.lon_e7;
  g.alt_msl_m = f.alt_msl_m;
  g.speed_mps = f.speed_mps;
  g.course_deg = f.course_deg;
  const uint32_t age = gps_.last_gga_ms() ? now - gps_.last_gga_ms() : 0xFFFFu;
  g.age_ms = static_cast<uint16_t>(age > 0xFFFFu ? 0xFFFFu : age);
  g.utc_s = f.utc_s;
  return g;
}

uint32_t App::gps_period_ms() const {
  return fc_.state() == FlightState::Landed ? set_.tlm_landed_ms : set_.gps_tlm_ms;
}

// Bench mode: test commands allowed. RAM only; at boot it follows the setting bench_at_boot.
void App::set_bench(bool on, int32_t why, uint32_t now, const char* source) {
  bench_ = on;
  if (!on && remote_.armed()) {
    remote_.disarm();
    beeper_.clear();
    event(EventCode::RemoteDisarmed, static_cast<int32_t>(fc::RemoteFireEnd::Cancelled), now);
  }
  if (!on && eject_.armed() && eject_conditions() != fc::RemoteFireEnd::Fired) {
    eject_.disarm();
    beeper_.clear();
    event(EventCode::RemoteDisarmed, static_cast<int32_t>(fc::RemoteFireEnd::Cancelled), now);
  }
  event(EventCode::BenchMode, why, now);
  con.printf("[%lu] BENCH MODE %s (%s)%s\n", static_cast<unsigned long>(now), on ? "ON" : "OFF", source,
                on ? ": test commands enabled until 'bench off' or launch" : "");
}

// --- Battery sag during a pyro pulse -----------------------------------------
// The battery is read just before the pulse and as often as the loop allows
// while it lasts (about every 0.5 ms); the result is reported when it ends.
void App::start_sag() {
  sag_pre_v_ = batt_adc_.read_volts(4);
  sag_min_v_ = sag_pre_v_;
  sag_sum_ = 0;
  sag_n_ = 0;
  sag_last_us_ = micros();
  sag_active_ = true;
}

void App::update_sag(uint32_t now) {
  if (!sag_active_) return;
  if (pyro_.firing()) {
    const uint32_t us = micros();
    if (us - sag_last_us_ < 500) return;
    sag_last_us_ = us;
    const float v = batt_adc_.read_volts(2);
    if (v < sag_min_v_) sag_min_v_ = v;
    sag_sum_ += v;
    ++sag_n_;
    return;
  }
  sag_active_ = false;
  const uint32_t pre_mv = static_cast<uint32_t>(sag_pre_v_ * 1000.0f + 0.5f);
  const uint32_t min_mv = static_cast<uint32_t>(sag_min_v_ * 1000.0f + 0.5f);
  event(EventCode::PyroSag, static_cast<int32_t>(((pre_mv & 0xFFFFu) << 16) | (min_mv & 0xFFFFu)), now);
  const float mean = sag_n_ ? sag_sum_ / static_cast<float>(sag_n_) : sag_pre_v_;
  con.printf("[%lu] PYRO battery: %.3f V before, min %.3f V (drop %.3f V), mean %.3f V, %lu samples%s\n",
                static_cast<unsigned long>(now), static_cast<double>(sag_pre_v_), static_cast<double>(sag_min_v_),
                static_cast<double>(sag_pre_v_ - sag_min_v_), static_cast<double>(mean),
                static_cast<unsigned long>(sag_n_), have_battery_ ? "" : " (no cell: USB supply)");
  if (have_battery_ && sag_min_v_ < 3.3f)
    con.println("  WARNING: below 3.3 V during the pulse: risk of a brown-out reset. Check the cell, "
                   "its wiring and the 1000 uF capacitor.");
}

// Disarm when the window runs out or a condition is lost (e.g. arm link pulled).
void App::check_remote_fire(uint32_t now) {
  if (!remote_.armed()) return;
  fc::RemoteFireEnd why = fc::RemoteFireEnd::Timeout;
  if (!remote_.expired(now)) {
    why = remote_conditions();
    if (why == fc::RemoteFireEnd::Fired || why == fc::RemoteFireEnd::Busy) return;
    remote_.disarm();
  }
  beeper_.clear();
  event(EventCode::RemoteDisarmed, static_cast<int32_t>(why), now);
  con.printf("REMOTE FIRE: disarmed: %s\n", fc::to_string(why));
}

// --- Connectivity check (ground station button / 'ping') ---------------------
// Answers with a PONG event (sent at once, before telemetry) and beeps once,
// except in flight. The ground station beeps 0.5 s after the answer arrives:
// two separate sounds = both directions of the link work.
void App::handle_ping(const fc::tlm::Uplink& u, uint32_t now) {
  event(EventCode::Pong, u.pulse_ms, now);
  if (in_flight(fc_.state()) || warning_armed()) return;
  beeper_.clear();
  beeper_.push(150, 0);
  sound_until_ = now + 200;
  t_report_ = now;  // no pad beeps right on top of it
}

// --- Console by radio (TextCmd from the ground station) ---------------------
// The command runs as if typed on USB; its output goes back in TextOut frames.
// Settings print compactly (V lines). Not by radio: commands that stream a
// lot (dump, flog dump: use the WiFi download) or pulse the output directly
// (fire: use the remote test fire).
void App::handle_text(const fc::tlm::Text& t, uint32_t now) {
  const uint16_t crc = fc::crc16_ccitt(reinterpret_cast<const uint8_t*>(t.text), t.len);
  if (t.code != fc::text_code(t.seq, crc, set_.rf_key)) {
    event(EventCode::RemoteRefused, static_cast<int32_t>(fc::RemoteFireEnd::BadCode), now);
    return;
  }
  if (have_text_seq_ && t.seq == last_text_seq_) return;  // the same command repeated
  have_text_seq_ = true;
  last_text_seq_ = t.seq;
  char line[fc::tlm::kMaxCmdText + 1];
  memcpy(line, t.text, t.len);
  line[t.len] = '\0';
  con.capture(true);
  con.printf("> %s\n", line);
  const bool stream = !strncmp(line, "dump", 4) || !strncmp(line, "flog dump", 9) || !strncmp(line, "csv", 3);
  if (!strncmp(line, "fire", 4)) {
    con.println("refused by radio: use the remote test fire");
  } else if (stream) {
    con.println("refused by radio (too long): use the WiFi log download");
  } else {
    params_.set_compact(true);
    console_exec(line, now);
    params_.set_compact(false);
  }
  con.capture(false);
}

// --- Manual ejection by radio ------------------------------------------------
// Same two-step scheme as the remote test fire (EjectArm -> EJECT_ARMED with a
// one-time nonce -> Eject with the code from nonce and rf_key, within 10 s).
//  - On the pad: bench mode only, continuity required, a test pulse (like the
//    remote test fire, does not use the flight shot).
//  - After launch detection: only with manual_eject = 1 (set before the
//    flight; settings cannot change in flight) or in a simulated flight in
//    bench mode. Overrides the flight logic: no altitude, apogee or
//    continuity checks. Fires once; the flight logic then never fires.
fc::RemoteFireEnd App::eject_conditions() const {
  using fc::RemoteFireEnd;
  const FlightState s = fc_.state();
  if (pyro_.firing()) return RemoteFireEnd::Busy;
  if (s == FlightState::Pad) {
    if (!bench_) return RemoteFireEnd::NotLaunched;
    if (simulating()) return RemoteFireEnd::NotOnPad;
    if (!continuity_) return RemoteFireEnd::NoContinuity;
    return RemoteFireEnd::Fired;
  }
  if (!fc_.sm().launched()) return RemoteFireEnd::NotLaunched;
  if (!set_.manual_eject && !bench_) return RemoteFireEnd::ManualEjectOff;
  if (fc_.sm().fired() || pyro_.fired()) return RemoteFireEnd::AlreadyFired;
  return RemoteFireEnd::Fired;  // = all conditions met
}

void App::handle_eject(const fc::tlm::Uplink& u, uint32_t now) {
  using fc::RemoteFireEnd;
  // 0 = the configured flight pulse.
  const uint16_t pulse = u.pulse_ms ? u.pulse_ms : static_cast<uint16_t>(set_.pyro_pulse_ms);
  const bool flying = in_flight(fc_.state());
  if (u.kind == fc::tlm::UplinkKind::EjectArm) {
    const RemoteFireEnd r = eject_.arm(now, pulse, esp_random(), eject_conditions());
    if (r != RemoteFireEnd::Fired) {
      event(EventCode::RemoteRefused, static_cast<int32_t>(r), now);
      con.printf("MANUAL EJECT: arm refused: %s\n", fc::to_string(r));
      return;
    }
    event(EventCode::EjectArmed, static_cast<int32_t>(eject_.nonce()), now);
    con.printf("MANUAL EJECT: ARMED (%u ms pulse), fire window %lu s\n", eject_.pulse_ms(),
                  static_cast<unsigned long>(fc::RemoteFire::kWindowMs / 1000));
    if (!flying) {  // warning to anyone near the rocket
      beeper_.clear();
      for (uint32_t i = 0; i < fc::RemoteFire::kWindowMs / 200; ++i) beeper_.push(100, 100);
    }
    return;
  }

  const RemoteFireEnd r = eject_.fire(now, u.code, pulse, eject_conditions());
  if (!flying) beeper_.clear();
  if (r != RemoteFireEnd::Fired) {
    event(EventCode::RemoteRefused, static_cast<int32_t>(r), now);
    con.printf("MANUAL EJECT: refused: %s\n", fc::to_string(r));
    return;
  }
  const int32_t alt_cm = static_cast<int32_t>(fc_.estimator().alt_m() * 100.0f);
  if (fc_.state() == FlightState::Pad) {  // bench test of the command path
    watch_open_ = continuity_;
    start_sag();
    if (pyro_.bench_fire(now, pulse)) {
      event(EventCode::ManualEject, alt_cm, now);
      con.printf("MANUAL EJECT: test pulse FIRED %u ms (bench mode, on the pad)\n", pulse);
    } else {
      sag_active_ = false;
    }
    return;
  }
  // After launch: the flight logic must never fire again.
  fc_.note_manual_fire();
  event(EventCode::ManualEject, alt_cm, now);
  if (sim_) {
    sim_->deploy();
    if (!sim_live_) {
      con.println("MANUAL EJECT: [sim] fire (pyro output not driven; use 'sim live' to drive it)");
      return;
    }
  }
  start_sag();
  pyro_.set_armed(true);  // the override: the state machine's arming is not consulted
  const bool ok = pyro_.fire(now, pulse);
  pyro_.set_armed(fc_.sm().pyro_armed());
  if (ok) {
    watch_open_ = continuity_;
    con.printf("[%lu] MANUAL EJECT: PYRO FIRED (%u ms pulse)\n", static_cast<unsigned long>(now), pulse);
  } else {
    sag_active_ = false;
    con.printf("[%lu] MANUAL EJECT: pyro driver refused (fired before)\n", static_cast<unsigned long>(now));
  }
}

void App::check_eject(uint32_t now) {
  if (!eject_.armed()) return;
  fc::RemoteFireEnd why = fc::RemoteFireEnd::Timeout;
  if (!eject_.expired(now)) {
    why = eject_conditions();
    if (why == fc::RemoteFireEnd::Fired || why == fc::RemoteFireEnd::Busy) return;
  }
  eject_.disarm();
  if (!in_flight(fc_.state())) beeper_.clear();
  event(EventCode::RemoteDisarmed, static_cast<int32_t>(why), now);
  con.printf("MANUAL EJECT: disarmed: %s\n", fc::to_string(why));
}

void App::sync_time(uint64_t utc_ms, uint32_t now, const char* source) {
  if (!clock_.apply(utc_ms, now)) return;
  uint32_t t_ms;
  int32_t unix_s;
  clock_.event_point(now, &t_ms, &unix_s);
  event_at(EventCode::TimeSync, unix_s, t_ms);
  char buf[32];
  format_utc(now, buf, sizeof(buf));
  con.printf("[%lu] real time from %s: %s\n", static_cast<unsigned long>(now), source, buf);
}

void App::format_utc(uint32_t t_ms, char* buf, size_t cap) const {
  if (!clock_.synced()) {
    snprintf(buf, cap, "not synced");
    return;
  }
  const uint64_t ms = clock_.utc_ms(t_ms);
  const time_t s = static_cast<time_t>(ms / 1000);
  struct tm tm;
  gmtime_r(&s, &tm);
  snprintf(buf, cap, "%04d-%02d-%02d %02d:%02d:%02d.%03u UTC", tm.tm_year + 1900, tm.tm_mon + 1,
           tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<unsigned>(ms % 1000));
}

void App::update_radio(uint32_t now) {
  radio_.poll();
  if (!radio_.ok() || radio_.busy()) return;
  receive_uplink(now);
  // Leave a short receive window after each transmission for the ground
  // station's reply. In flight only when it may send a manual ejection.
  const bool flying = in_flight(fc_.state());
  if ((!flying || listen_in_flight()) && radio_.tx_done_ms() != 0 &&
      now - radio_.tx_done_ms() < (flying ? rx_window_flight_ms_ : rx_window_ms_))
    return;

  uint8_t buf[fc::tlm::kMaxFrameLen];
  if (evq_count_ > 0) {
    fc::tlm::Event e = evq_[evq_head_];
    if (!duty_.try_consume(now, airtime_evt_us_)) return;  // retry when budget allows
    e.seq = radio_seq_;
    const size_t n = fc::tlm::encode(e, buf, sizeof(buf));
    if (radio_.send(buf, n)) {
      ++radio_seq_;
      evq_head_ = (evq_head_ + 1) % kEventQueue;
      --evq_count_;
    }
    return;
  }

  // Console output for the ground station (a command received by radio).
  if (con.pending() > 0 || text_len_ > 0) {
    if (text_len_ == 0) text_len_ = con.take(text_buf_, fc::tlm::kMaxText);
    fc::tlm::Text t;
    t.state = fc_.state();
    t.t_ms = now;
    t.seq = radio_seq_;
    t.len = static_cast<uint8_t>(text_len_);
    memcpy(t.text, text_buf_, text_len_);
    const size_t n = fc::tlm::encode(t, buf2_, sizeof(buf2_));
    if (n && duty_.try_consume(now, fc::lora_airtime_us(fc::lora_modulation(set_), n)) && radio_.send(buf2_, n)) {
      ++radio_seq_;
      text_len_ = 0;
    }
    return;
  }

  // GPS position (optional module): its own, slower period.
  if (gps_state_ >= 1 && (t_gps_tx_ == 0 || now - t_gps_tx_ >= gps_period_ms())) {
    t_gps_tx_ = now;
    if (duty_.try_consume(now, airtime_gps_us_)) {
      fc::tlm::Gps g = gps_snapshot(now);
      g.seq = radio_seq_;
      const size_t n = fc::tlm::encode(g, buf, sizeof(buf));
      if (radio_.send(buf, n)) ++radio_seq_;
      return;
    }
  }

  if (t_tlm_ != 0 && now - t_tlm_ < telemetry_period_ms()) return;
  t_tlm_ = now;
  if (!duty_.try_consume(now, airtime_tlm_us_)) {
    ++tx_denied_;
    return;
  }
  fc::tlm::Telemetry t = snapshot(now);
  t.seq = radio_seq_;
  const size_t n = fc::tlm::encode(t, buf, sizeof(buf));
  if (radio_.send(buf, n)) ++radio_seq_;
}
