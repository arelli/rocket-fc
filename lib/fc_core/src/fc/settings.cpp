#include "fc/settings.h"

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace fc {

namespace {

#define FC_SETTINGS_DEF(type, name, group, def, mn, mx, unit, flags, opts, help)                \
  ParamDef{#name, group, ParamType::type, static_cast<double>(def), static_cast<double>(mn), \
           static_cast<double>(mx), unit, static_cast<uint8_t>(flags), opts, help,          \
           offsetof(Settings, name)},
const ParamDef kDefs[] = {FC_SETTINGS(FC_SETTINGS_DEF)};
#undef FC_SETTINGS_DEF
constexpr size_t kCount = sizeof(kDefs) / sizeof(kDefs[0]);

template <typename T>
T& field(Settings& s, const ParamDef& d) {
  return *reinterpret_cast<T*>(reinterpret_cast<char*>(&s) + d.offset);
}
template <typename T>
const T& field(const Settings& s, const ParamDef& d) {
  return *reinterpret_cast<const T*>(reinterpret_cast<const char*>(&s) + d.offset);
}

// Name of enum option i (copied into buf). Returns false if there is none.
bool option_name(const ParamDef& d, unsigned i, char* buf, size_t cap) {
  const char* p = d.options;
  for (unsigned k = 0; p && *p; ++k) {
    const char* end = strchr(p, '|');
    const size_t n = end ? static_cast<size_t>(end - p) : strlen(p);
    if (k == i) {
      if (n + 1 > cap) return false;
      memcpy(buf, p, n);
      buf[n] = '\0';
      return true;
    }
    if (!end) break;
    p = end + 1;
  }
  return false;
}

int option_index(const ParamDef& d, const char* text) {
  char name[24];
  for (unsigned i = 0; option_name(d, i, name, sizeof(name)); ++i) {
    if (!strcmp(name, text)) return static_cast<int>(i);
  }
  return -1;
}

void say(char* why, size_t cap, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
void say(char* why, size_t cap, const char* fmt, ...) {
  if (!why || cap == 0) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(why, cap, fmt, ap);
  va_end(ap);
}

bool in_range(const Settings& s, const ParamDef& d) {
  const double v = param_value(s, d);
  if (!(v == v)) return false;  // NaN
  return v >= d.min && v <= d.max;
}

}  // namespace

size_t param_count() { return kCount; }
const ParamDef& param_def(size_t i) { return kDefs[i < kCount ? i : 0]; }

const ParamDef* find_param(const char* key) {
  if (!key) return nullptr;
  for (const ParamDef& d : kDefs)
    if (!strcmp(d.key, key)) return &d;
  return nullptr;
}

const char* to_string(ParamType t) {
  switch (t) {
    case ParamType::F32: return "f32";
    case ParamType::U32: return "u32";
    case ParamType::I32: return "i32";
    case ParamType::Bool: return "bool";
    case ParamType::Enum: return "enum";
  }
  return "?";
}

double param_value(const Settings& s, const ParamDef& d) {
  switch (d.type) {
    case ParamType::F32: return field<float>(s, d);
    case ParamType::U32: return field<uint32_t>(s, d);
    case ParamType::I32: return field<int32_t>(s, d);
    case ParamType::Bool: return field<bool>(s, d) ? 1.0 : 0.0;
    case ParamType::Enum: return field<uint8_t>(s, d);
  }
  return 0;
}

bool param_equal(const Settings& a, const Settings& b, const ParamDef& d) {
  switch (d.type) {
    case ParamType::F32: return memcmp(&field<float>(a, d), &field<float>(b, d), sizeof(float)) == 0;
    case ParamType::U32: return field<uint32_t>(a, d) == field<uint32_t>(b, d);
    case ParamType::I32: return field<int32_t>(a, d) == field<int32_t>(b, d);
    case ParamType::Bool: return field<bool>(a, d) == field<bool>(b, d);
    case ParamType::Enum: return field<uint8_t>(a, d) == field<uint8_t>(b, d);
  }
  return false;
}

size_t format_param(const Settings& s, const ParamDef& d, char* buf, size_t cap) {
  if (cap == 0) return 0;
  int n = 0;
  switch (d.type) {
    case ParamType::F32: n = snprintf(buf, cap, "%.7g", static_cast<double>(field<float>(s, d))); break;
    case ParamType::U32:
      n = (d.flags & kParamHex) ? snprintf(buf, cap, "0x%lX", static_cast<unsigned long>(field<uint32_t>(s, d)))
                                : snprintf(buf, cap, "%lu", static_cast<unsigned long>(field<uint32_t>(s, d)));
      break;
    case ParamType::I32: n = snprintf(buf, cap, "%ld", static_cast<long>(field<int32_t>(s, d))); break;
    case ParamType::Bool: n = snprintf(buf, cap, "%u", field<bool>(s, d) ? 1u : 0u); break;
    case ParamType::Enum:
      if (!option_name(d, field<uint8_t>(s, d), buf, cap)) n = snprintf(buf, cap, "%u", field<uint8_t>(s, d));
      else n = static_cast<int>(strlen(buf));
      break;
  }
  if (n < 0) n = 0;
  return static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : cap - 1;
}

const char* set_param(Settings& s, const ParamDef& d, const char* text) {
  if (!text || !*text) return "missing value";
  char* end = nullptr;
  Settings t = s;  // set on a copy, check the range, then commit
  switch (d.type) {
    case ParamType::F32: {
      const double v = strtod(text, &end);
      if (end == text || *end || !(v == v) || isinf(v)) return "not a number";
      field<float>(t, d) = static_cast<float>(v);
      break;
    }
    case ParamType::U32: {
      if (*text == '-') return "must not be negative";
      const unsigned long long v = strtoull(text, &end, 0);
      if (end == text || *end) return "not a whole number";
      if (v > 0xFFFFFFFFull) return "out of range";
      field<uint32_t>(t, d) = static_cast<uint32_t>(v);
      break;
    }
    case ParamType::I32: {
      const long long v = strtoll(text, &end, 0);
      if (end == text || *end) return "not a whole number";
      if (v < INT32_MIN || v > INT32_MAX) return "out of range";
      field<int32_t>(t, d) = static_cast<int32_t>(v);
      break;
    }
    case ParamType::Bool:
      if (!strcmp(text, "1") || !strcmp(text, "on") || !strcmp(text, "true") || !strcmp(text, "yes")) {
        field<bool>(t, d) = true;
      } else if (!strcmp(text, "0") || !strcmp(text, "off") || !strcmp(text, "false") || !strcmp(text, "no")) {
        field<bool>(t, d) = false;
      } else {
        return "must be 0 or 1";
      }
      break;
    case ParamType::Enum: {
      int i = option_index(d, text);
      if (i < 0) {
        const long v = strtol(text, &end, 10);
        if (end == text || *end) return "not one of the options";
        i = static_cast<int>(v);
      }
      if (i < 0 || i > 255) return "not one of the options";
      field<uint8_t>(t, d) = static_cast<uint8_t>(i);
      break;
    }
  }
  if (!in_range(t, d)) return "out of range";
  s = t;
  return nullptr;
}

void reset_param(Settings& s, const ParamDef& d) {
  static const Settings kDefaults;
  switch (d.type) {
    case ParamType::F32: field<float>(s, d) = field<float>(kDefaults, d); break;
    case ParamType::U32: field<uint32_t>(s, d) = field<uint32_t>(kDefaults, d); break;
    case ParamType::I32: field<int32_t>(s, d) = field<int32_t>(kDefaults, d); break;
    case ParamType::Bool: field<bool>(s, d) = field<bool>(kDefaults, d); break;
    case ParamType::Enum: field<uint8_t>(s, d) = field<uint8_t>(kDefaults, d); break;
  }
}

size_t settings_to_text(const Settings& s, bool only_changed, char* buf, size_t cap) {
  static const Settings kDefaults;
  if (cap == 0) return 0;
  size_t n = 0;
  buf[0] = '\0';
  for (const ParamDef& d : kDefs) {
    if (only_changed && param_equal(s, kDefaults, d)) continue;
    char v[32];
    format_param(s, d, v, sizeof(v));
    const int w = snprintf(buf + n, cap - n, "%s=%s\n", d.key, v);
    if (w < 0 || static_cast<size_t>(w) >= cap - n) {
      buf[0] = '\0';
      return 0;
    }
    n += static_cast<size_t>(w);
  }
  return n;
}

void settings_from_text(Settings& s, const char* text, size_t len, LoadReport* r) {
  LoadReport dummy;
  if (!r) r = &dummy;
  size_t i = 0;
  while (i < len) {
    char line[96];
    size_t n = 0;
    while (i < len && text[i] != '\n') {
      if (n < sizeof(line) - 1) line[n++] = text[i];
      ++i;
    }
    ++i;  // skip '\n'
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) --n;
    line[n] = '\0';
    char* p = line;
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p || *p == '#') continue;
    char* eq = strchr(p, '=');
    if (!eq) {
      ++r->errors;
      if (!r->first_problem[0]) say(r->first_problem, sizeof(r->first_problem), "bad line '%.40s'", p);
      continue;
    }
    *eq = '\0';
    char* key = p;
    char* val = eq + 1;
    for (char* k = eq - 1; k >= key && (*k == ' ' || *k == '\t'); --k) *k = '\0';
    while (*val == ' ' || *val == '\t') ++val;
    const ParamDef* d = find_param(key);
    if (!d) {
      ++r->unknown;
      if (!r->first_problem[0]) say(r->first_problem, sizeof(r->first_problem), "unknown setting '%.40s'", key);
      continue;
    }
    if (const char* why = set_param(s, *d, val)) {
      ++r->errors;
      if (!r->first_problem[0])
        say(r->first_problem, sizeof(r->first_problem), "%.30s=%.20s: %s", key, val, why);
      continue;
    }
    ++r->applied;
  }
}

bool radio_within_eu_rules(const Settings& s) {
  // ERC REC 70-03 annex 1 band 54 (869.40-869.65 MHz): 500 mW ERP, 10 % duty.
  const double half = radio_bw_khz(s) / 2000.0;
  const double lo = s.radio_freq_mhz - half, hi = s.radio_freq_mhz + half;
  const bool band = lo >= 869.40 - 1e-4 && hi <= 869.65 + 1e-4;
  const double hour_s = static_cast<double>(s.radio_duty_pct) / 100.0 * 3600.0 + s.radio_burst_s;
  return band && hour_s <= 360.0 + 1e-6;
}

bool validate_radio(const Settings& s, char* why, size_t cap) {
  for (const ParamDef& d : kDefs) {
    if (!(d.flags & (kParamShared | kParamGround))) continue;
    if (!in_range(s, d)) {
      say(why, cap, "%s out of range", d.key);
      return false;
    }
  }
  if (!(s.gs_search_lo_dbm < s.gs_search_hi_dbm))
    return say(why, cap, "gs_search_lo_dbm must be below gs_search_hi_dbm"), false;
  if (!s.radio_unlock && !radio_within_eu_rules(s)) {
    say(why, cap,
        "radio outside the EU 869.40-869.65 MHz band / 10 %% duty per hour: "
        "set radio_unlock = 1 to accept responsibility");
    return false;
  }
  return true;
}

bool validate(const Settings& s, char* why, size_t cap) {
  for (const ParamDef& d : kDefs) {
    if (!in_range(s, d)) {
      say(why, cap, "%s out of range", d.key);
      return false;
    }
  }
  if (const char* f = validate(flight_computer_config(s).flight)) {
    say(why, cap, "%s", f);
    return false;
  }
  if (s.calib_ms > s.settle_ms) return say(why, cap, "calib_ms must not exceed settle_ms"), false;
  if (!(s.burnout_accel_g < s.launch_accel_g))
    return say(why, cap, "burnout_accel_g must be below launch_accel_g"), false;
  if (!(s.st_p_min_pa < s.st_p_max_pa)) return say(why, cap, "st_p_min_pa must be below st_p_max_pa"), false;
  if (!(s.batt_crit_v < s.batt_low_v)) return say(why, cap, "batt_crit_v must be below batt_low_v"), false;
  if (!(s.batt_present_v < s.batt_crit_v))
    return say(why, cap, "batt_present_v must be below batt_crit_v"), false;
  return validate_radio(s, why, cap);
}

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

uint32_t settings_crc(const Settings& s) {
  uint32_t crc = 0;
  for (const ParamDef& d : kDefs) {
    char line[96];
    char v[32];
    format_param(s, d, v, sizeof(v));
    const int n = snprintf(line, sizeof(line), "%s=%s\n", d.key, v);
    if (n > 0) crc = crc32_update(crc, reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(n));
  }
  return crc;
}

Axis up_axis(const Settings& s) {
  static const Axis kAxes[] = {Axis::PosX, Axis::NegX, Axis::PosY, Axis::NegY, Axis::PosZ, Axis::NegZ};
  return kAxes[s.imu_up_axis < 6 ? s.imu_up_axis : 0];
}

FlightComputer::Config flight_computer_config(const Settings& s) {
  FlightComputer::Config c;
  c.settle_ms = s.settle_ms;
  c.calib_ms = s.calib_ms;
  c.estimator.up_axis = up_axis(s);
  c.estimator.pad_drift_tau_s = s.pad_drift_tau_s;
  c.estimator.filter.baro_sigma_m = s.baro_sigma_m;
  c.estimator.filter.accel_sigma_mps2 = s.accel_sigma;
  c.estimator.filter.no_imu_sigma_mps2 = s.no_imu_sigma;
  FlightConfig& f = c.flight;
  f.launch_accel_g = s.launch_accel_g;
  f.launch_accel_ms = s.launch_accel_ms;
  f.launch_alt_m = s.launch_alt_m;
  f.launch_alt_ms = s.launch_alt_ms;
  f.burnout_accel_g = s.burnout_accel_g;
  f.burnout_ms = s.burnout_ms;
  f.apogee_lockout_ms = s.apogee_lockout_ms;
  f.apogee_confirm_ms = s.apogee_confirm_ms;
  f.apogee_drop_m = s.apogee_drop_m;
  f.min_deploy_alt_m = s.min_deploy_alt_m;
  f.deploy_delay_ms = s.deploy_delay_ms;
  f.backup_deploy_ms = s.backup_deploy_ms;
  f.landed_ms = s.landed_ms;
  f.landed_window_m = s.landed_window_m;
  return c;
}

BatteryMonitor::Params battery_params(const Settings& s) {
  BatteryMonitor::Params p;
  p.low_v = s.batt_low_v;
  p.critical_v = s.batt_crit_v;
  p.hysteresis_v = s.batt_hyst_v;
  p.debounce_ms = s.batt_debounce_ms;
  return p;
}

Ahrs::Params ahrs_params(const Settings& s) {
  Ahrs::Params p;
  p.kp = s.ahrs_kp;
  p.kp_fast = s.ahrs_kp_fast;
  p.accel_gate_g = s.ahrs_gate_g;
  return p;
}

float radio_bw_khz(const Settings& s) {
  static const float kBw[] = {62.5f, 125.0f, 250.0f, 500.0f};
  return kBw[s.radio_bw < 4 ? s.radio_bw : 2];
}

uint32_t gps_baud(const Settings& s) {
  static const uint32_t kBaud[] = {4800, 9600, 19200, 38400, 57600, 115200};
  return kBaud[s.gps_baud < 6 ? s.gps_baud : 1];
}

LoraModulation lora_modulation(const Settings& s) {
  LoraModulation m;
  m.sf = static_cast<uint8_t>(s.radio_sf);
  m.bw_hz = static_cast<uint32_t>(radio_bw_khz(s) * 1000.0f);
  m.cr_denom = static_cast<uint8_t>(s.radio_cr);
  m.preamble = static_cast<uint16_t>(s.radio_preamble);
  return m;
}

}  // namespace fc
