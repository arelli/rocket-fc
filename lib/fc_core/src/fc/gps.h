// GPS (optional): NMEA 0183 parser for a serial GNSS module such as the
// Quectel L76K (docs/HARDWARE.md). Pure logic: the firmware feeds it the bytes
// it receives; the unit tests feed it recorded sentences.
//
// Uses GGA (fix quality, satellites, HDOP, position, altitude) and RMC (date,
// speed, course). Any talker (GP, GN, GL, BD, GA). Sentences with a wrong
// checksum are counted and ignored. The GPS is never used for flight
// decisions: position is for recovery, the log and the ground station.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fc {

struct GpsFix {
  uint8_t quality = 0;    // GGA fix quality: 0 = no fix, 1 = GPS, 2 = DGPS, 6 = estimated...
  uint8_t sats = 0;       // satellites used
  float hdop = 0;         // horizontal dilution of precision (0 = unknown)
  int32_t lat_e7 = 0;     // latitude [1e-7 deg], + north
  int32_t lon_e7 = 0;     // longitude [1e-7 deg], + east
  float alt_msl_m = 0;    // altitude above mean sea level
  float speed_mps = 0;    // ground speed (RMC)
  float course_deg = 0;   // course over ground (RMC)
  uint32_t utc_s = 0;     // Unix time of the fix [s] (needs a date from RMC), 0 = unknown
  uint16_t utc_ms = 0;    // ...and milliseconds
  bool valid() const { return quality > 0; }
};

// Unix seconds from a UTC calendar date/time (proleptic Gregorian).
uint32_t unix_seconds(int year, int month, int day, int hour, int minute, int second);

class NmeaParser {
 public:
  static constexpr size_t kMaxLine = 96;  // NMEA allows 82 characters

  // Feed one received byte; returns true when it completed a valid GGA or RMC.
  bool feed(char c, uint32_t now_ms);

  const GpsFix& fix() const { return fix_; }
  // millis() of the last valid sentence of any kind (0 = none yet).
  uint32_t last_sentence_ms() const { return last_ms_; }
  // millis() of the last GGA (position update, with or without a fix).
  uint32_t last_gga_ms() const { return gga_ms_; }
  uint32_t gga_count() const { return gga_count_; }
  uint32_t sentences() const { return sentences_; }
  uint32_t checksum_errors() const { return bad_; }
  // Has a valid RMC with status 'A' (time and date valid) been received?
  bool time_valid() const { return time_valid_; }
  // The last complete line (valid or not), for diagnostics.
  const char* last_line() const { return last_line_; }

 private:
  bool process(uint32_t now_ms);  // true if a GGA or RMC was used
  bool parse_gga(char** f, int n);
  bool parse_rmc(char** f, int n);

  char line_[kMaxLine + 1] = {0};
  size_t len_ = 0;
  bool in_line_ = false;
  char last_line_[kMaxLine + 1] = {0};
  GpsFix fix_;
  uint32_t date_days_ = 0;  // days since 1970-01-01 of the last RMC date (0 = none)
  uint32_t date_sod_ = 0;   // time of day of that RMC (to catch midnight in a later GGA)
  bool time_valid_ = false;
  uint32_t last_ms_ = 0, gga_ms_ = 0;
  uint32_t gga_count_ = 0, sentences_ = 0, bad_ = 0;
};

}  // namespace fc
