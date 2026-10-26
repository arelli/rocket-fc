// Ground station firmware (second XIAO ESP32-S3 + Wio-SX1262 kit).
//
// Receives the flight computer's LoRa frames and prints one CSV line per
// frame (format: lib/fc_core/src/fc/csv.h) with the radio signal quality
// appended:   ...,rssi_dbm,snr_db
// Everything printed goes to USB and to the browsers on the station's WiFi
// (src/ground/web.h); commands are accepted from both. The UI is
// tools/ground_station.html, served over WiFi.
//
// Lines starting with '#' are status / diagnostics:
//   # station mode=flight|bench arm=0|1 cont=0|1 ready=0|1 launch=idle|countdown|firing left=<s>
//             search=0|1 vbat=<V> pct=<%> low=0|1        (on change, and every 1-5 s)
//   # launch ...   # ping ...   # eject ...   # noise <dBm>   # stats ...   # uplink ...   # wifi ...
// Rocket console output (command sent with "r"): RC,<line>
// Rocket setting definitions ("rdefs"): D,<same fields as a P line>
//
// Uplink: each rocket frame may be answered 15 ms later (the rocket listens
// right after transmitting) with, in this order: a queued command (ping,
// manual ejection, bench mode), a queued console line, or a heartbeat (pad
// beep period and real time, fc/uplink.h).
//
// Commands (one per line):
//   launch / abort      launch channel: start the countdown / stop it
//   mode flight|bench   station mode (same as flipping the MODE switch; the latest wins)
//   ping                connectivity check (the rocket beeps and answers)
//   search on|off       search mode: beeps faster the stronger the rocket's signal
//   eject [ms] | eject cancel   manual ejection (rocket setting manual_eject, or bench mode on the pad)
//   r <command>         run a console command on the rocket (output: RC lines)
//   rdefs               print the rocket's setting definitions (D lines)
//   rwifi               tell the rocket to join this station's WiFi (log download)
//   beep <0..255>       pad beep period in seconds;  uplink on|off;  time <unix ms>
//   param ...           this station's settings;  wifi ...;  reboot
//   (empty line)        re-print the CSV headers
//
// Panel (docs/HARDWARE.md "Ground station"): MODE switch (open BENCH / closed FLIGHT, the
// rocket's bench mode follows it), ARM switch (in the igniter circuit), push
// button, buzzer, ON and CONNECTED LEDs, battery sense, launch channel.
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "board_pins.h"
#include "fc/battery.h"
#include "fc/crc16.h"
#include "fc/crypto.h"
#include "fc/csv.h"
#include "fc/indicators.h"
#include "fc/launch.h"
#include "fc/param_shell.h"
#include "fc/radio_budget.h"
#include "fc/remote_fire.h"
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

using fc::FlightState;
using fc::tlm::UplinkKind;

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
  char line_[512];
  size_t len_ = 0;
};
Tee out;
web::Config wifi;
bool wifi_up = false;

// --- radio --------------------------------------------------------------------
constexpr uint32_t kReplyDelayMs = 15;  // the rocket switches to receive right after sending
hal::RadioLink radio;
bool radio_ok = false;
fc::UplinkPolicy policy;
fc::DutyCycleLimiter duty(0.08f, 72000);
uint32_t uplink_airtime_us = 0;
uint8_t beep_period_s = 10;
uint16_t uplink_seq = 0;
uint32_t uplink_sent = 0;
int64_t utc_base_ms = 0;  // real time at millis() == 0; 0 = not set
enum class Reply : uint8_t { None, Command, Text, Heartbeat };
Reply reply = Reply::None;
uint32_t reply_due = 0;

// One queued command (ping, ejection, bench mode): waits for the next rocket frame.
constexpr uint32_t kCommandTimeoutMs = 5000;
bool cmd_pending = false;
UplinkKind cmd_kind = UplinkKind::Ping;
uint16_t cmd_arg = 0;  // pulse_ms field
uint32_t cmd_since = 0;
uint32_t eject_nonce = 0;

// One queued console line for the rocket ("r <command>").
bool text_pending = false;
char text_cmd[fc::tlm::kMaxCmdText + 1];
uint32_t text_since = 0;
char rc_line[256];  // rocket output being reassembled into lines
size_t rc_len = 0;
// "rwifi": key exchange in progress (fc/crypto.h); the secret is wiped after use.
uint8_t wifi_secret[32];
bool wifi_exchange = false;
uint32_t wifi_exchange_at = 0;

// --- settings -------------------------------------------------------------------
fc::Settings set;  // radio + station settings, loaded at boot
fc::ParamShell params(true);
hal::SettingsStore store;
bool settings_ok = true;
char settings_problem[160] = {0};

// --- the rocket, as last heard --------------------------------------------------
bool have_frame = false;
uint32_t last_frame_ms = 0, last_tlm_ms = 0;
float frame_rssi = 0;
FlightState rocket_state = FlightState::Init;
uint8_t rocket_link = 0;  // link bits of the last telemetry frame
bool flying() {
  return rocket_state == FlightState::Boost || rocket_state == FlightState::Coast ||
         rocket_state == FlightState::Descent;
}
bool rocket_recent(uint32_t now) { return have_frame && now - last_frame_ms < 3000; }

// --- panel ------------------------------------------------------------------------
fc::PanelButton button;
fc::BeepSequencer beeper;
hal::DigitalOut buzzer, led_power, led_link, gate;
hal::AnalogIn batt_adc, launch_adc;
float vbat = 0;
bool warn_sound = false;  // the beeper plays a warning: always audible
bool search_on = false;
uint32_t t_search_beep = 0;
constexpr uint32_t kSearchHoldMs = 1500;
// MODE switch (debounced): true = BENCH.
bool mode_bench = false, mode_raw = false, mode_switch = false, mode_raw_done = true;
uint32_t mode_raw_since = 0, t_mode_sync = 0;
// Launch channel.
fc::LaunchControl launch;
uint8_t sense_class = 0, sense_cand = 0, sense_count = 0;  // 0 disarmed, 1 armed/open, 2 armed + igniter
float sense_frac = 0;
uint32_t t_sense = 0, t_armed_chirp = 0, gate_on_since = 0;
// Ping.
uint16_t ping_id = 0;
bool ping_wait = false;
uint32_t ping_sent_ms = 0, pong_beep_at = 0;
// Manual ejection.
enum class Eject : uint8_t { Idle, Arming, Firing };
Eject ej = Eject::Idle;
uint32_t ej_since = 0;
uint16_t ej_pulse = 0;

uint32_t n_ok = 0, n_bad = 0, t_stats = 0, t_noise = 0, t_status = 0;
float noise_sum = 0;
int noise_n = 0;
char status_last[200] = {0};
char cmdline[160];
size_t cmd_len = 0;

// =================================================================================
// Output helpers
// =================================================================================

void print_wifi(bool with_pass = false) {
  if (!wifi.on) {
    out.println("# wifi off");
    return;
  }
  out.printf("# wifi %s ssid=%s%s%s channel=%u ip=192.168.4.1 url=http://rocketfc.local clients=%d\n",
             wifi_up ? "on" : "failed", wifi.ssid, with_pass ? " pass=" : "", with_pass ? wifi.pass : "",
             wifi.channel, web::clients());
}

void print_headers() {
  out.printf("# rocket-fc ground station %s  %.3f MHz SF%lu BW%.1f sync 0x%02lX %ld dBm  settings crc %08lX\n",
             FC_GIT_REV, static_cast<double>(set.radio_freq_mhz), static_cast<unsigned long>(set.radio_sf),
             static_cast<double>(fc::radio_bw_khz(set)), static_cast<unsigned long>(set.radio_sync),
             static_cast<long>(set.radio_power_dbm), static_cast<unsigned long>(fc::settings_crc(set)));
  if (!settings_ok) out.printf("# ERROR: settings invalid (%s): running on the defaults\n", settings_problem);
  out.printf("%s,rssi_dbm,snr_db\n", fc::csv::telemetry_header());
  out.printf("%s,rssi_dbm,snr_db\n", fc::csv::event_header());
  out.printf("%s,rssi_dbm,snr_db\n", fc::csv::gps_header());
  print_wifi();
}

void print_uplink() {
  out.printf("# uplink %s beep=%u sent=%lu time=%s\n", policy.enabled() ? "on" : "off", beep_period_s,
             static_cast<unsigned long>(uplink_sent), utc_base_ms ? "set" : "unset");
}

uint64_t utc_now_ms() {
  return utc_base_ms ? static_cast<uint64_t>(utc_base_ms + static_cast<int64_t>(millis())) : 0;
}

bool rocket_ready(uint32_t now) {
  // In PAD (calibrated, deployment logic armed for launch detection), in flight mode.
  return have_frame && now - last_tlm_ms < 3000 && rocket_state == FlightState::Pad &&
         !(rocket_link & fc::tlm::kLinkBenchMode);
}

bool armed() { return sense_class >= 1 || launch.state() == fc::LaunchState::Firing; }
bool continuity() { return sense_class == 2 || launch.state() == fc::LaunchState::Firing; }

fc::LaunchInputs launch_inputs(uint32_t now) {
  fc::LaunchInputs in;
  in.armed = armed();
  in.continuity = continuity();
  in.bench = mode_bench;
  in.need_pad = set.gs_launch_need_pad;
  in.rocket_ready = rocket_ready(now);
  return in;
}

// One status line; printed when it changes (ignoring the voltage) and periodically.
void print_status(uint32_t now, bool force) {
  const char* ls = launch.state() == fc::LaunchState::Countdown ? "countdown"
                   : launch.state() == fc::LaunchState::Firing  ? "firing"
                                                                : "idle";
  char core[200];
  snprintf(core, sizeof(core), "mode=%s arm=%d cont=%d ready=%d launch=%s left=%lu search=%d",
           mode_bench ? "bench" : "flight", armed() ? 1 : 0, continuity() ? 1 : 0, rocket_ready(now) ? 1 : 0, ls,
           static_cast<unsigned long>((launch.remaining_ms(now) + 999) / 1000), search_on ? 1 : 0);
  const bool changed = strcmp(core, status_last) != 0;
  const uint32_t period = (armed() || launch.state() != fc::LaunchState::Idle) ? 1000 : 5000;
  if (!force && !changed && now - t_status < period) return;
  strncpy(status_last, core, sizeof(status_last) - 1);
  t_status = now;
  const bool cell = vbat >= set.batt_present_v;
  out.printf("# station %s vbat=%.2f pct=%.0f low=%d\n", core, static_cast<double>(cell ? vbat : 0),
             static_cast<double>(cell ? fc::liion_percent(vbat) : 0), cell && vbat < set.gs_batt_low_v ? 1 : 0);
}

void warn_beeps(uint8_t n, uint16_t on, uint16_t off) {
  beeper.clear();
  warn_sound = true;
  beeper.beeps(n, on, off, 0);
}

// =================================================================================
// Settings
// =================================================================================

void serial_line(void*, const char* line) { out.println(line); }
void defs_line(void*, const char* line) {
  if (!strncmp(line, "P,", 2)) out.printf("D,%s\n", line + 2);
}
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

// =================================================================================
// Commands to the rocket
// =================================================================================

void queue_command(UplinkKind kind, uint16_t arg, uint32_t now) {
  cmd_kind = kind;
  cmd_arg = arg;
  cmd_since = now;
  cmd_pending = true;
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
  queue_command(UplinkKind::Ping, ping_id, now);
  out.printf("# ping queued id=%u (%s)\n", ping_id, source);
}

void eject_end(const char* what) {
  ej = Eject::Idle;
  if (cmd_pending && (cmd_kind == UplinkKind::EjectArm || cmd_kind == UplinkKind::Eject)) cmd_pending = false;
  out.printf("# eject %s\n", what);
  warn_beeps(3, 60, 90);
}

void start_eject(uint16_t pulse_ms, uint32_t now) {
  ping_wait = false;
  pong_beep_at = 0;
  ej = Eject::Arming;
  ej_since = now;
  ej_pulse = pulse_ms;
  queue_command(UplinkKind::EjectArm, pulse_ms, now);
  out.println("# eject queued: arming");
  warn_beeps(4, 40, 40);
}

void queue_text(const char* line, uint32_t now) {
  const size_t n = strlen(line);
  if (n == 0 || n > fc::tlm::kMaxCmdText) {
    out.printf("RC,! command must be 1..%u characters\n", static_cast<unsigned>(fc::tlm::kMaxCmdText));
    return;
  }
  memcpy(text_cmd, line, n + 1);
  text_pending = true;
  text_since = now;
}

void set_search(bool on, uint32_t now) {
  search_on = on;
  t_search_beep = now;
  if (!warn_sound) {
    beeper.clear();
    if (on) beeper.beeps(2, 60, 80, 300);
    else beeper.push(250, 0);
  }
  print_status(now, true);
}

void set_mode(bool bench, uint32_t now, const char* source) {
  const bool changed = bench != mode_bench;
  mode_bench = bench;
  out.printf("# mode %s (%s)\n", mode_bench ? "BENCH" : "FLIGHT", source);
  if (changed && !warn_sound) {
    beeper.clear();
    beeper.beeps(mode_bench ? 2 : 1, 200, 150, 0);
  }
  t_mode_sync = 0;
  print_status(now, true);
}

void start_launch(uint32_t now, const char* source) {
  const fc::LaunchEnd why = launch.start(now, launch_inputs(now), set.gs_launch_countdown_s * 1000u);
  if (why != fc::LaunchEnd::None) {
    out.printf("# launch refused: %s\n", fc::to_string(why));
    warn_beeps(3, 60, 90);
    return;
  }
  out.printf("# launch countdown %lu s (%s)\n", static_cast<unsigned long>(set.gs_launch_countdown_s), source);
  print_status(now, true);
}

void exec(char* line) {
  const uint32_t now = millis();
  // "r <rest of the line>" goes to the rocket unchanged.
  if (line[0] == 'r' && line[1] == ' ') {
    queue_text(line + 2, now);
    return;
  }
  char* save = nullptr;
  const char* argv[6] = {};
  int argc = 0;
  for (char* t = strtok_r(line, " \t", &save); t && argc < 6; t = strtok_r(nullptr, " \t", &save)) argv[argc++] = t;
  const char* c = argv[0];
  const char* a = argv[1];
  if (!c) {
    print_headers();
  } else if (!strcmp(c, "launch")) {
    start_launch(now, "command");
  } else if (!strcmp(c, "abort")) {
    if (launch.state() == fc::LaunchState::Idle) out.println("# launch: nothing to abort");
    launch.abort();
  } else if (!strcmp(c, "mode") && a && (!strcmp(a, "flight") || !strcmp(a, "bench"))) {
    set_mode(!strcmp(a, "bench"), now, "page");
  } else if (!strcmp(c, "ping")) {
    start_ping(now, "command");
  } else if (!strcmp(c, "search")) {
    if (a) set_search(!strcmp(a, "on"), now);
    else print_status(now, true);
  } else if (!strcmp(c, "eject")) {
    if (a && !strcmp(a, "cancel")) {
      if (ej != Eject::Idle) eject_end("cancelled (the rocket disarms by itself within 10 s)");
    } else {
      const long ms = a ? strtol(a, nullptr, 10) : 0;
      if (ms < 0 || ms > fc::RemoteFire::kMaxPulseMs) {
        out.println("# eject error: pulse must be 1..2000 ms (or none = the rocket's pyro_pulse_ms)");
        return;
      }
      start_eject(static_cast<uint16_t>(ms), now);
    }
  } else if (!strcmp(c, "rdefs")) {
    static fc::ParamShell defs(false);  // the rocket's settings table, with the defaults
    defs.init(fc::Settings{}, fc::Settings{});
    const char* list[] = {"list"};
    defs.exec(1, list, false, defs_line, store_text, &store);
    out.println("# end of rocket definitions");
  } else if (!strcmp(c, "rwifi")) {
    // Step 1 of the sealed join: our public key (the password goes in step 2, encrypted).
    if (!wifi_up) {
      out.println("RC,! this station's WiFi is off");
      return;
    }
    fc::Sha256 h;  // the WiFi radio is on: esp_random() is a true random source
    for (int i = 0; i < 16; ++i) {
      const uint32_t r[2] = {esp_random(), micros()};
      h.update(reinterpret_cast<const uint8_t*>(r), sizeof(r));
    }
    h.final(wifi_secret);
    uint8_t pub[32];
    fc::x25519_public(pub, wifi_secret);
    char t[fc::tlm::kMaxCmdText + 1] = "wifi key ";
    fc::to_hex(pub, 32, t + 9);
    wifi_exchange = true;
    wifi_exchange_at = now;
    queue_text(t, now);
  } else if (!strcmp(c, "param")) {
    params.exec(argc - 1, argv + 1, true, serial_line, store_text, &store);
  } else if (!strcmp(c, "wifi")) {
    wifi_command(a, argv[2]);
  } else if (!strcmp(c, "reboot")) {
    out.println("# rebooting");
    Serial.flush();
    gate.set(false);
    ESP.restart();
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
  } else if (!strcmp(c, "uplink") && a) {
    policy.set_enabled(!strcmp(a, "on"));
    print_uplink();
  } else {
    out.println("# commands: launch, abort, mode flight|bench, ping, search on|off, eject [ms]|cancel, r <rocket command>, rdefs, "
                "rwifi, beep <s>, uplink on|off, time <unix ms>, param ..., wifi ..., reboot");
  }
}

// =================================================================================
// Radio
// =================================================================================

bool send_frame(const uint8_t* buf, size_t n, uint32_t now) {
  if (!duty.try_consume(now, fc::lora_airtime_us(fc::lora_modulation(set), n))) return false;
  if (!radio.send(buf, n)) return false;
  ++uplink_sent;
  return true;
}

void send_command(uint32_t now) {
  fc::tlm::Uplink u;
  u.seq = uplink_seq++;
  u.t_ms = now;
  u.kind = cmd_kind;
  u.pulse_ms = cmd_arg;
  if (cmd_kind == UplinkKind::Eject) u.code = fc::remote_fire_code(eject_nonce, set.rf_key);
  if (cmd_kind == UplinkKind::BenchMode) u.code = fc::bench_mode_code(cmd_arg != 0, set.rf_key);
  uint8_t buf[fc::tlm::kUplinkLen];
  const bool sent = send_frame(buf, fc::tlm::encode(u, buf, sizeof(buf)), now);
  if (cmd_kind == UplinkKind::Ping) {
    if (sent) {
      ping_sent_ms = now ? now : 1;
      out.printf("# ping sent id=%u\n", ping_id);
    } else {
      ping_end("radio busy or duty-cycle budget exhausted");
    }
  } else if (cmd_kind == UplinkKind::EjectArm || cmd_kind == UplinkKind::Eject) {
    if (sent) out.printf("# eject sent %s\n", cmd_kind == UplinkKind::EjectArm ? "arm" : "FIRE");
    else eject_end("FAILED: radio busy or duty-cycle budget exhausted");
  }
}

void send_text(uint32_t now) {
  fc::tlm::Text t;
  t.cmd = true;
  t.seq = uplink_seq++;
  t.t_ms = now;
  t.len = static_cast<uint8_t>(strlen(text_cmd));
  memcpy(t.text, text_cmd, t.len);
  t.code = fc::text_code(t.seq, fc::crc16_ccitt(reinterpret_cast<const uint8_t*>(t.text), t.len), set.rf_key);
  static uint8_t buf[fc::tlm::kMaxTextFrameLen];
  if (!send_frame(buf, fc::tlm::encode(t, buf, sizeof(buf)), now))
    out.println("RC,! not sent (radio busy or duty-cycle budget): try again");
}

void send_heartbeat(uint32_t now) {
  fc::tlm::Uplink u;
  u.seq = uplink_seq++;
  u.t_ms = now;
  u.beep_period_s = beep_period_s;
  u.utc_ms = utc_now_ms();
  uint8_t buf[fc::tlm::kUplinkLen];
  send_frame(buf, fc::tlm::encode(u, buf, sizeof(buf)), now);
}

void wipe(void* p, size_t n) {
  volatile uint8_t* v = static_cast<volatile uint8_t*>(p);
  while (n--) *v++ = 0;
}

// Step 2: the rocket's public key arrived; send the password sealed with the shared key.
void wifi_seal_and_join(const char* rocket_pub_hex) {
  const uint32_t now = millis();
  if (!wifi_exchange || now - wifi_exchange_at > 60000) return;
  wifi_exchange = false;
  uint8_t peer[32], shared[32], key[32];
  if (fc::from_hex(rocket_pub_hex, peer, sizeof(peer)) != 32) {
    wipe(wifi_secret, sizeof(wifi_secret));
    out.println("RC,! bad key from the rocket");
    return;
  }
  fc::x25519(shared, wifi_secret, peer);
  wipe(wifi_secret, sizeof(wifi_secret));
  fc::wifi_secret::derive_key(shared, set.rf_key, key);
  wipe(shared, sizeof(shared));
  const size_t n = strlen(wifi.pass);
  uint8_t ct[fc::wifi_secret::kMaxPass], tag[fc::wifi_secret::kTag];
  fc::wifi_secret::crypt(key, reinterpret_cast<const uint8_t*>(wifi.pass), ct, n);
  fc::wifi_secret::tag(key, wifi.ssid, ct, n, tag);
  wipe(key, sizeof(key));
  char ct_hex[2 * fc::wifi_secret::kMaxPass + 1], tag_hex[2 * fc::wifi_secret::kTag + 1];
  fc::to_hex(ct, n, ct_hex);
  fc::to_hex(tag, sizeof(tag), tag_hex);
  char t[fc::tlm::kMaxCmdText + 1];
  snprintf(t, sizeof(t), "wifi join %s %s %s", wifi.ssid, ct_hex, tag_hex);
  queue_text(t, now);
}

// Rocket console output: whole lines become "RC,<line>".
void rocket_text(const fc::tlm::Text& t) {
  for (size_t i = 0; i < t.len; ++i) {
    const char ch = t.text[i];
    if (ch == '\n' || rc_len == sizeof(rc_line) - 1) {
      rc_line[rc_len] = '\0';
      out.printf("RC,%s\n", rc_line);
      const char* key = strstr(rc_line, "wifi: key ");
      if (key) wifi_seal_and_join(key + 10);
      const char* ip = strstr(rc_line, "wifi: joined ip=");
      if (ip) web::set_rocket_ip(ip + 16);
      if (strstr(rc_line, "wifi: off")) web::set_rocket_ip("");
      rc_len = 0;
      if (ch == '\n') continue;
    }
    rc_line[rc_len++] = ch;
  }
}

void on_event(const fc::tlm::Event& ev, uint32_t now, float rssi, float snr) {
  if (ev.code == fc::EventCode::Pong && ping_wait && static_cast<uint16_t>(ev.arg) == ping_id) {
    ping_wait = false;
    pong_beep_at = now + 500;  // the second sound of the connectivity check
    if (!pong_beep_at) pong_beep_at = 1;
    out.printf("# ping reply id=%u rtt=%lu ms rssi=%.1f snr=%.2f\n", ping_id,
               static_cast<unsigned long>(ping_sent_ms ? now - ping_sent_ms : 0), static_cast<double>(rssi),
               static_cast<double>(snr));
  }
  if (ev.code == fc::EventCode::EjectArmed && ej == Eject::Arming) {
    // Answer this very frame with the fire command (the rocket listens now).
    eject_nonce = static_cast<uint32_t>(ev.arg);
    ej = Eject::Firing;
    queue_command(UplinkKind::Eject, ej_pulse, now);
    out.println("# eject rocket armed: sending FIRE");
  }
  if (ev.code == fc::EventCode::ManualEject) {
    ej = Eject::Idle;
    out.printf("# eject FIRED at %.1f m\n", static_cast<double>(ev.arg) / 100.0);
    warn_beeps(1, 1000, 0);
  }
  if (ej != Eject::Idle && (ev.code == fc::EventCode::RemoteRefused || ev.code == fc::EventCode::RemoteDisarmed)) {
    char what[80];
    snprintf(what, sizeof(what), "refused by the rocket: %s", fc::to_string(static_cast<fc::RemoteFireEnd>(ev.arg)));
    eject_end(what);
  }
}

// A rocket frame arrived: decide what (if anything) to send back in 15 ms.
void plan_reply(uint32_t now) {
  if (reply != Reply::None) return;
  if (cmd_pending) {
    const bool any_state = cmd_kind != UplinkKind::BenchMode;
    const bool listens = !flying() || (rocket_link & (fc::tlm::kLinkManualEject | fc::tlm::kLinkBenchMode));
    if (!listens) {
      cmd_pending = false;
      if (cmd_kind == UplinkKind::Ping) ping_end("the rocket does not listen in flight (manual_eject = 0)");
      else if (cmd_kind != UplinkKind::BenchMode) eject_end("FAILED: the rocket does not listen in flight (manual_eject = 0)");
      return;
    }
    if (any_state || !flying()) {
      cmd_pending = false;
      reply = Reply::Command;
      reply_due = now + kReplyDelayMs;
      return;
    }
  }
  if (text_pending && !flying()) {
    text_pending = false;
    reply = Reply::Text;
    reply_due = now + kReplyDelayMs;
    return;
  }
  if (policy.on_rocket_frame(rocket_state, now)) {
    reply = Reply::Heartbeat;
    reply_due = now + kReplyDelayMs;
  }
}

void receive(uint32_t now) {
  static uint8_t buf[fc::tlm::kMaxTextFrameLen + 8];
  float rssi = 0, snr = 0;
  bool crc_error = false;
  const size_t n = radio.receive(buf, sizeof(buf), &rssi, &snr, &crc_error);
  if (n == 0) return;
  fc::tlm::FrameType type;
  if (crc_error || fc::tlm::frame_type(buf, n, &type) != fc::tlm::DecodeResult::Ok ||
      type == fc::tlm::FrameType::Uplink || type == fc::tlm::FrameType::TextCmd) {
    ++n_bad;
    out.printf("# bad frame: %u bytes, %s, rssi %.1f snr %.2f\n", static_cast<unsigned>(n),
               crc_error ? "LoRa CRC error" : "not a rocket-fc frame", static_cast<double>(rssi),
               static_cast<double>(snr));
    return;
  }
  ++n_ok;
  have_frame = true;
  last_frame_ms = now;
  frame_rssi = rssi;
  rocket_state = static_cast<FlightState>(buf[3]);  // header byte 3: the rocket's flight state
  if (type == fc::tlm::FrameType::TextOut) {
    static fc::tlm::Text t;
    if (fc::tlm::decode(buf, n, &t) == fc::tlm::DecodeResult::Ok) rocket_text(t);
  } else {
    fc::tlm::Telemetry tf;
    if (fc::tlm::decode(buf, n, &tf) == fc::tlm::DecodeResult::Ok) {
      rocket_link = tf.link;
      last_tlm_ms = now;
    }
    fc::tlm::Event ev;
    if (fc::tlm::decode(buf, n, &ev) == fc::tlm::DecodeResult::Ok) on_event(ev, now, rssi, snr);
    char line[fc::csv::kLineMax];
    if (fc::csv::format_frame(buf, n, line, sizeof(line)))
      out.printf("%s,%.1f,%.2f\n", line, static_cast<double>(rssi), static_cast<double>(snr));
  }
  plan_reply(now);
}

// =================================================================================
// Panel: switches, launch channel, button, beeps, LEDs
// =================================================================================

// MODE: the switch sets it when flipped, the page ("mode flight|bench") at any
// time; the latest wins. Open (or no switch) = BENCH, closed = FLIGHT: the
// station boots in BENCH unless the switch is in FLIGHT.
void update_mode(uint32_t now) {
  const bool raw = digitalRead(pins::ground::kModeSwitch) == HIGH;  // open = BENCH
  if (raw != mode_raw) {
    mode_raw = raw;
    mode_raw_since = now;
    mode_raw_done = false;
  }
  if (!mode_raw_done && now - mode_raw_since >= 50) {  // debounced flip
    mode_raw_done = true;
    if (mode_raw != mode_switch) {
      mode_switch = mode_raw;
      set_mode(mode_switch, now, "switch");
    }
  }
  // The rocket's bench mode follows the switch (on the ground; it ends by itself at launch).
  const bool rocket_bench = (rocket_link & fc::tlm::kLinkBenchMode) != 0;
  if (!cmd_pending && have_frame && now - last_tlm_ms < 3000 && !flying() && rocket_bench != mode_bench &&
      (t_mode_sync == 0 || now - t_mode_sync >= 3000)) {
    t_mode_sync = now ? now : 1;
    queue_command(UplinkKind::BenchMode, mode_bench ? 1 : 0, now);
  }
}

void update_launch(uint32_t now) {
  // ARM and igniter from the launch drain: 0 = ARM off; ~2/3 of the battery =
  // armed, no igniter (100k bypass); ~battery = armed with an igniter.
  if (now - t_sense >= 50) {
    t_sense = now;
    const float v = launch_adc.read_volts(4);
    sense_frac = vbat > 2.5f ? v / vbat : 0.0f;
    const uint8_t cls = sense_frac < 0.2f ? 0 : sense_frac < 0.85f ? 1 : 2;
    if (launch.state() != fc::LaunchState::Firing) {  // the drain reads ~0 while firing
      if (cls == sense_cand) {
        if (sense_count < 3) ++sense_count;
      } else {
        sense_cand = cls;
        sense_count = 1;
      }
      if (sense_count >= 3 && cls != sense_class) {
        const bool was_armed = sense_class >= 1;
        sense_class = cls;
        if (!was_armed && cls >= 1) {
          out.println("# launch ARMED");
          warn_beeps(3, 80, 80);
          t_armed_chirp = now;
        } else if (was_armed && cls == 0) {
          out.println("# launch disarmed");
          warn_beeps(1, 400, 0);
        }
        print_status(now, true);
      }
    }
  }

  const fc::LaunchControl::Output o = launch.update(now, launch_inputs(now), set.gs_launch_pulse_ms);
  if (o.tick_s >= 0) {
    out.printf("# launch %ld\n", static_cast<long>(o.tick_s));
    warn_beeps(1, 150, 0);
  }
  if (o.fired_now) {
    out.println("# launch FIRING");
    beeper.clear();
    warn_sound = true;
    beeper.push(static_cast<uint16_t>(set.gs_launch_pulse_ms), 0);
    gate_on_since = now;
  }
  // Independent cap on the gate: never longer than the pulse setting + 0.5 s.
  const bool on = o.gate && now - gate_on_since < set.gs_launch_pulse_ms + 500;
  gate.set(on);
  if (o.ended == fc::LaunchEnd::Fired) {
    out.println("# launch fired");
  } else if (o.ended != fc::LaunchEnd::None) {
    out.printf("# launch aborted: %s\n", fc::to_string(o.ended));
    warn_beeps(3, 60, 90);
  }
  if (o.ended != fc::LaunchEnd::None) print_status(now, true);
  // Armed and waiting: a short chirp every 2 s so nobody forgets.
  if (armed() && launch.state() == fc::LaunchState::Idle && now - t_armed_chirp >= 2000 && !beeper.busy()) {
    t_armed_chirp = now;
    warn_sound = true;
    beeper.push(30, 0);
  }
}

void update_panel(uint32_t now) {
  const auto act = button.update(digitalRead(pins::ground::kButton) == LOW, now, kSearchHoldMs);
  if (act == fc::PanelButton::Action::Short) {
    if (launch.state() != fc::LaunchState::Idle) launch.abort();
    else if (armed()) start_launch(now, "button");
    else start_ping(now, "button");
  } else if (act == fc::PanelButton::Action::Long && !armed()) {
    set_search(!search_on, now);
  }

  if (pong_beep_at && static_cast<int32_t>(now - pong_beep_at) >= 0) {
    pong_beep_at = 0;
    if (!warn_sound) {
      beeper.clear();
      beeper.push(150, 0);
    }
  }

  // Search mode: beep rate from the last frame's signal strength.
  if (search_on && !beeper.busy() && !ping_wait && !pong_beep_at && ej == Eject::Idle && !armed()) {
    if (rocket_recent(now) || (have_frame && now - last_frame_ms < 8000)) {
      if (now - t_search_beep >= fc::SearchBeep::interval_ms(frame_rssi, set.gs_search_lo_dbm, set.gs_search_hi_dbm)) {
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

  static uint32_t t_batt = 0;
  if (now - t_batt >= 2000) {
    t_batt = now;
    vbat = batt_adc.read_volts();
  }
  const bool batt_low = vbat >= set.batt_present_v && vbat < set.gs_batt_low_v;

  // ON LED: steady; 2 Hz blink = ARMED; fast = countdown/firing or radio failed; double blink = battery low.
  // CONNECTED LED: on while rocket frames arrive (< 3 s), off for an instant at each frame.
  bool p = true;
  bool l = rocket_recent(now) && now - last_frame_ms >= 80;
  if (launch.state() != fc::LaunchState::Idle || !radio_ok) {
    p = (now / 80) % 2 == 0;
  } else if (armed()) {
    p = (now / 250) % 2 == 0;
  } else if (batt_low) {
    const uint32_t ph = now % 2000;
    p = !(ph < 120 || (ph >= 240 && ph < 360));
  }
  if (ej != Eject::Idle) p = l = (now / 60) % 2 == 0;
  led_power.set(p);
  led_link.set(l);
}

}  // namespace

void setup() {
  // The launch gate low before anything else (it also has a 100k pull-down).
  gate.begin(pins::ground::kLaunchGate);
  buzzer.begin(pins::ground::kBuzzer, true, true);  // buzzer straight on the pin
  led_power.begin(pins::ground::kLedPower);
  led_link.begin(pins::ground::kLedLink);
  led_power.set(true);
  pinMode(pins::ground::kButton, INPUT_PULLUP);
  pinMode(pins::ground::kModeSwitch, INPUT_PULLUP);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(20);
  // Note: GPIO21 is the Wio-SX1262 button and the XIAO LED: never drive it.

  load_settings();
  batt_adc.begin(pins::ground::kBattSense, set.gs_batt_divider);
  launch_adc.begin(pins::ground::kLaunchSense, set.gs_launch_divider);
  vbat = batt_adc.read_volts();
  mode_raw = mode_switch = mode_bench = digitalRead(pins::ground::kModeSwitch) == HIGH;  // open = BENCH
  wifi = web::load_config();
  wifi_up = web::begin(wifi);
  const hal::RadioLink::Pins p{pins::kLoraSck,  pins::kLoraMiso, pins::kLoraMosi, pins::kLoraNss,
                               pins::kLoraDio1, pins::kLoraReset, pins::kLoraBusy, pins::kLoraRfSw};
  const hal::RadioLink::Params r{set.radio_freq_mhz,
                                 fc::radio_bw_khz(set),
                                 static_cast<uint8_t>(set.radio_sf),
                                 static_cast<uint8_t>(set.radio_cr),
                                 static_cast<uint8_t>(set.radio_sync),
                                 static_cast<int8_t>(set.radio_power_dbm),
                                 static_cast<uint16_t>(set.radio_preamble),
                                 radio_cfg::kTcxoVolts,
                                 set.radio_rx_boost};
  delay(1500);  // give a USB host time to open the port and see the header
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
  out.printf("# mode %s\n", mode_bench ? "BENCH" : "FLIGHT");
  print_uplink();
  print_status(millis(), true);
}

void loop() {
  radio.poll();
  const uint32_t now = millis();

  if (reply != Reply::None && !radio.busy() && now - reply_due < 0x80000000u) {
    const Reply r = reply;
    reply = Reply::None;
    if (r == Reply::Command) send_command(now);
    else if (r == Reply::Text) send_text(now);
    else send_heartbeat(now);
  }
  if (cmd_pending && now - cmd_since > kCommandTimeoutMs) {
    cmd_pending = false;
    if (cmd_kind == UplinkKind::Ping) ping_end("no rocket frame heard within 5 s");
    else if (cmd_kind != UplinkKind::BenchMode) eject_end("FAILED: no rocket frame heard within 5 s");
  }
  if (text_pending && now - text_since > kCommandTimeoutMs) {
    text_pending = false;
    out.println("RC,! not sent: no rocket frame heard within 5 s (rocket off, out of range, or in flight)");
  }
  if (ping_wait && ping_sent_ms && now - ping_sent_ms > 3000) ping_end("no answer within 3 s");
  if (ej != Eject::Idle && now - ej_since > fc::RemoteFire::kWindowMs + 2000) eject_end("FAILED: no answer from the rocket");

  receive(now);

  // Noise floor: average of instantaneous channel RSSI samples, every 2 s.
  if (now - t_noise >= 100) {
    t_noise = now;
    const float rs = radio.channel_rssi_dbm();
    if (!isnan(rs)) {
      noise_sum += rs;
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
    out.printf("# stats: %lu ok, %lu bad\n", static_cast<unsigned long>(n_ok), static_cast<unsigned long>(n_bad));
    print_uplink();
  }

  update_mode(now);
  update_launch(now);
  update_panel(now);
  print_status(now, false);

  // Commands from WiFi browsers and USB.
  char wcmd[sizeof(cmdline)];
  while (web::poll_command(wcmd, sizeof(wcmd))) exec(wcmd);
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      cmdline[cmd_len] = '\0';
      cmd_len = 0;
      exec(cmdline);
    } else if (cmd_len < sizeof(cmdline) - 1) {
      cmdline[cmd_len++] = static_cast<char>(ch);
    }
  }
}
