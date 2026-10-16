// USB serial console of the flight computer (115200 baud, line based).
// Commands that block for a while are only accepted when nothing can happen
// (INIT, PAD, FAULT, LANDED). Bench-only commands exist only in the bench build.
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "board_pins.h"
#include "fc/csv.h"
#include "flight_config.h"
#include "hal/system.h"
#include "radio_config.h"

using fc::FlightState;

namespace {
const char* level_name(fc::BatteryMonitor::Level l) { return fc::to_string(l); }
}  // namespace

bool App::safe_to_interrupt() const {
  const FlightState s = fc_.state();
  return (s == FlightState::Init || s == FlightState::Pad || s == FlightState::Fault ||
          s == FlightState::Landed) &&
         !pyro_.firing();
}

void App::console_poll(uint32_t now) {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\r' || c == '\n') {
      if (cmd_len_ > 0) {
        cmd_[cmd_len_] = '\0';
        cmd_len_ = 0;
        console_exec(cmd_, now);
      }
    } else if (cmd_len_ < sizeof(cmd_) - 1) {
      cmd_[cmd_len_++] = static_cast<char>(c);
    }
  }
}

void App::console_exec(char* line, uint32_t now) {
  char* save = nullptr;
  const char* cmd = strtok_r(line, " \t", &save);
  const char* a1 = strtok_r(nullptr, " \t", &save);
  const char* a2 = strtok_r(nullptr, " \t", &save);
  (void)a2;  // used by bench-only commands
  if (!cmd) return;

  if (!strcmp(cmd, "help")) {
    Serial.println(
        "commands:\n"
        "  status          sensors, estimate, battery, pyro, radio\n"
        "  config          flight configuration in this firmware\n"
        "  csv on|off      stream telemetry as CSV at 10 Hz\n"
        "  dump            print the onboard log as CSV (not in flight)\n"
        "  logclear        erase the onboard log (not in flight)\n"
        "  i2c             scan the I2C bus (not in flight)\n"
        "  reboot          restart (not in flight)"
#ifdef FC_BENCH_BUILD
        "\n bench build only:\n"
        "  sim             simulated flight: fake sensors, pyro output NOT driven\n"
        "  sim live        simulated flight that also pulses the pyro output\n"
        "  fire <ms> CONFIRM   pulse the pyro output now (igniter test, no charge!)\n"
        "  beep            test the buzzer"
#endif
    );
  } else if (!strcmp(cmd, "status")) {
    print_status(now);
  } else if (!strcmp(cmd, "config")) {
    print_config();
  } else if (!strcmp(cmd, "csv")) {
    csv_stream_ = a1 && !strcmp(a1, "on");
    if (csv_stream_) Serial.println(fc::csv::telemetry_header());
  } else if (!safe_to_interrupt()) {
    Serial.println("refused: in flight");
  } else if (!strcmp(cmd, "dump")) {
    dump_log();
  } else if (!strcmp(cmd, "logclear")) {
    ram_log_.ring().clear();
    Serial.println("onboard log cleared");
  } else if (!strcmp(cmd, "i2c")) {
    i2c_scan();
  } else if (!strcmp(cmd, "reboot")) {
    Serial.println("rebooting");
    Serial.flush();
    hal::clear_flight_record();
    ESP.restart();
#ifdef FC_BENCH_BUILD
  } else if (!strcmp(cmd, "sim")) {
    start_sim(a1 && !strcmp(a1, "live"), now);
  } else if (!strcmp(cmd, "beep")) {
    beeper_.beeps(3);
  } else if (!strcmp(cmd, "fire")) {
    const long ms = a1 ? strtol(a1, nullptr, 10) : 0;
    if (ms <= 0 || ms > static_cast<long>(hal::PyroChannel::kMaxPulseMs) || !a2 ||
        strcmp(a2, "CONFIRM") != 0) {
      Serial.printf("usage: fire <1..%lu ms> CONFIRM\n",
                    static_cast<unsigned long>(hal::PyroChannel::kMaxPulseMs));
    } else if (pyro_.bench_fire(now, static_cast<uint32_t>(ms))) {
      event(fc::EventCode::BenchFire, ms, now);
      watch_open_ = continuity_;
    }
#endif
  } else {
    Serial.printf("unknown command '%s' (type 'help')\n", cmd);
  }
}

void App::print_status(uint32_t now) {
  const fc::Estimator& est = fc_.estimator();
  Serial.printf("firmware   %s%s, reset: %s%s\n", FC_GIT_REV,
#ifdef FC_BENCH_BUILD
                " [BENCH]",
#else
                "",
#endif
                hal::reset_reason_name(), resumed_ ? " (flight RESUMED)" : "");
  Serial.printf("state      %s  uptime %lu s  fault %u  warnings 0x%02X%s\n",
                to_string(fc_.state()), static_cast<unsigned long>(now / 1000),
                static_cast<unsigned>(fault_), warnings_, simulating() ? "  [SIMULATION]" : "");
  Serial.printf("estimate   alt %.2f m  vel %.2f m/s  max %.2f m  accel_up %.3f g\n",
                static_cast<double>(est.alt_m()), static_cast<double>(est.vel_mps()),
                static_cast<double>(fc_.sm().max_alt_m()), static_cast<double>(est.accel_up_g()));
  Serial.printf("baro       %s  %.1f Pa  %.2f C  ground %.1f Pa  errors %lu\n",
                baro_ok_ ? "ok" : "--", static_cast<double>(pressure_pa_),
                static_cast<double>(temp_c_), static_cast<double>(est.ground_pa()),
                static_cast<unsigned long>(baro_.errors()));
  Serial.printf("imu        %s %s  accel %.3f %.3f %.3f g  gyro %.1f %.1f %.1f dps  errors %lu\n",
                imu_.chip_name(), imu_ok_ ? "ok" : "--", static_cast<double>(imu_s_.accel_g.x),
                static_cast<double>(imu_s_.accel_g.y), static_cast<double>(imu_s_.accel_g.z),
                static_cast<double>(imu_s_.gyro_dps.x), static_cast<double>(imu_s_.gyro_dps.y),
                static_cast<double>(imu_s_.gyro_dps.z), static_cast<unsigned long>(imu_.errors()));
  Serial.printf("attitude   tilt %.1f deg from vertical%s\n",
                static_cast<double>(fc::tilt_deg(ahrs_.q())), imu_ok_ ? "" : " (no IMU)");
  Serial.printf("mag        %s  %.1f %.1f %.1f uT\n", imu_.has_mag() ? "ok" : "--",
                static_cast<double>(imu_s_.mag_uT.x), static_cast<double>(imu_s_.mag_uT.y),
                static_cast<double>(imu_s_.mag_uT.z));
  if (have_battery_) {
    Serial.printf("battery    %.3f V  ~%.0f %%  %s\n", static_cast<double>(vbat_),
                  static_cast<double>(fc::liion_percent(vbat_)), level_name(batt_.level()));
  } else {
    Serial.printf("battery    none (%.2f V): running from USB\n", static_cast<double>(vbat_));
  }
  Serial.printf("pyro       drain %.2f V  continuity %s  armed %s  fired %s%s\n",
                static_cast<double>(vpyro_), continuity_ ? "YES" : "no",
                pyro_.armed() ? "YES" : "no", (fc_.sm().fired() || pyro_.fired()) ? "YES" : "no",
                pyro_.firing() ? "  FIRING" : "");
  Serial.printf("radio      %s  tx %lu  errors %lu  denied %lu  budget %.1f s  queue %u  "
                "dropped %lu\n",
                radio_.ok() ? "ok" : "FAIL", static_cast<unsigned long>(radio_.tx_count()),
                static_cast<unsigned long>(radio_.tx_errors()),
                static_cast<unsigned long>(tx_denied_),
                static_cast<double>(duty_.available_us(now)) / 1e6,
                static_cast<unsigned>(evq_count_), static_cast<unsigned long>(ev_dropped_));
  Serial.printf("log        %s  %u / %u frames  overwritten %lu\n", ram_log_.name(),
                static_cast<unsigned>(ram_log_.ring().count()),
                static_cast<unsigned>(ram_log_.ring().capacity()),
                static_cast<unsigned long>(ram_log_.ring().overwritten()));
  Serial.printf("timing     longest control tick %lu us (budget %lu us)\n",
                static_cast<unsigned long>(loop_max_us_),
                static_cast<unsigned long>(cfg::kLoopMs * 1000));
}

void App::print_config() {
  const fc::FlightComputer::Config c = cfg::flight_computer();
  const fc::FlightConfig& f = c.flight;
  Serial.printf("imu up axis        %s\n", fc::to_string(cfg::kImuUpAxis));
  Serial.printf("settle / calib     %lu / %lu ms\n", static_cast<unsigned long>(c.settle_ms),
                static_cast<unsigned long>(c.calib_ms));
  Serial.printf("launch             >= %.1f g for %lu ms, or >= %.0f m for %lu ms\n",
                static_cast<double>(f.launch_accel_g), static_cast<unsigned long>(f.launch_accel_ms),
                static_cast<double>(f.launch_alt_m), static_cast<unsigned long>(f.launch_alt_ms));
  Serial.printf("burnout            < %.1f g for %lu ms\n", static_cast<double>(f.burnout_accel_g),
                static_cast<unsigned long>(f.burnout_ms));
  Serial.printf("apogee             lockout %lu ms, drop %.1f m held %lu ms, min alt %.0f m\n",
                static_cast<unsigned long>(f.apogee_lockout_ms),
                static_cast<double>(f.apogee_drop_m), static_cast<unsigned long>(f.apogee_confirm_ms),
                static_cast<double>(f.min_deploy_alt_m));
  if (f.backup_deploy_ms) {
    Serial.printf("backup timer       %lu ms after launch\n",
                  static_cast<unsigned long>(f.backup_deploy_ms));
  } else {
    Serial.println("backup timer       disabled");
  }
  Serial.printf("landed             within %.1f m for %lu ms\n",
                static_cast<double>(f.landed_window_m), static_cast<unsigned long>(f.landed_ms));
  Serial.printf("pyro pulse         %lu ms\n", static_cast<unsigned long>(cfg::kPyroPulseMs));
  Serial.printf("radio              %.3f MHz BW %.0f kHz SF%u CR4/%u sync 0x%02X %d dBm\n",
                static_cast<double>(radio_cfg::kFreqMHz),
                static_cast<double>(radio_cfg::kBandwidthKHz), radio_cfg::kSpreadingFactor,
                radio_cfg::kCodingRateDenom, radio_cfg::kSyncWord, radio_cfg::kTxPowerDbm);
  Serial.printf("airtime            telemetry %lu us, event %lu us (RadioLib: %lu us)\n",
                static_cast<unsigned long>(airtime_tlm_us_),
                static_cast<unsigned long>(airtime_evt_us_),
                static_cast<unsigned long>(radio_.time_on_air_us(fc::tlm::kTelemetryLen)));
  Serial.printf("telemetry period   pad %lu ms, flight %lu ms, landed %lu ms\n",
                static_cast<unsigned long>(cfg::kTlmPadMs),
                static_cast<unsigned long>(cfg::kTlmFlightMs),
                static_cast<unsigned long>(cfg::kTlmLandedMs));
  Serial.printf("battery            low %.2f V, critical %.2f V (sleep after %lu s)\n",
                static_cast<double>(cfg::battery().low_v),
                static_cast<double>(cfg::battery().critical_v),
                static_cast<unsigned long>(cfg::kBattShutdownMs / 1000));
}

void App::print_csv_line(uint32_t now) {
  if (now - t_csv_ < 100) return;
  t_csv_ = now;
  char line[fc::csv::kLineMax];
  if (fc::csv::format(snapshot(now), line, sizeof(line))) Serial.println(line);
}

void App::i2c_scan() {
  Serial.print("I2C devices:");
  int found = 0;
  for (uint8_t a = 1; a < 127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", a);
      ++found;
    }
    hal::watchdog_feed();
  }
  Serial.println(found ? "" : " none");
  Serial.println("  expected: 0x68 (MPU9250), 0x76 or 0x77 (BMP280); 0x0C (AK8963) after init");
}

void App::dump_log() {
  const fc::FrameRing& r = ram_log_.ring();
  Serial.printf("# onboard log: %u frames, %lu overwritten, firmware %s\n",
                static_cast<unsigned>(r.count()), static_cast<unsigned long>(r.overwritten()),
                FC_GIT_REV);
  Serial.println(fc::csv::telemetry_header());
  Serial.println(fc::csv::event_header());
  uint8_t frame[fc::FrameRing::kSlot];
  char line[fc::csv::kLineMax];
  for (size_t i = 0; i < r.count(); ++i) {
    const size_t n = r.get(i, frame, sizeof(frame));
    if (fc::csv::format_frame(frame, n, line, sizeof(line))) Serial.println(line);
    if ((i & 31) == 0) {
      hal::watchdog_feed();
      pyro_.update(millis());
    }
  }
  Serial.println("# end of log");
}

#ifdef FC_BENCH_BUILD
void App::start_sim(bool live, uint32_t now) {
  if (sim_) {
    Serial.println("a simulation already ran: 'reboot' first");
    return;
  }
  fc::SimParams p;
  p.ignition_ms = cfg::kSettleMs + 5000;
  p.seed = now | 1u;
  sim_ = new fc::SimFlight(p);
  sim_last_ms_ = now;
  sim_live_ = live;
  sim_reported_ = false;

  // Fresh flight computer and a fresh (bench-only) pyro shot.
  hal::clear_flight_record();
  fc_ = fc::FlightComputer(cfg::flight_computer());
  pyro_.begin(pins::kPyroGate);
  resumed_ = false;
  lost_reported_ = 0;
  baro_good_ = 0;
  beeper_.clear();
  start_countdown(now);
  Serial.printf("[sim] simulated flight started (%s): ignition in %lu s. Sensors are FAKE "
                "until reboot.\n",
                live ? "LIVE: pyro output WILL pulse" : "pyro output not driven",
                static_cast<unsigned long>(p.ignition_ms / 1000));
}
#endif
