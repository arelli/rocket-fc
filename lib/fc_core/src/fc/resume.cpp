#include "fc/resume.h"

#include "fc/bytes.h"
#include "fc/crc16.h"

#include <string.h>

namespace fc {

namespace {
uint16_t record_crc(const FlightRecord& r) {
  // Serialize explicitly so padding bytes never affect the checksum.
  uint8_t buf[24];
  ByteWriter w(buf, sizeof(buf));
  w.u32(r.magic);
  w.u8(r.state);
  w.u8(r.fired);
  w.u16(r.reserved);
  uint32_t u;
  memcpy(&u, &r.ground_pa, 4);
  w.u32(u);
  memcpy(&u, &r.max_alt_m, 4);
  w.u32(u);
  w.u32(r.ms_since_launch);
  return crc16_ccitt(buf, w.size());
}
}  // namespace

void FlightRecord::seal() {
  magic = kMagic;
  crc = record_crc(*this);
}

bool FlightRecord::valid() const { return magic == kMagic && crc == record_crc(*this); }

bool should_resume(const FlightRecord& rec, ResetCause cause) {
  if (cause == ResetCause::PowerOn) return false;
  if (!rec.valid()) return false;
  if (!(rec.ground_pa > 30000.0f && rec.ground_pa < 120000.0f)) return false;
  switch (static_cast<FlightState>(rec.state)) {
    case FlightState::Boost:
    case FlightState::Coast:
    case FlightState::Descent:
    case FlightState::Landed:
      return true;
    default:
      return false;  // on the pad a fresh start is the safe choice
  }
}

const char* to_string(ResetCause c) {
  switch (c) {
    case ResetCause::PowerOn: return "power_on";
    case ResetCause::Software: return "software";
    case ResetCause::Watchdog: return "watchdog";
    case ResetCause::Panic: return "panic";
    case ResetCause::Brownout: return "brownout";
    case ResetCause::Other: return "other";
  }
  return "?";
}

}  // namespace fc
