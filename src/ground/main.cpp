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
// rocket the pad beep period. See fc/uplink.h. Commands (below) replace one
// heartbeat each.
//
// Panel (optional, pins::ground, docs/HARDWARE.md): push button (short press
// = ping, 1.5 s hold = search mode on/off, 3 s hold = manual ejection when
// enabled with "eject button"), buzzer, ON LED, "rocket connected" LED and
// battery sense. Status lines:
//   # panel search=on|off eject_button=<s left> buzzer=on|off
//   # battery <V> V <pct> % [LOW]   or   # battery none
//   # ping ...  /  # eject ...
//
// USB commands (one per line):
//   beep <0..255>   pad beep period in seconds (0 = rocket stays silent on the pad)
//   uplink on|off   send heartbeats or not
//   time <unix ms>  real time from the computer, passed to the rocket in heartbeats
//   rf arm <ms>     remote test fire, step 1 (rocket must run the BENCH build)
//   rf fire         step 2, after the rocket reported REMOTE_ARMED
//   rf disarm       cancel
//   bench on|off    switch the rocket's bench mode by radio (it answers BENCH_MODE)
//   ping            connectivity check: the rocket beeps and answers PONG; this
//                   station beeps 0.5 s after the answer arrives
//   search on|off   search mode: beeps faster the stronger the rocket's signal
//   eject [ms]      MANUAL EJECTION: arm + fire in one go (the rocket must have
//                   manual_eject = 1, or be in bench mode on the pad)
//   eject button    let a 3 s button hold send the ejection (for 60 s)
//   eject cancel    drop a queued ejection / the button enable
//   param ...       radio settings of this kit (same as the flight computer's
//                   "param", radio group only); they apply after "reboot"
//   reboot          restart this kit
//   wifi [on|off | ssid <name> | pass <8..63 chars> | channel <1..13>]
//                   WiFi access point serving the UI (src/ground/web.h); applies after reboot
//   (empty line)    re-print the CSV headers
//
// Everything printed goes to USB and, when WiFi is on, to every browser
// connected over WiFi; commands are accepted from both.
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
#include "fc/battery.h"
#include "fc/indicators.h"
#include "fc/settings.h"
#include "fc/station.h"
#include "fc/telemetry.h"
#include "fc/uplink.h"
#include "hal/io.h"
#include "hal/radio_link.h"
#include "hal/settings_store.h"
#include "radio_config.h"
#include "web.h"

namespace {

// Output to USB and to the WiFi browsers, line by line.
class Tee : public Print {
 public:
  size_t write(uint8_t c) override {
    Serial.write(c);
    if (c == '\n') {
      size_t n = len_;
      while (n && line_[n - 1] == '\r') --n;
      web::publish(line_, n);
      len_ = 0;
    } else if (len_ < sizeof(line_)) {
      line_[len_++] = static_cast<char>(c);
    }
    return 1;
  }
  size_t write(const uint8_t* b, size_t n) override {
    for (size_t i = 0; i < n; ++i) write(b[i]);
    return n;
  }

 private:
  char line_[256];
  size_t len_ = 0;
};
Tee out;
web::Config wifi;
bool wifi_up = false;

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

// --- Panel: button, buzzer, LEDs, battery ------------------------------------
using fc::tlm::UplinkKind;
fc::PanelButton button;
fc::BeepSequencer beeper;
hal::DigitalOut buzzer, led_power, led_link;
hal::AnalogIn batt_adc;
float vbat = 0;
bool radio_ok = false;
bool warn_sound = false;          // the beeper plays an ejection warning: always audible
bool search_on = false;
uint32_t t_search_beep = 0;
uint32_t eject_button_until = 0;  // 0 = button ejection disabled
constexpr uint32_t kEjectButtonWindowMs = 60000;
constexpr uint32_t kEjectHoldMs = 3000;
constexpr uint32_t kSearchHoldMs = 1500;
// Last good rocket frame (link LED, search mode, command checks).
bool have_frame = false;
uint32_t last_frame_ms = 0;
float frame_rssi = 0;
uint8_t rocket_link = 0;          // link bits of the last telemetry frame
// Ping (connectivity check).
uint16_t ping_id = 0;
bool ping_wait = false;
uint32_t ping_sent_ms = 0;        // 0 = queued, not sent yet
uint32_t pong_beep_at = 0;        // 0 = none
constexpr uint32_t kPingReplyMs = 3000;
constexpr uint32_t kPongBeepDelayMs = 500;
// Manual ejection.
enum class Eject : uint8_t { Idle, Arming, Firing };
Eject ej = Eject::Idle;
uint32_t ej_since = 0;

uint32_t n_ok = 0, n_bad = 0, t_stats = 0, t_noise = 0;
float noise_sum = 0;
int noise_n = 0;
float last_rssi = 0, last_snr = 0;
char cmd[112];
size_t cmd_len = 0;

// The password is printed only on request ("wifi"), so it does not end up in saved logs.
void print_wifi(bool with_pass = false) {
  if (!wifi.on) {
    out.println("# wifi off");
    return;
  }
  out.printf("# wifi %s ssid=%s%s%s channel=%u ip=192.168.4.1 url=http://rocketfc.local clients=%d\n",
             wifi_up ? "on" : "failed", wifi.ssid, with_pass ? " pass=" : "", with_pass ? wifi.pass : "",
             wifi.channel, web::clients());
}

void wifi_command(const char* a, const char* b) {
  web::Config c = wifi;
  const char* err = nullptr;
  if (!a) {
    print_wifi(true);
    return;
  } else if (!strcmp(a, "on") || !strcmp(a, "off")) {
    c.on = !strcmp(a, "on");
  } else if (!strcmp(a, "ssid")) {
    err = web::check_ssid(b);
    if (!err) strncpy(c.ssid, b, sizeof(c.ssid) - 1);
  } else if (!strcmp(a, "pass")) {
    err = web::check_pass(b);
    if (!err) strncpy(c.pass, b, sizeof(c.pass) - 1);
  } else if (!strcmp(a, "channel")) {
    const long ch = b ? strtol(b, nullptr, 10) : 0;
    if (ch < 1 || ch > 13) err = "channel must be 1..13";
    else c.channel = static_cast<uint8_t>(ch);
  } else {
    err = "usage: wifi [on|off | ssid <name> | pass <8..63 chars> | channel <1..13>]";
  }
  if (err) {
    out.printf("# wifi error: %s\n", err);
    return;
  }
  if (!web::save_config(c)) {
    out.println("# wifi error: could not save");
    return;
  }
  out.printf("# wifi saved (on=%u ssid=%s channel=%u): applies after 'reboot'\n", c.on ? 1u : 0u, c.ssid, c.channel);
}

void print_headers() {
  out.printf("# rocket-fc ground station %s  %.3f MHz SF%lu BW%.1f sync 0x%02lX %ld dBm  settings crc %08lX\n",
                FC_GIT_REV, static_cast<double>(set.radio_freq_mhz), static_cast<unsigned long>(set.radio_sf),
                static_cast<double>(fc::radio_bw_khz(set)), static_cast<unsigned long>(set.radio_sync),
                static_cast<long>(set.radio_power_dbm), static_cast<unsigned long>(fc::settings_crc(set)));
  if (!settings_ok) out.printf("# ERROR: radio settings invalid (%s): running on the defaults\n", settings_problem);
  out.printf("%s,rssi_dbm,snr_db\n", fc::csv::telemetry_header());
  out.printf("%s,rssi_dbm,snr_db\n", fc::csv::event_header());
  out.printf("%s,rssi_dbm,snr_db\n", fc::csv::gps_header());
  print_wifi();
}

uint64_t utc_now_ms() {
  return utc_base_ms ? static_cast<uint64_t>(utc_base_ms + static_cast<int64_t>(millis())) : 0;
}

void print_uplink() {
  out.printf("# uplink %s beep=%u sent=%lu time=%s\n", policy.enabled() ? "on" : "off",
                beep_period_s, static_cast<unsigned long>(uplink_sent),
                utc_base_ms ? "set" : "unset");
}

void serial_line(void*, const char* line) { out.println(line); }
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

bool eject_button_enabled(uint32_t now) {
  return eject_button_until != 0 && static_cast<int32_t>(eject_button_until - now) > 0;
}

void print_panel(uint32_t now) {
  const uint32_t left = eject_button_enabled(now) ? (eject_button_until - now + 999) / 1000 : 0;
  out.printf("# panel search=%s eject_button=%lu buzzer=%s\n", search_on ? "on" : "off",
             static_cast<unsigned long>(left), set.gs_buzzer_on ? "on" : "off");
}

void print_battery() {
  if (vbat < set.batt_present_v) {
    out.println("# battery none");
    return;
  }
  out.printf("# battery %.2f V %.0f %%%s\n", static_cast<double>(vbat),
             static_cast<double>(fc::liion_percent(vbat)), vbat < set.gs_batt_low_v ? " LOW" : "");
}

// Queues a command for the next rocket frame (one slot; the newest wins).
void queue_rf(UplinkKind kind, uint16_t pulse_ms, uint32_t now) {
  rf_kind = kind;
  rf_pulse_ms = pulse_ms;
  rf_since = now;
  rf_pending = true;
}

void ping_end(const char* why) {
  ping_wait = false;
  out.printf("# ping no reply id=%u (%s)\n", ping_id, why);
  if (!warn_sound) {
    beeper.clear();
    beeper.beeps(3, 60, 90, 0);  // three short: no answer
  }
}

void start_ping(uint32_t now, const char* source) {
  if (ej != Eject::Idle) {
    out.println("# ping error: a manual ejection is in progress");
    return;
  }
  ++ping_id;
  ping_wait = true;
  ping_sent_ms = 0;
  pong_beep_at = 0;
  queue_rf(UplinkKind::Ping, ping_id, now);
  out.printf("# ping queued id=%u (%s)\n", ping_id, source);
}

void eject_end(const char* what, uint32_t now) {
  ej = Eject::Idle;
  if (rf_pending && (rf_kind == UplinkKind::EjectArm || rf_kind == UplinkKind::Eject)) rf_pending = false;
  out.printf("# eject %s\n", what);
  beeper.clear();
  warn_sound = true;
  beeper.beeps(3, 60, 90, 0);
  print_panel(now);
}

void start_eject(uint16_t pulse_ms, uint32_t now, const char* source) {
  ping_wait = false;
  pong_beep_at = 0;
  ej = Eject::Arming;
  ej_since = now;
  queue_rf(UplinkKind::EjectArm, pulse_ms, now);
  out.printf("# eject queued: arming (%s, pulse %s)\n", source, pulse_ms ? "set" : "rocket's pyro_pulse_ms");
  // Warning: always audible.
  beeper.clear();
  warn_sound = true;
  beeper.beeps(4, 40, 40, 0);
}

void set_search(bool on, uint32_t now) {
  search_on = on;
  t_search_beep = now;
  if (!warn_sound) {
    beeper.clear();
    if (on) beeper.beeps(2, 60, 80, 300);
    else beeper.push(250, 0);
  }
  print_panel(now);
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
  } else if (!strcmp(c, "wifi")) {
    wifi_command(a, argv[2]);
  } else if (!strcmp(c, "reboot")) {
    out.println("# rebooting");
    Serial.flush();
    ESP.restart();
  } else if (!strcmp(c, "bench") && a && (!strcmp(a, "on") || !strcmp(a, "off"))) {
    rf_since = millis();
    rf_kind = fc::tlm::UplinkKind::BenchMode;
    rf_pulse_ms = !strcmp(a, "on") ? 1 : 0;
    rf_pending = true;
    out.printf("# rf queued bench %s\n", a);
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
        out.println("# rf error: pulse must be 1..2000 ms");
        return;
      }
      rf_pulse_ms = static_cast<uint16_t>(ms);
      rf_kind = fc::tlm::UplinkKind::FireArm;
      rf_have_nonce = false;
      rf_pending = true;
      out.printf("# rf queued arm %u ms\n", rf_pulse_ms);
    } else if (!strcmp(a, "fire")) {
      if (!rf_have_nonce || millis() - rf_nonce_at >= fc::RemoteFire::kWindowMs) {
        out.println("# rf error: rocket is not armed (no REMOTE_ARMED received)");
        return;
      }
      rf_kind = fc::tlm::UplinkKind::Fire;
      rf_pending = true;
      out.println("# rf queued fire");
    } else if (!strcmp(a, "disarm")) {
      rf_kind = fc::tlm::UplinkKind::FireDisarm;
      rf_have_nonce = false;
      rf_pending = true;
      out.println("# rf queued disarm");
    }
  } else if (!strcmp(c, "ping")) {
    start_ping(millis(), "command");
  } else if (!strcmp(c, "search") && a && (!strcmp(a, "on") || !strcmp(a, "off"))) {
    set_search(!strcmp(a, "on"), millis());
  } else if (!strcmp(c, "search")) {
    print_panel(millis());
  } else if (!strcmp(c, "eject")) {
    const uint32_t now = millis();
    if (a && !strcmp(a, "cancel")) {
      eject_button_until = 0;
      if (ej != Eject::Idle) eject_end("cancelled (the rocket disarms by itself within 10 s)", now);
      else out.println("# eject cancelled");
      print_panel(now);
    } else if (a && !strcmp(a, "button")) {
      eject_button_until = now + kEjectButtonWindowMs;
      if (!eject_button_until) eject_button_until = 1;
      out.printf("# eject button ENABLED for %lu s: hold the button %lu s to eject\n",
                 static_cast<unsigned long>(kEjectButtonWindowMs / 1000),
                 static_cast<unsigned long>(kEjectHoldMs / 1000));
      print_panel(now);
    } else {
      const long ms = a ? strtol(a, nullptr, 10) : 0;
      if (ms < 0 || ms > fc::RemoteFire::kMaxPulseMs) {
        out.println("# eject error: pulse must be 1..2000 ms (or none = the rocket's pyro_pulse_ms)");
        return;
      }
      start_eject(static_cast<uint16_t>(ms), now, "command");
    }
  } else if (!strcmp(c, "uplink") && a) {
    policy.set_enabled(!strcmp(a, "on"));
    print_uplink();
  } else {
    out.println("# commands: beep <0..255 s>, uplink on|off, time <unix ms>, rf arm <ms>|fire|disarm, "
                   "bench on|off, ping, search on|off, eject [ms]|button|cancel, param ..., wifi ..., "
                   "reboot, empty line = headers");
  }
}

void send_rf(uint32_t now) {
  const bool is_ping = rf_kind == UplinkKind::Ping;
  const bool is_eject = rf_kind == UplinkKind::EjectArm || rf_kind == UplinkKind::Eject;
  if (!duty.try_consume(now, uplink_airtime_us)) {
    if (is_ping) ping_end("radio duty-cycle budget exhausted");
    else if (is_eject) eject_end("FAILED: radio duty-cycle budget exhausted", now);
    else out.println("# rf error: radio duty-cycle budget exhausted, try again later");
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
  } else if (rf_kind == UplinkKind::Eject) {
    u.code = fc::remote_fire_code(rf_nonce, set.rf_key);
    rf_have_nonce = false;
  }
  uint8_t buf[fc::tlm::kUplinkLen];
  const size_t n = fc::tlm::encode(u, buf, sizeof(buf));
  const char* what = rf_kind == fc::tlm::UplinkKind::FireArm     ? "arm"
                     : rf_kind == fc::tlm::UplinkKind::Fire      ? "fire"
                     : rf_kind == fc::tlm::UplinkKind::BenchMode ? (rf_pulse_ms ? "bench on" : "bench off")
                                                                 : "disarm";
  const bool sent = radio.send(buf, n);
  if (sent) ++uplink_sent;
  if (is_ping) {
    if (sent) {
      ping_sent_ms = now ? now : 1;
      out.printf("# ping sent id=%u\n", ping_id);
    } else {
      ping_end("radio busy");
    }
  } else if (is_eject) {
    if (sent) out.printf("# eject sent %s\n", rf_kind == UplinkKind::EjectArm ? "arm" : "FIRE");
    else eject_end("FAILED: radio busy", now);
  } else if (sent) {
    out.printf("# rf sent %s\n", what);
  } else {
    out.printf("# rf error: %s not sent (radio busy)\n", what);
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

// Button, beeps, LEDs, battery. Runs every loop.
void update_panel(uint32_t now) {
  // Button.
  const bool eject_btn = eject_button_enabled(now);
  if (eject_button_until != 0 && !eject_btn) {
    eject_button_until = 0;
    out.println("# eject button disabled (60 s passed)");
    print_panel(now);
  }
  const auto act = button.update(digitalRead(pins::ground::kButton) == LOW, now,
                                 eject_btn ? kEjectHoldMs : kSearchHoldMs);
  if (act == fc::PanelButton::Action::Short) {
    start_ping(now, "button");
  } else if (act == fc::PanelButton::Action::Long) {
    if (eject_btn) {
      eject_button_until = 0;
      start_eject(0, now, "button");
      print_panel(now);
    } else {
      set_search(!search_on, now);
    }
  } else if (eject_btn && button.held_ms(now) >= fc::PanelButton::kShortMaxMs && !beeper.busy()) {
    warn_sound = true;  // hold-to-eject countdown chirps
    beeper.push(40, 160);
  }

  // Delayed answer beep of the connectivity check.
  if (pong_beep_at && static_cast<int32_t>(now - pong_beep_at) >= 0) {
    pong_beep_at = 0;
    if (!warn_sound) {
      beeper.clear();
      beeper.push(150, 0);
    }
  }

  // Search mode: beep rate from the last frame's signal strength.
  if (search_on && !beeper.busy() && !ping_wait && !pong_beep_at && ej == Eject::Idle) {
    if (have_frame && now - last_frame_ms < 8000) {
      if (now - t_search_beep >= fc::SearchBeep::interval_ms(frame_rssi, set.gs_search_lo_dbm,
                                                              set.gs_search_hi_dbm)) {
        t_search_beep = now;
        beeper.push(30, 0);
      }
    } else if (now - t_search_beep >= 3000) {  // nothing heard: two faint ticks
      t_search_beep = now;
      beeper.beeps(2, 15, 60, 0);
    }
  }

  const bool sound = beeper.update(now);
  if (!beeper.busy()) warn_sound = false;
  buzzer.set(sound && (set.gs_buzzer_on || warn_sound));

  // Battery: every 2 s (the divider is high impedance; a few samples average it).
  static uint32_t t_batt = 0;
  if (now - t_batt >= 2000) {
    t_batt = now;
    vbat = batt_adc.read_volts();
  }
  const bool batt_low = vbat >= set.batt_present_v && vbat < set.gs_batt_low_v;

  // LEDs. ON: steady; double blink = battery low; fast blink = radio failed.
  // CONNECTED: on while rocket frames arrive (< 3 s old), briefly off at each frame.
  bool p = true;
  bool l = have_frame && now - last_frame_ms < 3000 && now - last_frame_ms >= 80;
  if (!radio_ok) {
    p = (now / 100) % 2 == 0;
  } else if (batt_low) {
    const uint32_t ph = now % 2000;
    p = !(ph < 120 || (ph >= 240 && ph < 360));
  }
  if (ej != Eject::Idle) {  // ejection in progress: both blink fast together
    p = l = (now / 60) % 2 == 0;
  } else if (eject_btn) {   // button ejection enabled: alternate
    p = (now / 125) % 2 == 0;
    l = !p;
  }
  led_power.set(p);
  led_link.set(l);
}

}  // namespace

void setup() {
  // Buzzer (straight on the pin) off first.
  buzzer.begin(pins::ground::kBuzzer, true, true);
  led_power.begin(pins::ground::kLedPower);
  led_link.begin(pins::ground::kLedLink);
  led_power.set(true);
  pinMode(pins::ground::kButton, INPUT_PULLUP);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(20);
  // Note: GPIO21 is the Wio-SX1262 button and the XIAO LED: never drive it.

  const hal::RadioLink::Pins p{pins::kLoraSck,  pins::kLoraMiso,  pins::kLoraMosi,
                               pins::kLoraNss,  pins::kLoraDio1,  pins::kLoraReset,
                               pins::kLoraBusy, pins::kLoraRfSw};
  load_settings();
  batt_adc.begin(pins::ground::kBattSense, set.gs_batt_divider);
  vbat = batt_adc.read_volts();
  wifi = web::load_config();
  wifi_up = web::begin(wifi);
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
    out.printf("# ERROR: radio init failed (RadioLib error %d)\n", radio.last_error());
    beeper.beeps(5, 60, 60, 0);
  } else {
    radio_ok = true;
    radio.set_listen(true);
    out.println("# listening");
    beeper.push(80, 0);  // powered up
  }
  print_uplink();
  print_panel(millis());
  print_battery();
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
    if (rf_kind == UplinkKind::Ping) ping_end("no rocket frame heard within 5 s");
    else if (rf_kind == UplinkKind::EjectArm || rf_kind == UplinkKind::Eject)
      eject_end("FAILED: no rocket frame heard within 5 s", now);
    else out.println("# rf error: no rocket frame heard within 5 s, command dropped");
  }
  if (ping_wait && ping_sent_ms && now - ping_sent_ms > kPingReplyMs) ping_end("no answer within 3 s");
  if (ej != Eject::Idle && now - ej_since > fc::RemoteFire::kWindowMs + 2000)
    eject_end("FAILED: no answer from the rocket", now);

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
      have_frame = true;
      last_frame_ms = now;
      frame_rssi = rssi;
      fc::tlm::Telemetry tf;
      if (fc::tlm::decode(buf, n, &tf) == fc::tlm::DecodeResult::Ok) rocket_link = tf.link;
      fc::tlm::Event ev;
      const bool is_event = fc::tlm::decode(buf, n, &ev) == fc::tlm::DecodeResult::Ok;
      if (is_event && ev.code == fc::EventCode::RemoteArmed) {
        rf_nonce = static_cast<uint32_t>(ev.arg);
        rf_nonce_at = now;
        rf_have_nonce = true;
        out.println("# rf rocket armed: send 'rf fire' within 10 s");
      }
      if (is_event && ev.code == fc::EventCode::Pong && ping_wait &&
          static_cast<uint16_t>(ev.arg) == ping_id) {
        ping_wait = false;
        pong_beep_at = now + kPongBeepDelayMs;
        if (!pong_beep_at) pong_beep_at = 1;
        out.printf("# ping reply id=%u rtt=%lu ms rssi=%.1f snr=%.2f\n", ping_id,
                   static_cast<unsigned long>(ping_sent_ms ? now - ping_sent_ms : 0),
                   static_cast<double>(rssi), static_cast<double>(snr));
      }
      if (is_event && ev.code == fc::EventCode::EjectArmed && ej == Eject::Arming) {
        // Answer this very frame with the fire command (the rocket listens now).
        rf_nonce = static_cast<uint32_t>(ev.arg);
        rf_have_nonce = true;
        ej = Eject::Firing;
        queue_rf(UplinkKind::Eject, rf_pulse_ms, now);
        out.println("# eject rocket armed: sending FIRE");
      }
      if (is_event && ev.code == fc::EventCode::ManualEject) {
        if (ej != Eject::Idle) ej = Eject::Idle;
        out.printf("# eject FIRED at %.1f m\n", static_cast<double>(ev.arg) / 100.0);
        beeper.clear();
        warn_sound = true;
        beeper.push(1000, 0);
      }
      if (is_event && ej != Eject::Idle &&
          (ev.code == fc::EventCode::RemoteRefused || ev.code == fc::EventCode::RemoteDisarmed)) {
        char what[80];
        snprintf(what, sizeof(what), "refused by the rocket: %s",
                 fc::to_string(static_cast<fc::RemoteFireEnd>(ev.arg)));
        eject_end(what, now);
      }
      if (rf_pending && !reply_pending) {
        rf_pending = false;
        const bool flying = rocket_state == fc::FlightState::Boost ||
                            rocket_state == fc::FlightState::Coast ||
                            rocket_state == fc::FlightState::Descent;
        const bool any_ground_state = rf_kind == fc::tlm::UplinkKind::FireDisarm ||
                                      rf_kind == fc::tlm::UplinkKind::BenchMode;
        const bool any_state = rf_kind == UplinkKind::Ping || rf_kind == UplinkKind::EjectArm ||
                               rf_kind == UplinkKind::Eject;
        // In flight the rocket listens only with manual_eject (or a bench simulation).
        const bool listens_in_flight =
            (rocket_link & (fc::tlm::kLinkManualEject | fc::tlm::kLinkBenchMode)) != 0;
        if (any_state && flying && !listens_in_flight) {
          if (rf_kind == UplinkKind::Ping) ping_end("the rocket does not listen in flight (manual_eject = 0)");
          else eject_end("FAILED: the rocket does not listen in flight (manual_eject = 0)", now);
        } else if (rocket_state == fc::FlightState::Pad || (any_ground_state && !flying) || any_state) {
          reply_pending = true;
          reply_is_rf = true;
          reply_due = now + kReplyDelayMs;
        } else {
          out.printf("# rf error: rocket is in %s: command dropped\n",
                        fc::to_string(rocket_state));
        }
      } else if (policy.on_rocket_frame(rocket_state, now)) {
        reply_pending = true;
        reply_is_rf = false;
        reply_due = now + kReplyDelayMs;
      }
      out.printf("%s,%.1f,%.2f\n", line, static_cast<double>(rssi), static_cast<double>(snr));
    } else {
      ++n_bad;
      out.printf("# bad frame: %u bytes, %s, rssi %.1f snr %.2f\n", static_cast<unsigned>(n),
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
      out.printf("# noise %.1f\n", static_cast<double>(noise_sum / noise_n));
      noise_sum = 0;
      noise_n = 0;
    }
  }
  if (now - t_stats >= 10000) {
    t_stats = now;
    if (wifi.on) print_wifi();
    print_panel(now);
    print_battery();
    out.printf("# stats: %lu ok, %lu bad, last rssi %.1f dBm snr %.2f dB\n",
                  static_cast<unsigned long>(n_ok), static_cast<unsigned long>(n_bad),
                  static_cast<double>(last_rssi), static_cast<double>(last_snr));
    print_uplink();
  }

  update_panel(now);

  // Commands from WiFi browsers, same as USB.
  char wcmd[sizeof(cmd)];
  while (web::poll_command(wcmd, sizeof(wcmd))) exec(wcmd);

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
