// USB serial console of the flight computer (115200 baud, line based).
// Commands that block for a while are only accepted when nothing can happen
// (INIT, PAD, FAULT, LANDED). Test commands need bench mode ('bench on', RAM
// only: every boot starts in flight mode).
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
void serial_line(void*, const char* line) { con.println(line); }
bool store_text(void* ctx, const char* text, size_t len) {
  return static_cast<hal::SettingsStore*>(ctx)->save(text, len);
}
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
  const char* argv[6] = {};
  int argc = 0;
  for (char* t = strtok_r(line, " \t", &save); t && argc < 6; t = strtok_r(nullptr, " \t", &save)) argv[argc++] = t;
  const char* cmd = argv[0];
  const char* a1 = argv[1];
  const char* a2 = argv[2];
  if (!cmd) return;

  if (!strcmp(cmd, "help")) {
    con.println(
        "commands:\n"
        "  status          sensors, estimate, battery, pyro, radio\n"
        "  config          flight configuration in this firmware\n"
        "  csv on|off      stream telemetry as CSV at 10 Hz\n"
        "  dump            print the RAM log of this boot as CSV (not in flight)\n"
        "  flog [list]     flash log sessions (kept after power-off)\n"
        "  flog dump <n|all>   print flash log session n as CSV\n"
        "  flog erase CONFIRM  erase the whole flash log (about a minute)\n"
        "  logclear        erase the onboard log (not in flight)\n"
        "  i2c             scan the I2C bus (not in flight)\n"
        "  time <unix ms>  set real time for the log (once per boot; the UI sends it)\n"
        "  param ...       settings: param list | get <k> | set <k> <v> | reset <k>|all | revert | save | export\n"
        "  beep            test the buzzer\n"
        "  gps [raw on|off] GPS status; raw: echo the NMEA sentences\n"
        "  bench on|off    bench mode: enables the test commands below until reboot (not in flight)\n"
        "  reboot          restart (not in flight)\n"
        "  wifi join <ssid> <pass> | off   join the ground station WiFi to send logs (landed/bench)\n"
        " bench mode only:\n"
        "  sim             simulated flight: fake sensors, pyro output NOT driven\n"
        "  sim live        simulated flight that also pulses the pyro output\n"
        "  fire <ms> CONFIRM   pulse the pyro output now (igniter test, no charge!)");
  } else if (!strcmp(cmd, "status")) {
    print_status(now);
  } else if (!strcmp(cmd, "config")) {
    print_config();
  } else if (!strcmp(cmd, "gps")) {
    if (a1 && !strcmp(a1, "raw")) gps_raw_ = a2 && !strcmp(a2, "on");
    const fc::GpsFix& f = gps_.fix();
    con.printf("gps        mode %s, %s, %lu sentences, %lu rejected%s\n",
                  set_.gps_mode == 0 ? "off" : set_.gps_mode == 1 ? "auto" : "required",
                  gps_state_ == 2 ? "FIX" : gps_state_ == 1 ? "data, no fix" : "no data",
                  static_cast<unsigned long>(gps_.sentences()), static_cast<unsigned long>(gps_.checksum_errors()),
                  gps_raw_ ? ", raw echo on" : "");
    if (gps_.sentences())
      con.printf("           quality %u, %u sats, hdop %.2f, %.7f %.7f, %.1f m MSL, %.1f m/s\n", f.quality,
                    f.sats, static_cast<double>(f.hdop), f.lat_e7 / 1e7, f.lon_e7 / 1e7,
                    static_cast<double>(f.alt_msl_m), static_cast<double>(f.speed_mps));
  } else if (!strcmp(cmd, "param")) {
    params_.exec(argc - 1, argv + 1, safe_to_interrupt(), serial_line, store_text, &store_);
  } else if (!strcmp(cmd, "csv")) {
    csv_stream_ = a1 && !strcmp(a1, "on");
    if (csv_stream_) con.println(fc::csv::telemetry_header());
  } else if (!safe_to_interrupt()) {
    con.println("refused: in flight");
  } else if (!strcmp(cmd, "wifi")) {
    wifi_command(a1, a2, argv[3], now);
  } else if (!strcmp(cmd, "dump")) {
    dump_log(con);
  } else if (!strcmp(cmd, "flog")) {
    flog_command(a1, a2, now);
  } else if (!strcmp(cmd, "logclear")) {
    ram_log_.ring().clear();
    con.println("onboard log cleared");
  } else if (!strcmp(cmd, "time")) {
    // time <unix ms>: real time from the computer (the ground station UI sends it).
    const unsigned long long v = a1 ? strtoull(a1, nullptr, 10) : 0;
    if (clock_.synced()) {
      con.println("time already set for this boot (kept, so log timestamps never jump)");
    } else if (!fc::TimeSync::plausible(v)) {
      con.println("usage: time <unix time in ms>");
    } else {
      sync_time(v, now, "USB");
    }
  } else if (!strcmp(cmd, "i2c")) {
    i2c_scan();
  } else if (!strcmp(cmd, "reboot")) {
    con.println("rebooting");
    Serial.flush();
    hal::clear_flight_record();
    ESP.restart();
  } else if (!strcmp(cmd, "beep")) {
    beeper_.beeps(3);
  } else if (!strcmp(cmd, "bench")) {
    if (!a1 || (strcmp(a1, "on") && strcmp(a1, "off"))) {
      con.printf("bench mode is %s (usage: bench on|off)\n", bench_ ? "ON" : "off");
    } else if (!strcmp(a1, "off") && simulating()) {
      con.println("a simulation is running: reboot to end it");
    } else {
      set_bench(!strcmp(a1, "on"), !strcmp(a1, "on") ? 1 : 0, now, "USB");
    }
  } else if ((!strcmp(cmd, "sim") || !strcmp(cmd, "fire")) && !bench_) {
    con.printf("refused: '%s' needs bench mode ('bench on')\n", cmd);
  } else if (!strcmp(cmd, "sim")) {
    start_sim(a1 && !strcmp(a1, "live"), now);
  } else if (!strcmp(cmd, "fire")) {
    const long ms = a1 ? strtol(a1, nullptr, 10) : 0;
    if (ms <= 0 || ms > static_cast<long>(hal::PyroChannel::kMaxPulseMs) || !a2 ||
        strcmp(a2, "CONFIRM") != 0) {
      con.printf("usage: fire <1..%lu ms> CONFIRM\n",
                    static_cast<unsigned long>(hal::PyroChannel::kMaxPulseMs));
    } else {
      start_sag();
      if (pyro_.bench_fire(now, static_cast<uint32_t>(ms))) {
        event(fc::EventCode::BenchFire, ms, now);
        watch_open_ = continuity_;
      } else {
        sag_active_ = false;
      }
    }
  } else {
    con.printf("unknown command '%s' (type 'help')\n", cmd);
  }
}

void App::print_status(uint32_t now) {
  const fc::Estimator& est = fc_.estimator();
  con.printf("firmware   %s, reset: %s%s\n", FC_GIT_REV, hal::reset_reason_name(),
                resumed_ ? " (flight RESUMED)" : "");
  con.printf("mode       %s\n", bench_ ? "BENCH (test commands enabled; not for flight)" : "flight");
  con.printf("man. eject %s\n", set_.manual_eject ? "ENABLED: the ground station can fire the charge in flight"
                                                     : "off (manual_eject = 0)");
  con.printf("settings   crc %08lX%s\n", static_cast<unsigned long>(set_crc_),
                config_ok_ ? "" : "  INVALID (running on defaults, will not fly): see 'param'");
  {
    char utc[40];
    format_utc(now, utc, sizeof(utc));
    con.printf("time       %s\n", utc);
  }
  con.printf("state      %s  uptime %lu s  fault %u  warnings 0x%02X%s\n",
                to_string(fc_.state()), static_cast<unsigned long>(now / 1000),
                static_cast<unsigned>(fault_), warnings_, simulating() ? "  [SIMULATION]" : "");
  con.printf("estimate   alt %.2f m  vel %.2f m/s  max %.2f m  accel_up %.3f g\n",
                static_cast<double>(est.alt_m()), static_cast<double>(est.vel_mps()),
                static_cast<double>(fc_.sm().max_alt_m()), static_cast<double>(est.accel_up_g()));
  con.printf("baro       %s  %.1f Pa  %.2f C  ground %.1f Pa  errors %lu\n",
                baro_ok_ ? "ok" : "--", static_cast<double>(pressure_pa_),
                static_cast<double>(temp_c_), static_cast<double>(est.ground_pa()),
                static_cast<unsigned long>(baro_.errors()));
  con.printf("imu        %s %s  accel %.3f %.3f %.3f g  gyro %.1f %.1f %.1f dps  errors %lu\n",
                imu_.chip_name(), imu_ok_ ? "ok" : "--", static_cast<double>(imu_s_.accel_g.x),
                static_cast<double>(imu_s_.accel_g.y), static_cast<double>(imu_s_.accel_g.z),
                static_cast<double>(imu_s_.gyro_dps.x), static_cast<double>(imu_s_.gyro_dps.y),
                static_cast<double>(imu_s_.gyro_dps.z), static_cast<unsigned long>(imu_.errors()));
  con.printf("attitude   tilt %.1f deg from vertical%s\n",
                static_cast<double>(fc::tilt_deg(ahrs_.q())), imu_ok_ ? "" : " (no IMU)");
  con.printf("mag        %s  %.1f %.1f %.1f uT\n", imu_.has_mag() ? "ok" : "--",
                static_cast<double>(imu_s_.mag_uT.x), static_cast<double>(imu_s_.mag_uT.y),
                static_cast<double>(imu_s_.mag_uT.z));
  if (have_battery_) {
    con.printf("battery    %.3f V  ~%.0f %%  %s\n", static_cast<double>(vbat_),
                  static_cast<double>(fc::liion_percent(vbat_)), level_name(batt_.level()));
  } else {
    con.printf("battery    none (%.2f V): running from USB\n", static_cast<double>(vbat_));
  }
  con.printf("pyro       drain %.2f V  continuity %s  armed %s  fired %s%s\n",
                static_cast<double>(vpyro_), continuity_ ? "YES" : "no",
                pyro_.armed() ? "YES" : "no", (fc_.sm().fired() || pyro_.fired()) ? "YES" : "no",
                pyro_.firing() ? "  FIRING" : "");
  con.printf("radio      %s  tx %lu  errors %lu  denied %lu  budget %.1f s  queue %u  "
                "dropped %lu\n",
                radio_.ok() ? "ok" : "FAIL", static_cast<unsigned long>(radio_.tx_count()),
                static_cast<unsigned long>(radio_.tx_errors()),
                static_cast<unsigned long>(tx_denied_),
                static_cast<double>(duty_.available_us(now)) / 1e6,
                static_cast<unsigned>(evq_count_), static_cast<unsigned long>(ev_dropped_));
  if (ground_.connected(now)) {
    con.printf("ground     heard, uplink rssi %d dBm, pad beeps every %u s%s\n",
                  ground_.rssi_dbm(now), ground_.pad_beep_period_s(now),
                  ground_.pad_beep_period_s(now) ? "" : " (off)");
  } else {
    con.println("ground     not heard: pad status beeps off");
  }
  if (gps_uart_.started()) {
    const fc::GpsFix& f = gps_.fix();
    con.printf("gps        %s  %u sats  hdop %.2f  %.7f %.7f  %.1f m MSL\n",
                  gps_state_ == 2 ? "FIX" : gps_state_ == 1 ? "no fix" : "no data", f.sats,
                  static_cast<double>(f.hdop), f.lat_e7 / 1e7, f.lon_e7 / 1e7, static_cast<double>(f.alt_msl_m));
  } else {
    con.println("gps        off (gps_mode)");
  }
  if (flog_ok_) {
    con.printf("flash log  session %lu  %lu KB written this boot  backlog %u frames  lost %lu  errors %lu\n",
                  static_cast<unsigned long>(flog_.log().session()),
                  static_cast<unsigned long>(flog_.log().bytes_written() / 1024), static_cast<unsigned>(flog_.backlog()),
                  static_cast<unsigned long>(flog_.dropped()), static_cast<unsigned long>(flog_.log().errors()));
  } else {
    con.printf("flash log  %s\n", set_.flog_on ? "NOT AVAILABLE" : "off (flog_on)");
  }
  con.printf("log        %s  %u / %u frames  overwritten %lu\n", ram_log_.name(),
                static_cast<unsigned>(ram_log_.ring().count()),
                static_cast<unsigned>(ram_log_.ring().capacity()),
                static_cast<unsigned long>(ram_log_.ring().overwritten()));
  con.printf("timing     longest control tick %lu us (budget %lu us)\n",
                static_cast<unsigned long>(loop_max_us_),
                static_cast<unsigned long>(cfg::kLoopMs * 1000));
  if (sag_n_ > 0)
    con.printf("last pulse battery %.3f V before, min %.3f V\n", static_cast<double>(sag_pre_v_),
                  static_cast<double>(sag_min_v_));
}

// Human-readable list of the settings in use (since boot). The "param"
// command shows the edited / saved values as well.
void App::print_config() {
  const fc::Settings defaults;
  const char* group = "";
  for (size_t i = 0; i < fc::param_count(); ++i) {
    const fc::ParamDef& d = fc::param_def(i);
    if (strcmp(group, d.group) != 0) {
      group = d.group;
      con.printf("[%s]\n", group);
    }
    char v[32];
    fc::format_param(set_, d, v, sizeof(v));
    const bool changed = !fc::param_equal(set_, defaults, d);
    char dv[32] = "";
    if (changed) fc::format_param(defaults, d, dv, sizeof(dv));
    con.printf("  %-18s %s %s%s%s%s\n", d.key, v, d.unit, changed ? "   (default " : "", dv,
                  changed ? ")" : "");
    if ((i & 15) == 0) hal::watchdog_feed();
  }
  con.printf("settings crc %08lX%s\n", static_cast<unsigned long>(set_crc_),
                config_ok_ ? "" : "  INVALID: running on defaults");
  con.printf("airtime            telemetry %lu us, event %lu us (RadioLib: %lu us), uplink %lu ms; "
                "listen window %lu ms\n",
                static_cast<unsigned long>(airtime_tlm_us_), static_cast<unsigned long>(airtime_evt_us_),
                static_cast<unsigned long>(radio_.time_on_air_us(fc::tlm::kTelemetryLen)),
                static_cast<unsigned long>(airtime_up_ms_), static_cast<unsigned long>(rx_window_ms_));
}

void App::print_csv_line(uint32_t now) {
  if (now - t_csv_ < 100) return;
  t_csv_ = now;
  char line[fc::csv::kLineMax];
  if (fc::csv::format(snapshot(now), line, sizeof(line))) con.println(line);
}

void App::i2c_scan() {
  con.print("I2C devices:");
  int found = 0;
  for (uint8_t a = 1; a < 127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      con.printf(" 0x%02X", a);
      ++found;
    }
    hal::watchdog_feed();
  }
  con.println(found ? "" : " none");
  con.println("  expected: 0x68 (MPU9250), 0x76 or 0x77 (BMP280); 0x0C (AK8963) after init");
}

void App::dump_log(Print& o) {
  const fc::FrameRing& r = ram_log_.ring();
  o.printf("# onboard log: %u frames, %lu overwritten, firmware %s, settings crc %08lX\n",
                static_cast<unsigned>(r.count()), static_cast<unsigned long>(r.overwritten()), FC_GIT_REV,
                static_cast<unsigned long>(set_crc_));
  // Real time of t_ms = 0 (also in the TIME_SYNC event, which an old log may
  // have overwritten): utc_ms = utc0 + t_ms.
  if (clock_.synced()) {
    o.printf("# clock utc0_ms %llu\n", static_cast<unsigned long long>(clock_.utc_ms(0)));
  } else {
    o.println("# clock not synced (no ground station time this boot)");
  }
  o.println(fc::csv::telemetry_header());
  o.println(fc::csv::event_header());
  o.println(fc::csv::gps_header());
  uint8_t frame[fc::FrameRing::kSlot];
  char line[fc::csv::kLineMax];
  for (size_t i = 0; i < r.count(); ++i) {
    const size_t n = r.get(i, frame, sizeof(frame));
    if (fc::csv::format_frame(frame, n, line, sizeof(line))) o.println(line);
    if ((i & 31) == 0) {
      hal::watchdog_feed();
      pyro_.update(millis());
    }
  }
  o.println("# end of log");
}

namespace {
struct DumpCtx {
  uint32_t session = 0;
  uint32_t n = 0;
  App* app = nullptr;
  Print* o = nullptr;
};
}  // namespace

// Flash log: list sessions, print one (or all) as CSV, erase.
void App::flog_command(const char* sub, const char* arg, uint32_t now) {
  (void)now;
  if (!flog_ok_) {
    con.println(set_.flog_on ? "flash log not available (partition missing?)" : "flash log off (setting flog_on)");
    return;
  }
  fc::FlashLog& log = flog_.log();
  if (!sub || !strcmp(sub, "list")) {
    flog_list(con);
  } else if (!strcmp(sub, "dump")) {
    const bool all = arg && !strcmp(arg, "all");
    const uint32_t session = all ? 0 : (arg ? strtoul(arg, nullptr, 10) : 0);
    if (!all && session == 0) {
      con.println("usage: flog dump <session number>|all   (see 'flog list')");
      return;
    }
    flog_dump(con, session);
  } else if (!strcmp(sub, "erase")) {
    if (!arg || strcmp(arg, "CONFIRM") != 0) {
      con.println("usage: flog erase CONFIRM   (erases every stored session)");
      return;
    }
    con.println("erasing the flash log (about a minute)...");
    log.erase_all([](void* c) {
      hal::watchdog_feed();
      static_cast<App*>(c)->pyro_tick();
    }, this);
    con.println("flash log erased");
  } else {
    con.println("usage: flog [list] | flog dump <n>|all | flog erase CONFIRM");
  }
}

void App::start_sim(bool live, uint32_t now) {
  if (sim_) {
    con.println("a simulation already ran: 'reboot' first");
    return;
  }
  fc::SimParams p;
  p.ignition_ms = set_.settle_ms + 5000;
  p.seed = now | 1u;
  sim_ = new fc::SimFlight(p);
  sim_last_ms_ = now;
  sim_live_ = live;
  sim_reported_ = false;

  // Fresh flight computer and a fresh (bench-only) pyro shot.
  hal::clear_flight_record();
  fc_ = fc::FlightComputer(fc::flight_computer_config(set_));
  pyro_.begin(pins::kPyroGate);
  resumed_ = false;
  lost_reported_ = 0;
  baro_good_ = 0;
  beeper_.clear();
  start_countdown(now);
  con.printf("[sim] simulated flight started (%s): ignition in %lu s. Sensors are FAKE "
                "until reboot.\n",
                live ? "LIVE: pyro output WILL pulse" : "pyro output not driven",
                static_cast<unsigned long>(p.ignition_ms / 1000));
}

void App::flog_list(Print& o) {
  if (!flog_ok_) {
    o.println("# flash log not available");
    return;
  }
  fc::FlashLog& log = flog_.log();
  flash_log_drain();
  static fc::FlashLog::Session s[64];
  const size_t n = log.list(s, 64);
  o.printf("# flash log: %u KB, this boot is session %lu, %u session(s) stored, errors %lu\n",
           static_cast<unsigned>(log.sectors() * 4), static_cast<unsigned long>(log.session()),
           static_cast<unsigned>(n), static_cast<unsigned long>(log.errors()));
  for (size_t i = 0; i < (n < 64 ? n : 64); ++i) {
    o.printf("FLOG,%lu,%lu,%lu,%lu,%lu,%lu\n", static_cast<unsigned long>(s[i].session),
             static_cast<unsigned long>(s[i].sectors), static_cast<unsigned long>(s[i].frames),
             static_cast<unsigned long>(s[i].first_t_ms), static_cast<unsigned long>(s[i].last_t_ms),
             static_cast<unsigned long>(s[i].bytes));
  }
  if (n > 64) o.println("# (only the oldest 64 sessions listed)");
  o.println("# end of flash log list");
}

// Prints flash log `session` as CSV (0 = all sessions).
void App::flog_dump(Print& o, uint32_t session) {
  if (!flog_ok_) {
    o.println("# flash log not available");
    return;
  }
  fc::FlashLog& log = flog_.log();
  flash_log_drain();
  if (session == 0) {
    o.printf("# onboard log: flash, all sessions, firmware %s\n", FC_GIT_REV);
  } else {
    o.printf("# onboard log: flash session %lu, firmware %s\n", static_cast<unsigned long>(session), FC_GIT_REV);
  }
  o.println(fc::csv::telemetry_header());
  o.println(fc::csv::event_header());
  o.println(fc::csv::gps_header());
  DumpCtx ctx;
  ctx.app = this;
  ctx.o = &o;
  log.read(session, [](void* c, uint32_t sess, const uint8_t* fr, size_t len) {
    DumpCtx* d = static_cast<DumpCtx*>(c);
    if (sess != d->session) {
      d->session = sess;
      d->o->printf("# session %lu\n", static_cast<unsigned long>(sess));
    }
    char line[fc::csv::kLineMax];
    if (fc::csv::format_frame(fr, len, line, sizeof(line))) d->o->println(line);
    if ((++d->n & 31) == 0) {
      hal::watchdog_feed();
      d->app->pyro_tick();
    }
    return true;
  }, &ctx);
  o.println("# end of log");
}
