// Real-time clock for the logs, without an RTC chip.
//
// The ground station UI gives the ground kit the laptop's time; the ground kit
// puts it in every heartbeat (fc/uplink.h). The flight computer takes the first
// time it receives (heartbeat, or the USB "time" command) and logs one
// TIME_SYNC event: "at rocket time t_ms it was exactly arg Unix seconds".
// Every other timestamp in the log (ms since boot) converts to real time with:
//     utc_ms = arg * 1000 + (t_ms - event_t_ms)
//
// Only the first sync of a boot is used, so timestamps never jump within a log.
// Accuracy is a few ms (radio airtime is compensated); the 32-bit event
// argument limits this scheme to dates before 2038-01-19.
#pragma once

#include <stdint.h>

namespace fc {

class TimeSync {
 public:
  // utc_ms: real time at the moment the message was received, at rocket time now_ms.
  // Returns true if this sync was taken (the first plausible one).
  bool apply(uint64_t utc_ms, uint32_t now_ms) {
    if (synced_ || !plausible(utc_ms)) return false;
    boot_utc_ms_ = static_cast<int64_t>(utc_ms) - static_cast<int64_t>(now_ms);
    synced_ = true;
    return true;
  }

  bool synced() const { return synced_; }

  // Real time at rocket time t_ms (0 if not synced).
  uint64_t utc_ms(uint32_t t_ms) const {
    return synced_ ? static_cast<uint64_t>(boot_utc_ms_ + static_cast<int64_t>(t_ms)) : 0;
  }

  // The TIME_SYNC event: the first whole second at or after now_ms.
  void event_point(uint32_t now_ms, uint32_t* t_ms, int32_t* unix_s) const {
    const uint64_t utc = utc_ms(now_ms);
    const uint64_t s = (utc + 999) / 1000;
    *unix_s = static_cast<int32_t>(s);
    *t_ms = static_cast<uint32_t>(static_cast<int64_t>(s * 1000) - boot_utc_ms_);
  }

  // 2020-01-01 .. 2038-01-19 (the event argument is a signed 32-bit count).
  static bool plausible(uint64_t utc_ms) {
    return utc_ms >= 1577836800000ull && utc_ms < 2147483647000ull;
  }

 private:
  bool synced_ = false;
  int64_t boot_utc_ms_ = 0;
};

}  // namespace fc
