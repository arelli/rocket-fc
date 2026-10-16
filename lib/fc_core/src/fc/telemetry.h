// Telemetry / log frame format, version 1. The authoritative description is
// docs/TELEMETRY.md; keep the two in sync. The same frames are sent over LoRa
// and (in future) written to the SD card, so one decoder reads both.
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
constexpr uint8_t kVersion = 1;

enum class FrameType : uint8_t { Telemetry = 1, Event = 2 };

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
constexpr size_t kTelemetryLen = 45;
constexpr size_t kEventLen = 17;
constexpr size_t kMaxFrameLen = kTelemetryLen;

// Engineering-unit view of a telemetry frame. Encoding quantizes to the units
// given in the comments; decode returns the quantized values.
struct Telemetry {
  FlightState state = FlightState::Init;
  uint16_t seq = 0;
  uint32_t t_ms = 0;
  float alt_m = 0;        // filtered altitude AGL      (wire: i32 cm)
  float vel_mps = 0;      // filtered vertical velocity (wire: i16 cm/s)
  Vec3 accel_g;           // body-frame accel           (wire: i16 mg each)
  Vec3 gyro_dps;          // body-frame rate            (wire: i16 0.1 deg/s each)
  Vec3 mag_uT;            // body-frame magnetic field  (wire: i16 0.1 uT each)
  float pressure_pa = 0;  // raw baro pressure          (wire: u32 Pa)
  float temp_c = 0;       // baro temperature           (wire: i16 0.01 degC)
  float batt_v = 0;       // battery voltage            (wire: u16 mV)
  uint8_t flags = 0;      // Flags bitmask
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

const char* to_string(DecodeResult r);

}  // namespace tlm
}  // namespace fc
