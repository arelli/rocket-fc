// Persistent settings: every value you may want to change without editing code.
//
// ONE table (FC_SETTINGS below) defines each setting: name, group, type,
// default, range, unit, flags and help text. From it come
//   - the Settings struct the firmware runs on,
//   - range checking and the text format stored in flash ("key=value" lines,
//     only values that differ from the default),
//   - the console "param" commands, and through them the ground station UI's
//     settings panel (the UI builds its form from "param list").
// To add a setting: add a line here, use it in the firmware, document it if it
// matters for flight safety. Nothing else needs to change.
//
// Settings are loaded at boot and stay fixed until the next boot: the running
// configuration never changes mid-session. Its CRC-32 (settings_crc) is
// printed at boot and logged in the CONFIG event, so a log identifies the
// exact settings that flew (together with the git hash in the BOOT event).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "fc/ahrs.h"
#include "fc/battery.h"
#include "fc/flight_computer.h"
#include "fc/radio_budget.h"

namespace fc {

enum class ParamType : uint8_t { F32, U32, I32, Bool, Enum };

enum ParamFlags : uint8_t {
  kParamSafety = 1 << 0,  // decides when the charge fires: change with care, re-test
  kParamShared = 1 << 1,  // radio: must be identical on the ground station kit
  kParamHex = 1 << 2,     // shown in hexadecimal
};

// X(type, name, group, default, min, max, unit, flags, enum options, help)
// Enum defaults/ranges are option indexes; options are separated by '|'.
// Help text is shown in the UI; it may contain commas but no newlines.
// clang-format off
#define FC_SETTINGS(X) \
  X(Enum, imu_up_axis,      "mount",    0, 0, 5, "", kParamSafety, "+X|-X|+Y|-Y|+Z|-Z", \
    "IMU axis that points to the nose. Check: rocket nose-up, 'status' shows accel_up about +1.00 g.") \
  X(U32, settle_ms,         "init",     5000, 2000, 60000, "ms", 0, "", \
    "Power-on countdown. Keep the rocket still.") \
  X(U32, calib_ms,          "init",     2000, 500, 30000, "ms", 0, "", \
    "Last part of the countdown: averages ground pressure, gyro bias and the up axis. At most settle_ms.") \
  X(F32, st_p_min_pa,       "selftest", 30000, 1000, 120000, "Pa", 0, "", \
    "Self-test FAULT if the barometer reads below this (implausible).") \
  X(F32, st_p_max_pa,       "selftest", 110000, 1000, 120000, "Pa", 0, "", \
    "Self-test FAULT if the barometer reads above this.") \
  X(Bool, st_imu_required,  "selftest", 0, 0, 1, "", 0, "", \
    "IMU missing at self-test is a FAULT (no flight) instead of a warning.") \
  X(Bool, st_radio_required,"selftest", 0, 0, 1, "", 0, "", \
    "Radio failure at self-test is a FAULT instead of a warning.") \
  X(F32, st_min_batt_v,     "selftest", 0, 0, 4.2, "V", 0, "", \
    "Self-test FAULT if the battery is below this. 0 = off. Not checked without a cell (USB power).") \
  X(Bool, st_axis_check,    "selftest", 1, 0, 1, "", 0, "", \
    "Warn if the up axis does not read about +1 g during the countdown.") \
  X(F32, st_axis_tol_g,     "selftest", 0.15, 0.05, 0.5, "g", 0, "", \
    "Tolerance of the up-axis check.") \
  X(Bool, st_warn_mag,      "selftest", 1, 0, 1, "", 0, "", \
    "Warn if there is no magnetometer.") \
  X(Bool, st_warn_backup,   "selftest", 1, 0, 1, "", 0, "", \
    "Warn if the backup deployment timer is off.") \
  X(F32, launch_accel_g,    "launch",   2.5, 1.5, 20, "g", kParamSafety, "", \
    "Launch: axial acceleration at or above this (the pad reads about 1 g)...") \
  X(U32, launch_accel_ms,   "launch",   100, 20, 2000, "ms", kParamSafety, "", \
    "...held this long.") \
  X(F32, launch_alt_m,      "launch",   30, 10, 1000, "m", kParamSafety, "", \
    "Launch fallback without IMU: altitude at or above this...") \
  X(U32, launch_alt_ms,     "launch",   500, 100, 5000, "ms", kParamSafety, "", \
    "...held this long.") \
  X(F32, burnout_accel_g,   "launch",   0.5, -2, 5, "g", 0, "", \
    "Burnout (informational): axial acceleration below this... Must be below launch_accel_g.") \
  X(U32, burnout_ms,        "launch",   100, 20, 2000, "ms", 0, "", \
    "...held this long.") \
  X(U32, apogee_lockout_ms, "deploy",   1500, 500, 120000, "ms", kParamSafety, "", \
    "No apogee decision this long after launch (barometer unreliable during boost).") \
  X(U32, apogee_confirm_ms, "deploy",   100, 20, 2000, "ms", kParamSafety, "", \
    "Apogee: falling and below the maximum, held this long.") \
  X(F32, apogee_drop_m,     "deploy",   0.5, 0.1, 100, "m", kParamSafety, "", \
    "Apogee: altitude at least this far below the maximum.") \
  X(F32, min_deploy_alt_m,  "deploy",   10, 0, 10000, "m", kParamSafety, "", \
    "Never deploy if the maximum altitude stayed below this (failed launch, hop).") \
  X(U32, deploy_delay_ms,   "deploy",   0, 0, 5000, "ms", kParamSafety, "", \
    "Fire this long after the apogee decision. 0 = at once. Once decided the charge always fires.") \
  X(U32, backup_deploy_ms,  "deploy",   0, 0, 600000, "ms", kParamSafety, "", \
    "Backup: fire this long after launch if apogee was not detected. 0 = off. Use simulated apogee time + 1-2 s.") \
  X(U32, pyro_pulse_ms,     "deploy",   1000, 10, 2000, "ms", kParamSafety, "", \
    "Igniter on-time at deployment (the driver caps any pulse at 2000 ms).") \
  X(F32, continuity_frac,   "deploy",   0.6, 0.1, 0.95, "", 0, "", \
    "Continuity: drain voltage above this fraction of the battery voltage means the igniter is connected.") \
  X(U32, landed_ms,         "landing",  5000, 1000, 120000, "ms", 0, "", \
    "Landed: altitude stays within the window this long.") \
  X(F32, landed_window_m,   "landing",  2, 0.5, 100, "m", 0, "", \
    "Landed: altitude window.") \
  X(F32, baro_sigma_m,      "estimator", 0.5, 0.05, 20, "m", kParamSafety, "", \
    "Kalman filter: barometric altitude noise (1 sigma). Larger = smoother but slower.") \
  X(F32, accel_sigma,       "estimator", 4, 0.1, 100, "m/s2", kParamSafety, "", \
    "Kalman filter: acceleration model error with the IMU.") \
  X(F32, no_imu_sigma,      "estimator", 20, 1, 500, "m/s2", kParamSafety, "", \
    "Kalman filter: acceleration model error without the IMU (barometer only).") \
  X(F32, pad_drift_tau_s,   "estimator", 60, 5, 3600, "s", 0, "", \
    "On the pad the ground pressure follows the weather with this time constant.") \
  X(F32, ahrs_kp,           "attitude", 1, 0, 20, "1/s", 0, "", \
    "Attitude filter (display only): accelerometer/magnetometer correction gain.") \
  X(F32, ahrs_kp_fast,      "attitude", 10, 0, 50, "1/s", 0, "", \
    "Same during the power-on countdown (fast convergence).") \
  X(F32, ahrs_gate_g,       "attitude", 0.15, 0.01, 1, "g", 0, "", \
    "Use the accelerometer for attitude only while its magnitude is within this of 1 g.") \
  X(Enum, imu_dlpf,         "sensors",  3, 0, 6, "Hz", 0, "250|184|92|41|20|10|5", \
    "IMU digital low-pass filter, gyroscope and accelerometer (approximate corner).") \
  X(Enum, baro_iir,         "sensors",  2, 0, 4, "", 0, "off|2|4|8|16", \
    "Barometer IIR filter coefficient. Higher = smoother, more lag.") \
  X(Enum, baro_osr,         "sensors",  2, 0, 4, "", 0, "x1|x2|x4|x8|x16", \
    "Barometer pressure oversampling. Higher = less noise, fewer samples per second (x16: about 25 Hz).") \
  X(F32, batt_divider,      "battery",  2, 1, 10, "", 0, "", \
    "Battery divider ratio (R_top + R_bottom) / R_bottom. Calibrate: multimeter on the cell vs. 'status'.") \
  X(F32, pyro_divider,      "battery",  2, 1, 10, "", 0, "", \
    "Pyro continuity divider ratio.") \
  X(F32, batt_present_v,    "battery",  2.5, 1, 3.5, "V", 0, "", \
    "Below this no cell is fitted (running from USB).") \
  X(F32, batt_low_v,        "battery",  3.6, 3.0, 4.2, "V", 0, "", \
    "Low battery warning.") \
  X(F32, batt_crit_v,       "battery",  3.4, 2.8, 4.1, "V", 0, "", \
    "Critical battery. Must be below batt_low_v.") \
  X(F32, batt_hyst_v,       "battery",  0.05, 0, 0.5, "V", 0, "", \
    "Hysteresis of the low/critical levels.") \
  X(U32, batt_debounce_ms,  "battery",  3000, 0, 60000, "ms", 0, "", \
    "Ignore dips shorter than this (radio, pyro pulse).") \
  X(U32, batt_sleep_s,      "battery",  30, 0, 3600, "s", 0, "", \
    "Critical this long on the ground (no USB): deep sleep to protect the cell. 0 = never.") \
  X(U32, tlm_pad_ms,        "telemetry", 1000, 200, 60000, "ms", 0, "", \
    "Radio telemetry period on the pad.") \
  X(U32, tlm_flight_ms,     "telemetry", 200, 100, 5000, "ms", 0, "", \
    "Radio telemetry period in flight (the duty-cycle limit may skip packets).") \
  X(U32, tlm_landed_ms,     "telemetry", 5000, 1000, 60000, "ms", 0, "", \
    "Radio telemetry period after landing (recovery beacon).") \
  X(U32, log_flight_ms,     "telemetry", 20, 10, 1000, "ms", 0, "", \
    "Onboard log period in flight.") \
  X(U32, log_pad_ms,        "telemetry", 1000, 100, 60000, "ms", 0, "", \
    "Onboard log period on the ground.") \
  X(Bool, buzzer_on,        "buzzer",   1, 0, 1, "", 0, "", \
    "Buzzer master switch. The remote test fire warning always sounds.") \
  X(Bool, beep_countdown,   "buzzer",   1, 0, 1, "", 0, "", \
    "Tick every second during the power-on countdown.") \
  X(Bool, beep_selftest,    "buzzer",   1, 0, 1, "", 0, "", \
    "Two beeps when the self-test passes.") \
  X(Enum, pad_beeps,        "buzzer",   0, 0, 2, "", 0, "ground|always|never", \
    "Pad status beeps: while the ground station is heard (period set in the UI), always (pad_beep_s), or never.") \
  X(U32, pad_beep_s,        "buzzer",   10, 1, 255, "s", 0, "", \
    "Pad beep period when pad_beeps = always.") \
  X(U32, beep_short_ms,     "buzzer",   80, 20, 1000, "ms", 0, "", \
    "Short beep length (continuity OK: 3 short).") \
  X(U32, beep_long_ms,      "buzzer",   500, 100, 3000, "ms", 0, "", \
    "Long beep length (no continuity: 1 long).") \
  X(U32, report_s,          "buzzer",   10, 2, 120, "s", 0, "", \
    "Period of the fault code and the landed altitude beacon.") \
  X(Bool, landed_beacon,    "buzzer",   1, 0, 1, "", 0, "", \
    "Beep the maximum altitude after landing.") \
  X(Bool, led_on,           "led",      1, 0, 1, "", 0, "", \
    "Status LED master switch.") \
  X(U32, led_bright_pct,    "led",      100, 1, 100, "%", 0, "", \
    "Status LED brightness.") \
  X(Bool, led_flight,       "led",      1, 0, 1, "", 0, "", \
    "LED solid on in flight (off saves a little power).") \
  X(Bool, led_low_batt,     "led",      1, 0, 1, "", 0, "", \
    "Low battery blink pattern on the pad and after landing.") \
  X(F32, radio_freq_mhz,    "radio",    869.525, 860, 930, "MHz", kParamShared, "", \
    "Centre frequency. Without radio_unlock the whole channel must lie in 869.40-869.65 MHz (EU).") \
  X(Enum, radio_bw,         "radio",    2, 0, 3, "kHz", kParamShared, "62.5|125|250|500", \
    "Bandwidth. Narrower = more sensitive, slower.") \
  X(U32, radio_sf,          "radio",    7, 7, 12, "", kParamShared, "", \
    "Spreading factor. Each step up: about 2.5 dB more sensitive, twice the airtime.") \
  X(U32, radio_cr,          "radio",    5, 5, 8, "", kParamShared, "", \
    "Coding rate 4/N.") \
  X(U32, radio_sync,        "radio",    0x12, 0, 255, "", kParamShared | kParamHex, "", \
    "Sync word (0x12 private; not 0x34 LoRaWAN or 0x2B Meshtastic).") \
  X(I32, radio_power_dbm,   "radio",    20, -9, 22, "dBm", kParamShared, "", \
    "Transmit power. 22 dBm is the SX1262's maximum (EU band limit: 27 dBm ERP).") \
  X(U32, radio_preamble,    "radio",    8, 6, 64, "", kParamShared, "", \
    "Preamble length in symbols.") \
  X(Bool, radio_rx_boost,   "radio",    0, 0, 1, "", kParamShared, "", \
    "SX1262 boosted receive gain: about 2 dB more sensitivity for about 2 mA more current.") \
  X(F32, radio_duty_pct,    "radio",    8, 0.1, 100, "%", kParamShared, "", \
    "Long-term transmit duty cycle limit. With radio_burst_s it must stay within 10 % per hour without radio_unlock.") \
  X(U32, radio_burst_s,     "radio",    72, 0, 3600, "s", kParamShared, "", \
    "Burst budget: airtime that may be spent back to back (in flight).") \
  X(Bool, radio_unlock,     "radio",    0, 0, 1, "", kParamShared, "", \
    "I accept responsibility for complying with the radio rules where I operate: allows frequencies outside 869.40-869.65 MHz and more than 10 % duty cycle.") \
  X(U32, rf_key,            "radio",    0x6B1D4F37, 0, 4294967295.0, "", kParamShared | kParamHex, "", \
    "Pairing key for remote test fire and bench mode by radio. Same value on both kits. Not a cryptographic secret.") \
  X(U32, uplink_timeout_ms, "uplink",   5000, 1000, 60000, "ms", 0, "", \
    "The ground station counts as lost after this long without a heartbeat.") \
  X(U32, uplink_rx_ms,      "uplink",   150, 50, 3000, "ms", 0, "", \
    "On the ground: listen this long after each transmission (raised automatically for slow radio settings).")
// clang-format on

template <ParamType> struct ParamCType;
template <> struct ParamCType<ParamType::F32> { using T = float; };
template <> struct ParamCType<ParamType::U32> { using T = uint32_t; };
template <> struct ParamCType<ParamType::I32> { using T = int32_t; };
template <> struct ParamCType<ParamType::Bool> { using T = bool; };
template <> struct ParamCType<ParamType::Enum> { using T = uint8_t; };

struct Settings {
#define FC_SETTINGS_MEMBER(type, name, group, def, mn, mx, unit, flags, opts, help) \
  ParamCType<ParamType::type>::T name = static_cast<ParamCType<ParamType::type>::T>(def);
  FC_SETTINGS(FC_SETTINGS_MEMBER)
#undef FC_SETTINGS_MEMBER
};

struct ParamDef {
  const char* key;
  const char* group;
  ParamType type;
  double def, min, max;
  const char* unit;
  uint8_t flags;
  const char* options;  // enum option names separated by '|'
  const char* help;
  size_t offset;
};

// --- The table -----------------------------------------------------------------
size_t param_count();
const ParamDef& param_def(size_t i);
const ParamDef* find_param(const char* key);  // nullptr if unknown
const char* to_string(ParamType t);

// --- Values ------------------------------------------------------------------------
double param_value(const Settings& s, const ParamDef& d);
bool param_equal(const Settings& a, const Settings& b, const ParamDef& d);
// Text of a value as stored/shown (enum: option name, hex: 0x..., float: %.7g).
size_t format_param(const Settings& s, const ParamDef& d, char* buf, size_t cap);
// Parse and set one value. Returns nullptr on success, otherwise why not
// (the value is unchanged). Enforces the range and enum options.
const char* set_param(Settings& s, const ParamDef& d, const char* text);
void reset_param(Settings& s, const ParamDef& d);

// --- Text form (flash storage, export) -----------------------------------------------
// "key=value\n" lines. only_changed: skip values equal to the default.
// Returns the length written (always NUL-terminated if cap > 0); 0 if it does not fit.
size_t settings_to_text(const Settings& s, bool only_changed, char* buf, size_t cap);
struct LoadReport {
  uint16_t applied = 0;
  uint16_t unknown = 0;  // keys this firmware does not know (ignored)
  uint16_t errors = 0;   // bad values (ignored)
  char first_problem[96] = {0};
};
// Applies "key=value" lines on top of `s` (blank lines and '#' comments ignored).
void settings_from_text(Settings& s, const char* text, size_t len, LoadReport* report);

// --- Checks -------------------------------------------------------------------------
// Ranges, cross-checks between settings, the flight logic's own validation
// and the radio rules. Returns true if usable; otherwise writes why.
bool validate(const Settings& s, char* why, size_t cap);
// Only the radio settings (the ground station uses these).
bool validate_radio(const Settings& s, char* why, size_t cap);
// Inside the EU 869.40-869.65 MHz band, within 10 % duty per hour?
bool radio_within_eu_rules(const Settings& s);

// CRC-32 of the complete settings (all values, defaults included): identifies
// the configuration in logs.
uint32_t settings_crc(const Settings& s);
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len);

// --- What the rest of the code needs ------------------------------------------------
Axis up_axis(const Settings& s);
FlightComputer::Config flight_computer_config(const Settings& s);
BatteryMonitor::Params battery_params(const Settings& s);
Ahrs::Params ahrs_params(const Settings& s);
float radio_bw_khz(const Settings& s);
LoraModulation lora_modulation(const Settings& s);

}  // namespace fc
