#include "app.h"

#include "board_pins.h"
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

fc::LoraModulation modulation() {
  fc::LoraModulation m;
  m.sf = radio_cfg::kSpreadingFactor;
  m.bw_hz = static_cast<uint32_t>(radio_cfg::kBandwidthKHz * 1000.0f);
  m.cr_denom = radio_cfg::kCodingRateDenom;
  m.preamble = radio_cfg::kPreambleLen;
  return m;
}

#ifdef FC_BENCH_BUILD
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
#endif

}  // namespace

App::App()
    : fc_(cfg::flight_computer()),
      batt_(cfg::battery()),
      duty_(radio_cfg::kDutyFraction, radio_cfg::kDutyBurstMs),
      ground_(cfg::kUplinkTimeoutMs) {}

bool App::simulating() const {
#ifdef FC_BENCH_BUILD
  return sim_ != nullptr;
#else
  return false;
#endif
}

// =============================================================================
// Setup
// =============================================================================

void App::setup() {
  // Pyro gate low before anything else (the 100k pull-down holds it until now).
  pyro_.begin(pins::kPyroGate);
  buzzer_.begin(pins::kBuzzer);
  led_.begin(pins::kStatusLed);

  Serial.begin(115200);
  Serial.setTxTimeoutMs(5);  // never let a stalled USB host block the control loop

  reset_cause_ = hal::reset_cause();
  Serial.printf("\nrocket-fc %s%s  reset: %s\n", FC_GIT_REV,
#ifdef FC_BENCH_BUILD
                "  [BENCH BUILD]",
#else
                "",
#endif
                hal::reset_reason_name());

  batt_adc_.begin(pins::kBattSense, cfg::kBattDividerRatio);
  pyro_adc_.begin(pins::kPyroSense, cfg::kPyroDividerRatio);

  Wire.begin(pins::kI2cSda, pins::kI2cScl, 400000);
  Wire.setTimeOut(5);  // ms: a hung sensor must not stall the loop
  init_sensors();
  init_radio();
  init_log();

  const uint32_t now = millis();
  duty_.reset(now);
  airtime_tlm_us_ = fc::lora_airtime_us(modulation(), fc::tlm::kTelemetryLen);
  airtime_evt_us_ = fc::lora_airtime_us(modulation(), fc::tlm::kEventLen);

  event(EventCode::Boot, static_cast<int32_t>(FC_GIT_REV_U32), now);
  try_resume(now);
  if (!resumed_) start_countdown(now);

  update_battery(now);
  hal::watchdog_begin(cfg::kWatchdogS);
  t_tick_ = t_slow_ = t_report_ = now;
  Serial.println("type 'help' for commands");
}

void App::init_sensors() {
  // List everything on the bus first: tells wiring faults from driver faults.
  Serial.print("I2C bus:");
  int found = 0;
  for (uint8_t a = 1; a < 127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", a);
      ++found;
    }
  }
  Serial.println(found ? "  (MPU-9250 = 0x68/0x69, BMP280 = 0x76/0x77)" : " nothing (check SDA/SCL/3V3/GND)");

  if (imu_.begin(&Wire, 0x68) || imu_.begin(&Wire, 0x69)) {
    Serial.printf("IMU: %s (WHO_AM_I 0x%02X)%s\n", imu_.chip_name(), imu_.who_am_i(),
                  imu_.has_mag() ? " + AK8963 magnetometer" : ", no magnetometer");
  } else if (imu_.who_am_i() != 0) {
    Serial.printf("IMU: answered (WHO_AM_I 0x%02X) but could not be configured\n", imu_.who_am_i());
  } else {
    Serial.println("IMU: not found at 0x68 or 0x69");
  }
  if (baro_.begin(&Wire)) {
    Serial.printf("Baro: %s at 0x%02X\n", baro_.chip_id() == 0x60 ? "BME280" : "BMP280",
                  baro_.address());
  } else {
    Serial.println("Baro: not found");
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
  if (imu_.begin(&Wire, 0x68) || imu_.begin(&Wire, 0x69)) {
    Serial.printf("IMU: found after boot: %s (WHO_AM_I 0x%02X)%s\n", imu_.chip_name(),
                  imu_.who_am_i(), imu_.has_mag() ? " + AK8963 magnetometer" : ", no magnetometer");
    Serial.println("     gyro bias was not calibrated: restart with the rocket still for best attitude");
  }
}

void App::init_radio() {
  const hal::RadioLink::Pins p{pins::kLoraSck,  pins::kLoraMiso,  pins::kLoraMosi,
                               pins::kLoraNss,  pins::kLoraDio1,  pins::kLoraReset,
                               pins::kLoraBusy, pins::kLoraRfSw};
  const hal::RadioLink::Params r{radio_cfg::kFreqMHz,         radio_cfg::kBandwidthKHz,
                                 radio_cfg::kSpreadingFactor, radio_cfg::kCodingRateDenom,
                                 radio_cfg::kSyncWord,        radio_cfg::kTxPowerDbm,
                                 radio_cfg::kPreambleLen,     radio_cfg::kTcxoVolts};
  if (radio_.begin(p, r)) {
    radio_.set_listen(true);  // hear ground station heartbeats between transmissions
    radio_.start_receive();
    Serial.printf("Radio: SX1262 %.3f MHz SF%u BW%.0f, %d dBm\n",
                  static_cast<double>(radio_cfg::kFreqMHz), radio_cfg::kSpreadingFactor,
                  static_cast<double>(radio_cfg::kBandwidthKHz), radio_cfg::kTxPowerDbm);
  } else {
    Serial.printf("Radio: init failed (RadioLib error %d)\n", radio_.last_error());
  }
}

void App::init_log() {
  ram_log_.begin();
  add_sink(&ram_log_);
  // Future: static SdLog sd_log_; if (sd_log_.begin(pins::kSdCs)) add_sink(&sd_log_);
  for (size_t i = 0; i < n_sinks_; ++i) {
    Serial.printf("Log: %s %s\n", sinks_[i]->name(), sinks_[i]->ok() ? "ok" : "FAILED");
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
  Serial.printf("RESUMED flight in %s, %lu ms after launch, fired=%u\n", to_string(fc_.state()),
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
  Serial.printf("Keep the rocket still: calibrating for %lu s\n",
                static_cast<unsigned long>(cfg::kSettleMs / 1000));
}

// =============================================================================
// Main loop
// =============================================================================

void App::loop() {
  const uint32_t now = millis();

  pyro_.update(now);  // end the pulse on time, whatever else happens

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
  update_indicators(now);
  console_poll(now);
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
      if (s > 0) beeper_.push(40, 0);
    }
    if (imu_ok_ && rem <= cfg::kCalibMs) {
      gyro_sum_.x += imu_s_.gyro_dps.x;
      gyro_sum_.y += imu_s_.gyro_dps.y;
      gyro_sum_.z += imu_s_.gyro_dps.z;
      up_sum_ += fc::axis_component(imu_s_.accel_g, cfg::kImuUpAxis);
      ++cal_n_;
    }
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
  const fc::Axis up = cfg::kImuUpAxis;
  ahrs_.update(dt, fc::to_rocket_frame(imu_s_.gyro_dps, up), fc::to_rocket_frame(imu_s_.accel_g, up),
               fc::to_rocket_frame(imu_s_.mag_uT, up), imu_.has_mag());
}

void App::read_sensors(uint32_t now) {
#ifdef FC_BENCH_BUILD
  if (sim_) {
    const fc::SimSample s = sim_->step(now - sim_last_ms_);
    sim_last_ms_ = now;
    imu_s_ = hal::Mpu9250::Sample{};
    // The simulator reports accel along +X = nose; map it to the configured axis.
    imu_s_.accel_g = along(cfg::kImuUpAxis, s.accel_g.x);
    pressure_pa_ = s.pressure_pa;
    temp_c_ = 20.0f;
    imu_ok_ = baro_ok_ = true;
    ++baro_good_;
    return;
  }
#else
  (void)now;
#endif

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

  if (const char* why = fc::validate(cfg::flight_computer().flight)) {
    fault_ = fc::FaultCode::ConfigInvalid;
    Serial.printf("SELF-TEST: flight config invalid: %s\n", why);
  } else if (baro_good_ < 10 || !baro_ok_) {
    fault_ = fc::FaultCode::BaroMissing;
  } else if (!(pressure_pa_ > 30000.0f && pressure_pa_ < 110000.0f)) {
    fault_ = fc::FaultCode::BaroImplausible;
  }

  if (!imu_ok_) {
    warnings_ |= fc::kWarnImuMissing;
  } else {
    if (!imu_.has_mag() && !simulating()) warnings_ |= fc::kWarnMagMissing;
    if (cal_n_ > 0) {
      const float n = static_cast<float>(cal_n_);
      const float up = up_sum_ / n;
      if (up < 0.85f || up > 1.15f) warnings_ |= fc::kWarnImuAxis;
      imu_.set_gyro_bias(fc::Vec3{gyro_sum_.x / n, gyro_sum_.y / n, gyro_sum_.z / n});
    }
  }
  if (!radio_.ok()) warnings_ |= fc::kWarnRadioFail;
  if (have_battery_ && vbat_ < cfg::battery().low_v) warnings_ |= fc::kWarnBattLow;
  if (cfg::kBackupDeployMs == 0) warnings_ |= fc::kWarnNoBackupTimer;

  const bool cal_ok = fc_.finish_init(fault_ == fc::FaultCode::None);
  if (!cal_ok && fault_ == fc::FaultCode::None) fault_ = fc::FaultCode::BaroMissing;

  beeper_.clear();
  if (fault_ != fc::FaultCode::None) {
    event(EventCode::SelfTestFail, static_cast<int32_t>(fault_), now);
    beeper_.beeps(static_cast<uint8_t>(fault_), 600, 300, 1500);
    Serial.printf("SELF-TEST FAILED: fault %u -> FAULT state, will not fly\n",
                  static_cast<unsigned>(fault_));
  } else {
    event(EventCode::SelfTestOk, warnings_, now);
    beeper_.beeps(2, 150, 150, 1000);
    Serial.printf("SELF-TEST OK: ground %.1f Pa, warnings 0x%02X\n",
                  static_cast<double>(fc_.estimator().ground_pa()), warnings_);
  }
  if (warnings_ & fc::kWarnImuMissing) Serial.println("  warning: IMU not responding");
  if (warnings_ & fc::kWarnMagMissing) Serial.println("  warning: no magnetometer");
  if (warnings_ & fc::kWarnImuAxis)
    Serial.printf("  warning: up axis reads %.2f g (expected ~1.00): check kImuUpAxis / "
                  "orientation\n",
                  static_cast<double>(cal_n_ ? up_sum_ / static_cast<float>(cal_n_) : 0.0f));
  if (warnings_ & fc::kWarnRadioFail) Serial.println("  warning: radio not working");
  if (warnings_ & fc::kWarnBattLow) Serial.println("  warning: battery low");
  if (warnings_ & fc::kWarnNoBackupTimer)
    Serial.println("  note: backup deployment timer disabled (kBackupDeployMs = 0)");
  t_report_ = now;
}

void App::handle_output(const fc::SmOutput& out, uint32_t now) {
  for (size_t i = 0; i < out.n_events; ++i) event(out.events[i].code, out.events[i].arg, now);
  if (!out.fire) return;

#ifdef FC_BENCH_BUILD
  if (sim_) {
    sim_->deploy();
    if (!sim_live_) {
      Serial.println("[sim] FIRE command (pyro output not driven; use 'sim live' to drive it)");
      return;
    }
  }
#endif
  if (pyro_.fire(now, cfg::kPyroPulseMs)) {
    watch_open_ = continuity_;  // only meaningful if the circuit was closed
    Serial.printf("[%lu] PYRO FIRED (%lu ms pulse)\n", static_cast<unsigned long>(now),
                  static_cast<unsigned long>(cfg::kPyroPulseMs));
  } else {
    Serial.printf("[%lu] PYRO fire command REFUSED by driver (armed=%d fired=%d)\n",
                  static_cast<unsigned long>(now), pyro_.armed(), pyro_.fired());
  }
}

void App::on_state_change(FlightState from, FlightState to, uint32_t now) {
  Serial.printf("[%lu] state %s -> %s\n", static_cast<unsigned long>(now), to_string(from),
                to_string(to));
  if (to == FlightState::Boost) {
    beeper_.clear();  // no buzzer during flight
    t_tlm_ = 0;       // send telemetry right away
  }
  if (to == FlightState::Landed) {
    for (size_t i = 0; i < n_sinks_; ++i) sinks_[i]->flush();
    t_report_ = now - cfg::kPadReportMs;  // start the altitude beacon now
  }
}

// =============================================================================
// Slow tasks: power, continuity, beeps
// =============================================================================

void App::slow_tick(uint32_t now) {
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
#ifdef FC_BENCH_BUILD
  if (sim_ && !sim_reported_ && fc_.state() == FlightState::Landed) {
    sim_reported_ = true;
    Serial.printf("[sim] complete: true apogee %.1f m at t=%lu ms; computer max %.1f m\n",
                  static_cast<double>(sim_->apogee_alt_m()),
                  static_cast<unsigned long>(sim_->apogee_ms()),
                  static_cast<double>(fc_.sm().max_alt_m()));
  }
#endif
}

void App::update_battery(uint32_t now) {
  vbat_ = batt_adc_.read_volts();
  // Below 2.5 V there is no cell (running from USB with the switch off).
  have_battery_ = vbat_ > 2.5f;
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
  if (batt_.level() == fc::BatteryMonitor::Level::Critical && safe) {
    if (!critical_timing_) {
      critical_timing_ = true;
      critical_since_ = now;
    } else if (now - critical_since_ >= cfg::kBattShutdownMs) {
      shutdown("battery critical");
    }
  } else {
    critical_timing_ = false;
  }
}

void App::update_continuity(uint32_t now) {
  vpyro_ = pyro_adc_.read_volts();
  if (pyro_.firing()) return;  // the MOSFET pulls the drain low while firing
  continuity_ = have_battery_ && vpyro_ >= cfg::kContinuityFraction * vbat_;

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
  uint32_t period_ms = cfg::kPadReportMs;
  if (fc_.state() == FlightState::Pad) {
    // Pad beeps only while the ground station is heard, at the period it asks for.
    const uint8_t s = ground_.pad_beep_period_s(now);
    if (s == 0) return;
    period_ms = s * 1000u;
  }
  if (now - t_report_ < period_ms) return;
  switch (fc_.state()) {
    case FlightState::Pad:
      // 3 short = igniter continuity, 1 long = open circuit / arm link out.
      if (continuity_) {
        beeper_.beeps(3, 80, 120, 0);
      } else {
        beeper_.beeps(1, 500, 0, 0);
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
    case FlightState::Descent: m = fc::LedMode::Flight; break;
    case FlightState::Landed: m = fc::LedMode::Landed; break;
  }
  const bool low = batt_.level() == fc::BatteryMonitor::Level::Low ||
                   batt_.level() == fc::BatteryMonitor::Level::Critical;
  if (low && (fc_.state() == FlightState::Pad || fc_.state() == FlightState::Landed)) {
    m = fc::LedMode::LowBattery;
  }
  led_.set(fc::led_on(m, now));
  buzzer_.set(beeper_.update(now));
}

void App::shutdown(const char* why) {
  Serial.printf("SHUTDOWN: %s (vbat %.2f V). Power cycle to restart.\n", why,
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
  t.accel_g = fc::to_rocket_frame(imu_s_.accel_g, cfg::kImuUpAxis);
  t.gyro_dps = fc::to_rocket_frame(imu_s_.gyro_dps, cfg::kImuUpAxis);
  t.mag_uT = fc::to_rocket_frame(imu_s_.mag_uT, cfg::kImuUpAxis);
  t.q = imu_ok_ ? ahrs_.q() : fc::Quat{};
  t.link = ground_.connected(now) ? fc::tlm::kLinkGroundHeard : 0;
  t.uplink_rssi_dbm = ground_.rssi_dbm(now);
  t.beep_period_s = ground_.pad_beep_period_s(now);
  t.pressure_pa = pressure_pa_;
  t.temp_c = temp_c_;
  t.batt_v = vbat_;
  t.flags = flags();
  return t;
}

void App::event(EventCode code, int32_t arg, uint32_t now) {
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

  Serial.printf("[%lu] EVENT %s arg=%ld\n", static_cast<unsigned long>(now), to_string(code),
                static_cast<long>(arg));
}

void App::write_log(const uint8_t* frame, size_t len) {
  if (len == 0) return;
  for (size_t i = 0; i < n_sinks_; ++i) sinks_[i]->write_frame(frame, len);
}

void App::update_log(uint32_t now) {
  const uint32_t period = in_flight(fc_.state()) ? cfg::kLogFlightMs : cfg::kLogPadMs;
  if (now - t_log_ < period) return;
  t_log_ = now;
  fc::tlm::Telemetry t = snapshot(now);
  t.seq = log_seq_++;
  uint8_t buf[fc::tlm::kMaxFrameLen];
  write_log(buf, fc::tlm::encode(t, buf, sizeof(buf)));
}

uint32_t App::telemetry_period_ms() const {
  switch (fc_.state()) {
    case FlightState::Boost:
    case FlightState::Coast:
    case FlightState::Descent: return cfg::kTlmFlightMs;
    case FlightState::Landed: return cfg::kTlmLandedMs;
    default: return cfg::kTlmPadMs;
  }
}

void App::receive_uplink(uint32_t now) {
  uint8_t buf[32];
  float rssi = 0, snr = 0;
  bool crc_error = false;
  const size_t n = radio_.receive(buf, sizeof(buf), &rssi, &snr, &crc_error);
  if (n == 0 || crc_error) return;
  fc::tlm::Uplink u;
  if (fc::tlm::decode(buf, n, &u) != fc::tlm::DecodeResult::Ok) return;
  if (in_flight(fc_.state())) return;  // never act on the uplink in flight
  const float r = rssi < -127.0f ? -127.0f : rssi > 0.0f ? 0.0f : rssi;
  ground_.heard(now, static_cast<int8_t>(r), u.beep_period_s);
}

void App::update_radio(uint32_t now) {
  radio_.poll();
  if (!radio_.ok() || radio_.busy()) return;
  receive_uplink(now);
  // On the ground, leave a short receive window after each transmission for
  // the ground station's heartbeat reply. In flight, transmit freely.
  if (!in_flight(fc_.state()) && radio_.tx_done_ms() != 0 &&
      now - radio_.tx_done_ms() < cfg::kUplinkRxWindowMs)
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
