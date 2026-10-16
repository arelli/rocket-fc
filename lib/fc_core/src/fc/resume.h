// Surviving an unexpected reset in flight.
//
// If the processor resets mid-flight (watchdog, crash, brown-out), a naive
// reboot would re-calibrate "ground" at the current altitude and never deploy.
// The flight computer therefore keeps a small checksummed record in RTC RAM
// (survives resets, not power loss) and resumes the flight from it.
//
// The decision is pure logic so it can be tested; the HAL only stores bytes.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "fc/types.h"

namespace fc {

enum class ResetCause : uint8_t {
  PowerOn,   // cold start: never resume
  Software,  // esp_restart() etc.
  Watchdog,
  Panic,     // crash / exception
  Brownout,
  Other,
};

constexpr uint16_t kRecDeployPending = 1 << 0;  // apogee decided, delayed charge not fired yet

struct FlightRecord {
  static constexpr uint32_t kMagic = 0x464C4931;  // "FLI1"

  uint32_t magic = 0;
  uint8_t state = 0;  // FlightState
  uint8_t fired = 0;
  uint16_t flags = 0;  // kRecDeployPending
  float ground_pa = 0;          // baro reference pressure
  float max_alt_m = 0;
  uint32_t ms_since_launch = 0;
  uint16_t crc = 0;             // CRC-16 over all fields above

  void seal();        // set magic + crc
  bool valid() const; // magic and crc match
};

// Should this boot resume the stored flight?
bool should_resume(const FlightRecord& rec, ResetCause cause);

const char* to_string(ResetCause c);

}  // namespace fc
