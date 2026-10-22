// Shared vocabulary of the flight computer: states, events, sensor samples.
// Numeric values are part of the telemetry wire format (docs/TELEMETRY.md):
// append new values, never renumber existing ones.
#pragma once

#include <stdint.h>

namespace fc {

enum class FlightState : uint8_t {
  Init = 0,     // power-up, settle countdown, ground calibration
  Fault = 1,    // self-test failed: launch detection and pyro locked out
  Pad = 2,      // ready, waiting for launch (pyro software-disarmed)
  Boost = 3,    // launch detected, motor burning (pyro software-armed)
  Coast = 4,    // motor burnout detected, waiting for apogee (armed)
  Descent = 5,  // deployment fired (or apogee passed), descending
  Landed = 6,   // stationary on the ground: recovery beacon
};

enum class EventCode : uint8_t {
  Boot = 1,            // arg: first 8 hex digits of the firmware git hash
  SelfTestOk = 2,      // arg: warning bitmask (WarnBits)
  SelfTestFail = 3,    // arg: fault code (FaultCode)
  Launch = 4,          // arg: altitude AGL [cm]
  Burnout = 5,         // arg: altitude AGL [cm]
  Apogee = 6,          // arg: max altitude AGL [cm]
  PyroFire = 7,        // arg: altitude AGL [cm] when fired (apogee path)
  PyroBackupFire = 8,  // arg: altitude AGL [cm] when fired by backup timer
  Landed = 9,          // arg: max altitude AGL [cm]
  BattLow = 10,        // arg: battery [mV]
  BattCritical = 11,   // arg: battery [mV]
  PyroOpen = 12,       // arg: ms since fire (continuity lost: bridge burned)
  SensorLost = 13,     // arg: sensor bitmask (SensorBits) that stopped answering
  Resumed = 14,        // arg: ms since launch restored after a mid-flight reset
  ApogeeNoDeploy = 15, // arg: max altitude AGL [cm] (below min deploy altitude)
  BenchFire = 16,      // arg: pulse length [ms] (test fire in bench mode)
  GroundLink = 17,     // arg: 1 = ground station heard, 0 = lost
  TimeSync = 18,       // at this event's t_ms, real time was exactly `arg` (Unix seconds)
  RemoteArmed = 19,    // arg: one-time nonce for the remote test fire (bench mode)
  RemoteDisarmed = 20, // arg: RemoteFireEnd reason
  RemoteRefused = 21,  // arg: RemoteFireEnd reason
  Config = 22,         // arg: CRC-32 of the settings in use (fc/settings.h), at boot
  PyroSag = 23,        // arg: battery during a pyro pulse: (before_mV << 16) | min_mV
  BenchMode = 24,      // arg: 1 = bench mode on, 0 = off, 2 = off because launch was detected
  GpsStatus = 25,      // arg: 0 = no GPS data (lost), 1 = data but no fix, 2 = position fix
  Pong = 26,           // arg: ping id (answer to a ground station connectivity check)
  EjectArmed = 27,     // arg: one-time nonce for the manual ejection command
  ManualEject = 28,    // arg: altitude AGL [cm] when the charge was fired by the ground command
};

// Why the computer refuses to fly (FlightState::Fault). Beeped as N long beeps.
enum class FaultCode : uint8_t {
  None = 0,
  BaroMissing = 1,
  BaroImplausible = 2,
  ConfigInvalid = 3,
  ImuMissing = 4,   // only if settings require the IMU (st_imu_required)
  RadioFail = 5,    // only if settings require the radio (st_radio_required)
  BattLow = 6,      // battery below st_min_batt_v at self-test
  GpsMissing = 7,   // gps_mode = required and no GPS data at self-test
};

// Non-fatal self-test findings. Reported once after self-test and in the
// periodic pad report.
enum WarnBits : uint8_t {
  kWarnImuMissing = 1 << 0,
  kWarnMagMissing = 1 << 1,
  kWarnRadioFail = 1 << 2,
  kWarnBattLow = 1 << 3,
  kWarnImuAxis = 1 << 4,  // "up" axis does not read ~+1 g on the pad
  kWarnNoBackupTimer = 1 << 5,
  kWarnGpsMissing = 1 << 6,  // gps_mode = auto and no GPS data at self-test
  kWarnManualEject = 1 << 7, // manual ejection by radio is enabled for this flight
};

enum SensorBits : uint8_t {
  kSensorImu = 1 << 0,
  kSensorMag = 1 << 1,
  kSensorBaro = 1 << 2,
  kSensorRadio = 1 << 3,
};

struct Vec3 {
  float x = 0, y = 0, z = 0;
};

// Unit quaternion (attitude), see fc/ahrs.h for the frames.
struct Quat {
  float w = 1, x = 0, y = 0, z = 0;
};

const char* to_string(FlightState s);
const char* to_string(EventCode e);

}  // namespace fc
