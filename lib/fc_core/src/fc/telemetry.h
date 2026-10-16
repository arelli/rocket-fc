// Telemetry / log / uplink frame format, version 4. The authoritative description is
// docs/TELEMETRY.md; keep the two in sync. The same frames are sent over LoRa
// and written to the onboard logs (RAM and flash), so one decoder reads both.
//
// Common header (10 bytes, little-endian):
//   u8 magic=0xA5 | u8 version=1 | u8 type | u8 state | u16 seq | u32 t_ms
// Every frame ends with a u16 CRC-16/CCITT-FALSE over all preceding bytes.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "fc/types.h"

namespace fc {
namespace tlm {

constexpr uint8_t kMagic = 0xA5;
constexpr uint8_t kVersion = 4;  // 2: attitude; 3: ground link, uplink; 4: UTC in the uplink

// Telemetry, Event, Gps: rocket -> ground (and the onboard log). Uplink: ground -> rocket.
// Gps frames exist only when a GPS module sends data (optional hardware).
enum class FrameType : uint8_t { Telemetry = 1, Event = 2, Uplink = 3, Gps = 4 };

enum Flags : uint8_t {
  kFlagPyroContinuity = 1 << 0,  // igniter circuit closed (arm link fitted + bridge intact)
  kFlagPyroArmed = 1 << 1,       // software-armed (only after launch detection)
  kFlagPyroFired = 1 << 2,       // deployment has been commanded
  kFlagImuOk = 1 << 3,
  kFlagMagOk = 1 << 4,
  kFlagBaroOk = 1 << 5,
  kFlagBattLow = 1 << 6,
  kFlagResumed = 1 << 7,  // this boot resumed a flight after a reset
};

constexpr size_t kHeaderLen = 10;
constexpr size_t kTelemetryLen = 56;
constexpr size_t kEventLen = 17;
constexpr size_t kUplinkLen = 22;
constexpr size_t kGpsLen = 38;
constexpr size_t kMaxFrameLen = kTelemetryLen;

// Engineering-unit view of a telemetry frame. Encoding quantizes to the units
// given in the comments; decode returns the quantized values.
struct Telemetry {
  FlightState state = FlightState::Init;
  uint16_t seq = 0;
  uint32_t t_ms = 0;
  float alt_m = 0;        // filtered altitude AGL      (wire: i32 cm)
  float vel_mps = 0;      // filtered vertical velocity (wire: i16 cm/s)
  Vec3 accel_g;           // rocket-frame accel         (wire: i16 mg each)
  Vec3 gyro_dps;          // rocket-frame rate          (wire: i16 0.1 deg/s each)
  Vec3 mag_uT;            // rocket-frame field         (wire: i16 0.1 uT each)
  float pressure_pa = 0;  // raw baro pressure          (wire: u32 Pa)
  float temp_c = 0;       // baro temperature           (wire: i16 0.01 degC)
  float batt_v = 0;       // battery voltage            (wire: u16 mV)
  uint8_t flags = 0;      // Flags bitmask
  Quat q;                 // attitude, rocket -> earth  (wire: i16 /32767 each, w x y z)
  uint8_t link = 0;       // LinkBits
  int8_t uplink_rssi_dbm = -128;  // last heartbeat as heard by the rocket (-128 = none)
  uint8_t beep_period_s = 0;      // pad status beep period in use (0 = silent)
};

enum LinkBits : uint8_t {
  kLinkGroundHeard = 1 << 0,  // a ground heartbeat arrived within the timeout
  kLinkTimeSynced = 1 << 1,   // the rocket knows real time (TIME_SYNC event logged)
  kLinkUsbHost = 1 << 2,      // a computer is connected over USB (so it powers and charges)
  kLinkBenchMode = 1 << 3,    // bench mode on: test commands enabled (RAM only, off at boot)
  kLinkGpsData = 1 << 4,      // a GPS module is sending valid NMEA data
  kLinkGpsFix = 1 << 5,       // ...and has a position fix
};

// Ground -> rocket frames (fixed length, header "state" byte is 0). The rocket
// ignores all of them in flight.
enum class UplinkKind : uint8_t {
  Heartbeat = 1,   // pad beep period + real time
  FireArm = 2,     // remote test fire, step 1 (bench mode only): pulse_ms
  Fire = 3,        // step 2: code = fc::remote_fire_code(nonce, key), pulse_ms
  FireDisarm = 4,  // cancel an armed remote test fire
  BenchMode = 5,   // pulse_ms = 1 on / 0 off, code = fc::bench_mode_code(on, key)
};

struct Uplink {
  uint16_t seq = 0;
  uint32_t t_ms = 0;            // ground station clock
  UplinkKind kind = UplinkKind::Heartbeat;
  // Heartbeat
  uint8_t beep_period_s = 10;   // pad status beep period (0 = no pad beeps)
  uint64_t utc_ms = 0;          // real time (Unix ms) when transmitted, 0 = unknown
  // FireArm / Fire / BenchMode
  uint32_t code = 0;
  uint16_t pulse_ms = 0;
};

// GPS position (optional module, see fc/gps.h). t_ms = when the frame was
// made; the fix itself is age_ms older.
struct Gps {
  FlightState state = FlightState::Init;
  uint16_t seq = 0;
  uint32_t t_ms = 0;
  uint8_t quality = 0;     // 0 = no fix (GGA fix quality)
  uint8_t sats = 0;
  float hdop = 0;          // (wire: u16 0.01)
  int32_t lat_e7 = 0;      // 1e-7 deg
  int32_t lon_e7 = 0;      // 1e-7 deg
  float alt_msl_m = 0;     // (wire: i32 cm)
  float speed_mps = 0;     // ground speed (wire: u16 0.1 m/s)
  float course_deg = 0;    // (wire: u16 0.01 deg)
  uint16_t age_ms = 0;     // age of the fix when the frame was made (65535 = older)
  uint32_t utc_s = 0;      // Unix time of the fix, 0 = unknown
};

struct Event {
  FlightState state = FlightState::Init;
  uint16_t seq = 0;
  uint32_t t_ms = 0;
  EventCode code = EventCode::Boot;
  int32_t arg = 0;
};

// Encoders return the frame length, or 0 if `cap` is too small.
size_t encode(const Telemetry& t, uint8_t* out, size_t cap);
size_t encode(const Event& e, uint8_t* out, size_t cap);
size_t encode(const Uplink& u, uint8_t* out, size_t cap);
size_t encode(const Gps& g, uint8_t* out, size_t cap);

enum class DecodeResult : uint8_t {
  Ok = 0,
  TooShort,
  BadMagic,
  BadVersion,
  BadType,
  BadLength,
  BadCrc,
};

// Peek at the frame type of a CRC-valid frame. Returns BadType etc. on error.
DecodeResult frame_type(const uint8_t* buf, size_t len, FrameType* type);
DecodeResult decode(const uint8_t* buf, size_t len, Telemetry* out);
DecodeResult decode(const uint8_t* buf, size_t len, Event* out);
DecodeResult decode(const uint8_t* buf, size_t len, Uplink* out);
DecodeResult decode(const uint8_t* buf, size_t len, Gps* out);

const char* to_string(DecodeResult r);

}  // namespace tlm
}  // namespace fc
