// Ground station firmware (second XIAO ESP32-S3 + Wio-SX1262 kit on USB).
//
// Receives the flight computer's LoRa frames and prints one CSV line per
// frame on USB serial (format: lib/fc_core/src/fc/csv.h) with the radio
// signal quality appended:   ...,rssi_dbm,snr_db
// Lines starting with '#' are comments/diagnostics, including
//   # noise <dBm>                 channel noise floor, every 2 s
//   # uplink on|off beep=<s> ...  heartbeat settings and count, on change and every 10 s
//   # stats: ...                  packet counters, every 10 s
//
// Uplink: while the rocket is on the ground, each received rocket frame is
// answered (at most about once per second) with a heartbeat that tells the
// rocket the pad beep period. See fc/uplink.h. Nothing else is ever sent.
//
// USB commands (one per line):
//   beep <0..255>   pad beep period in seconds (0 = rocket stays silent on the pad)
//   uplink on|off   send heartbeats or not
//   time <unix ms>  real time from the computer, passed to the rocket in heartbeats
//   rf arm <ms>     remote test fire, step 1 (rocket must run the BENCH build)
//   rf fire         step 2, after the rocket reported REMOTE_ARMED
//   rf disarm       cancel
//   bench on|off    switch the rocket's bench mode by radio (it answers BENCH_MODE)
//   param ...       radio settings of this kit (same as the flight computer's
//                   "param", radio group only); they apply after "reboot"
//   reboot          restart this kit
//   (empty line)    re-print the CSV headers
//
// tools/ground_station.html shows and controls all of this.
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "board_pins.h"
#include "fc/csv.h"
#include "fc/param_shell.h"
#include "fc/radio_budget.h"
#include "fc/remote_fire.h"
#include "fc/settings.h"
#include "fc/telemetry.h"
#include "fc/uplink.h"
#include "hal/radio_link.h"
#include "hal/settings_store.h"
#include "radio_config.h"

namespace {

// Delay between receiving a rocket frame and replying, so the rocket has
// certainly switched back to receive (it does so right after transmitting).
constexpr uint32_t kReplyDelayMs = 15;

hal::RadioLink radio;
fc::UplinkPolicy policy;
// Radio settings (fc/settings.h, kParamShared): loaded at boot from flash.
fc::Settings set;
fc::ParamShell params(true);
hal::SettingsStore store;
bool settings_ok = true;
char settings_problem[160] = {0};
fc::DutyCycleLimiter duty(0.08f, 72000);
uint32_t uplink_airtime_us = 0;
uint8_t beep_period_s = 10;
uint16_t uplink_seq = 0;
uint32_t uplink_sent = 0;
bool reply_pending = false;
int64_t utc_base_ms = 0;  // real time at millis() == 0; 0 = not set

// Remote test fire: a command waits for the next rocket frame (the rocket
// only listens right after transmitting), then replaces that heartbeat.
constexpr uint32_t kRfCommandTimeoutMs = 5000;
bool rf_pending = false;
fc::tlm::UplinkKind rf_kind = fc::tlm::UplinkKind::FireDisarm;
uint16_t rf_pulse_ms = 0;
uint32_t rf_since = 0;
bool rf_have_nonce = false;
uint32_t rf_nonce = 0;
uint32_t rf_nonce_at = 0;
bool reply_is_rf = false;
uint32_t reply_due = 0;

uint32_t n_ok = 0, n_bad = 0, t_stats = 0, t_noise = 0;
float noise_sum = 0;
int noise_n = 0;
float last_rssi = 0, last_snr = 0;
char cmd[112];
size_t cmd_len = 0;

void print_headers() {
  Serial.printf("# rocket-fc ground station %s  %.3f MHz SF%lu BW%.1f sync 0x%02lX %ld dBm  settings crc %08lX\n",
                FC_GIT_REV, static_cast<double>(set.radio_freq_mhz), static_cast<unsigned long>(set.radio_sf),
                static_cast<double>(fc::radio_bw_khz(set)), static_cast<unsigned long>(set.radio_sync),
                static_cast<long>(set.radio_power_dbm), static_cast<unsigned long>(fc::settings_crc(set)));
  if (!settings_ok) Serial.printf("# ERROR: radio settings invalid (%s): running on the defaults\n", settings_problem);
  Serial.printf("%s,rssi_dbm,snr_db\n", fc::csv::telemetry_header());
  Serial.printf("%s,rssi_dbm,snr_db\n", fc::csv::event_header());
}

uint64_t utc_now_ms() {
  return utc_base_ms ? static_cast<uint64_t>(utc_base_ms + static_cast<int64_t>(millis())) : 0;
}

void print_uplink() {
  Serial.printf("# uplink %s beep=%u sent=%lu time=%s\n", policy.enabled() ? "on" : "off",
                beep_period_s, static_cast<unsigned long>(uplink_sent),
                utc_base_ms ? "set" : "unset");
}

void serial_line(void*, const char* line) { Serial.println(line); }
bool store_text(void* ctx, const char* text, size_t len) {
  return static_cast<hal::SettingsStore*>(ctx)->save(text, len);
}

void load_settings() {
  static char text[hal::SettingsStore::kMaxText];
  bool read_ok = true;
  const size_t n = store.load(text, sizeof(text), &read_ok);
  fc::Settings stored;
  fc::LoadReport rep;
  if (n) fc::settings_from_text(stored, text, n, &rep);
  settings_ok = true;
  if (!read_ok) {
    settings_ok = false;
    snprintf(settings_problem, sizeof(settings_problem), "flash read error");
  } else if (rep.errors) {
    settings_ok = false;
    snprintf(settings_problem, sizeof(settings_problem), "%s", rep.first_problem);
  } else if (!fc::validate_radio(stored, settings_problem, sizeof(settings_problem))) {
    settings_ok = false;
  }
  set = settings_ok ? stored : fc::Settings{};
  params.init(set, stored);
  duty = fc::DutyCycleLimiter(set.radio_duty_pct / 100.0f, set.radio_burst_s * 1000u);
}

void exec(char* line) {
  char* save = nullptr;
  const char* argv[6] = {};
  int argc = 0;
  for (char* t = strtok_r(line, " \t", &save); t && argc < 6; t = strtok_r(nullptr, " \t", &save)) argv[argc++] = t;
  const char* c = argv[0];
  const char* a = argv[1];
  if (!c) {
    print_headers();
  } else if (!strcmp(c, "param")) {
    params.exec(argc - 1, argv + 1, true, serial_line, store_text, &store);
  } else if (!strcmp(c, "reboot")) {
    Serial.println("# rebooting");
    Serial.flush();
    ESP.restart();
  } else if (!strcmp(c, "bench") && a && (!strcmp(a, "on") || !strcmp(a, "off"))) {
    rf_since = millis();
    rf_kind = fc::tlm::UplinkKind::BenchMode;
    rf_pulse_ms = !strcmp(a, "on") ? 1 : 0;
    rf_pending = true;
    Serial.printf("# rf queued bench %s\n", a);
  } else if (!strcmp(c, "beep") && a) {
    const long v = strtol(a, nullptr, 10);
    beep_period_s = static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
    print_uplink();
  } else if (!strcmp(c, "time") && a) {
    const unsigned long long v = strtoull(a, nullptr, 10);
    if (v > 1577836800000ull) {  // after 2020-01-01
      utc_base_ms = static_cast<int64_t>(v) - static_cast<int64_t>(millis());
      print_uplink();
    }
  } else if (!strcmp(c, "rf") && a) {
    const char* b = argv[2];
    rf_since = millis();
    if (!strcmp(a, "arm") && b) {
      const long ms = strtol(b, nullptr, 10);
      if (ms < 1 || ms > fc::RemoteFire::kMaxPulseMs) {
        Serial.println("# rf error: pulse must be 1..2000 ms");
        return;
      }
      rf_pulse_ms = static_cast<uint16_t>(ms);
      rf_kind = fc::tlm::UplinkKind::FireArm;
      rf_have_nonce = false;
      rf_pending = true;
      Serial.printf("# rf queued arm %u ms\n", rf_pulse_ms);
    } else if (!strcmp(a, "fire")) {
      if (!rf_have_nonce || millis() - rf_nonce_at >= fc::RemoteFire::kWindowMs) {
        Serial.println("# rf error: rocket is not armed (no REMOTE_ARMED received)");
        return;
      }
      rf_kind = fc::tlm::UplinkKind::Fire;
      rf_pending = true;
      Serial.println("# rf queued fire");
    } else if (!strcmp(a, "disarm")) {
      rf_kind = fc::tlm::UplinkKind::FireDisarm;
      rf_have_nonce = false;
      rf_pending = true;
      Serial.println("# rf queued disarm");
    }
  } else if (!strcmp(c, "uplink") && a) {
    policy.set_enabled(!strcmp(a, "on"));
    print_uplink();
  } else {
    Serial.println("# commands: beep <0..255 s>, uplink on|off, time <unix ms>, rf arm <ms>|fire|disarm, "
                   "bench on|off, param ..., reboot, empty line = headers");
  }
}

void send_rf(uint32_t now) {
  if (!duty.try_consume(now, uplink_airtime_us)) {
    Serial.println("# rf error: radio duty-cycle budget exhausted, try again later");
    return;
  }
  fc::tlm::Uplink u;
  u.seq = uplink_seq++;
  u.t_ms = now;
  u.kind = rf_kind;
  u.pulse_ms = rf_pulse_ms;
  if (rf_kind == fc::tlm::UplinkKind::Fire) {
    u.code = fc::remote_fire_code(rf_nonce, set.rf_key);
    rf_have_nonce = false;  // one use
  } else if (rf_kind == fc::tlm::UplinkKind::BenchMode) {
    u.code = fc::bench_mode_code(rf_pulse_ms != 0, set.rf_key);
  }
  uint8_t buf[fc::tlm::kUplinkLen];
  const size_t n = fc::tlm::encode(u, buf, sizeof(buf));
  const char* what = rf_kind == fc::tlm::UplinkKind::FireArm     ? "arm"
                     : rf_kind == fc::tlm::UplinkKind::Fire      ? "fire"
                     : rf_kind == fc::tlm::UplinkKind::BenchMode ? (rf_pulse_ms ? "bench on" : "bench off")
                                                                 : "disarm";
  if (radio.send(buf, n)) {
    ++uplink_sent;
    Serial.printf("# rf sent %s\n", what);
  } else {
    Serial.printf("# rf error: %s not sent (radio busy)\n", what);
  }
}

void send_heartbeat(uint32_t now) {
  if (!duty.try_consume(now, uplink_airtime_us)) return;
  fc::tlm::Uplink u;
  u.seq = uplink_seq++;
  u.t_ms = now;
  u.beep_period_s = beep_period_s;
  u.utc_ms = utc_now_ms();
  uint8_t buf[fc::tlm::kUplinkLen];
  const size_t n = fc::tlm::encode(u, buf, sizeof(buf));
  if (radio.send(buf, n)) ++uplink_sent;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(20);
  // Note: GPIO21 is the Wio-SX1262 button and the XIAO LED: never drive it.

  const hal::RadioLink::Pins p{pins::kLoraSck,  pins::kLoraMiso,  pins::kLoraMosi,
                               pins::kLoraNss,  pins::kLoraDio1,  pins::kLoraReset,
                               pins::kLoraBusy, pins::kLoraRfSw};
  load_settings();
  const hal::RadioLink::Params r{set.radio_freq_mhz,
                                 fc::radio_bw_khz(set),
                                 static_cast<uint8_t>(set.radio_sf),
                                 static_cast<uint8_t>(set.radio_cr),
                                 static_cast<uint8_t>(set.radio_sync),
                                 static_cast<int8_t>(set.radio_power_dbm),
                                 static_cast<uint16_t>(set.radio_preamble),
                                 radio_cfg::kTcxoVolts,
                                 set.radio_rx_boost};
  delay(1500);  // give the USB host time to open the port and see the header
  print_headers();
  uplink_airtime_us = fc::lora_airtime_us(fc::lora_modulation(set), fc::tlm::kUplinkLen);
  duty.reset(millis());
  if (!radio.begin(p, r) || !radio.start_receive()) {
    Serial.printf("# ERROR: radio init failed (RadioLib error %d)\n", radio.last_error());
  } else {
    radio.set_listen(true);
    Serial.println("# listening");
  }
  print_uplink();
}

void loop() {
  radio.poll();
  const uint32_t now = millis();

  if (reply_pending && !radio.busy() && now - reply_due < 0x80000000u) {
    reply_pending = false;
    if (reply_is_rf) {
      send_rf(now);
    } else {
      send_heartbeat(now);
    }
  }
  if (rf_pending && now - rf_since > kRfCommandTimeoutMs) {
    rf_pending = false;
    Serial.println("# rf error: no rocket frame heard within 5 s, command dropped");
  }

  uint8_t buf[64];
  float rssi = 0, snr = 0;
  bool crc_error = false;
  const size_t n = radio.receive(buf, sizeof(buf), &rssi, &snr, &crc_error);
  if (n > 0) {
    last_rssi = rssi;
    last_snr = snr;
    char line[fc::csv::kLineMax];
    if (!crc_error && fc::csv::format_frame(buf, n, line, sizeof(line))) {
      ++n_ok;
      // Byte 3 of every rocket frame is its flight state.
      const fc::FlightState rocket_state = static_cast<fc::FlightState>(buf[3]);
      fc::tlm::Event ev;
      if (fc::tlm::decode(buf, n, &ev) == fc::tlm::DecodeResult::Ok &&
          ev.code == fc::EventCode::RemoteArmed) {
        rf_nonce = static_cast<uint32_t>(ev.arg);
        rf_nonce_at = now;
        rf_have_nonce = true;
        Serial.println("# rf rocket armed: send 'rf fire' within 10 s");
      }
      if (rf_pending && !reply_pending) {
        rf_pending = false;
        const bool flying = rocket_state == fc::FlightState::Boost ||
                            rocket_state == fc::FlightState::Coast ||
                            rocket_state == fc::FlightState::Descent;
        const bool any_ground_state = rf_kind == fc::tlm::UplinkKind::FireDisarm ||
                                      rf_kind == fc::tlm::UplinkKind::BenchMode;
        if (rocket_state == fc::FlightState::Pad || (any_ground_state && !flying)) {
          reply_pending = true;
          reply_is_rf = true;
          reply_due = now + kReplyDelayMs;
        } else {
          Serial.printf("# rf error: rocket is in %s: command dropped\n",
                        fc::to_string(rocket_state));
        }
      } else if (policy.on_rocket_frame(rocket_state, now)) {
        reply_pending = true;
        reply_is_rf = false;
        reply_due = now + kReplyDelayMs;
      }
      Serial.printf("%s,%.1f,%.2f\n", line, static_cast<double>(rssi), static_cast<double>(snr));
    } else {
      ++n_bad;
      Serial.printf("# bad frame: %u bytes, %s, rssi %.1f snr %.2f\n", static_cast<unsigned>(n),
                    crc_error ? "LoRa CRC error" : "not a rocket-fc frame",
                    static_cast<double>(rssi), static_cast<double>(snr));
    }
  }

  // Noise floor: average of instantaneous channel RSSI samples, every 2 s.
  if (now - t_noise >= 100) {
    t_noise = now;
    const float r = radio.channel_rssi_dbm();
    if (!isnan(r)) {
      noise_sum += r;
      ++noise_n;
    }
    if (noise_n >= 20) {
      Serial.printf("# noise %.1f\n", static_cast<double>(noise_sum / noise_n));
      noise_sum = 0;
      noise_n = 0;
    }
  }
  if (now - t_stats >= 10000) {
    t_stats = now;
    Serial.printf("# stats: %lu ok, %lu bad, last rssi %.1f dBm snr %.2f dB\n",
                  static_cast<unsigned long>(n_ok), static_cast<unsigned long>(n_bad),
                  static_cast<double>(last_rssi), static_cast<double>(last_snr));
    print_uplink();
  }

  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      cmd[cmd_len] = '\0';
      cmd_len = 0;
      exec(cmd);
    } else if (cmd_len < sizeof(cmd) - 1) {
      cmd[cmd_len++] = static_cast<char>(c);
    }
  }
}
