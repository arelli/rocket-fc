#include "fc/gps.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace fc {

namespace {

int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

int64_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

// "ddmm.mmmm" / "dddmm.mmmm" + hemisphere -> 1e-7 degrees. false if empty/invalid.
bool parse_coord(const char* v, const char* hemi, int32_t* out) {
  if (!v || !*v || !hemi || !*hemi) return false;
  char* end = nullptr;
  const double raw = strtod(v, &end);
  if (end == v || *end) return false;
  const double deg = floor(raw / 100.0);
  const double min = raw - deg * 100.0;
  if (min < 0.0 || min >= 60.0) return false;
  double d = deg + min / 60.0;
  if (*hemi == 'S' || *hemi == 'W') d = -d;
  else if (*hemi != 'N' && *hemi != 'E') return false;
  if (d > 180.0 || d < -180.0) return false;
  *out = static_cast<int32_t>(llround(d * 1e7));
  return true;
}

// "hhmmss.sss" -> seconds of the day and milliseconds. false if invalid.
bool parse_time(const char* v, uint32_t* sod, uint16_t* ms) {
  if (!v || strlen(v) < 6) return false;
  for (int i = 0; i < 6; ++i)
    if (v[i] < '0' || v[i] > '9') return false;
  const int h = (v[0] - '0') * 10 + (v[1] - '0');
  const int m = (v[2] - '0') * 10 + (v[3] - '0');
  const int s = (v[4] - '0') * 10 + (v[5] - '0');
  if (h > 23 || m > 59 || s > 60) return false;
  double frac = 0;
  if (v[6] == '.') frac = strtod(v + 6, nullptr);
  *sod = static_cast<uint32_t>(h * 3600 + m * 60 + s);
  *ms = static_cast<uint16_t>(frac * 1000.0 + 0.5) % 1000;
  return true;
}

}  // namespace

uint32_t unix_seconds(int year, int month, int day, int hour, int minute, int second) {
  const int64_t days = days_from_civil(year, month, day);
  return static_cast<uint32_t>(days * 86400 + hour * 3600 + minute * 60 + second);
}

bool NmeaParser::feed(char c, uint32_t now_ms) {
  if (c == '$') {
    in_line_ = true;
    len_ = 0;
    line_[len_++] = c;
    return false;
  }
  if (!in_line_) return false;
  if (c == '\r') return false;
  if (c == '\n') {
    in_line_ = false;
    line_[len_] = '\0';
    memcpy(last_line_, line_, len_ + 1);
    return process(now_ms);
  }
  if (len_ >= kMaxLine) {  // too long: not NMEA, wait for the next '$'
    in_line_ = false;
    return false;
  }
  line_[len_++] = c;
  return false;
}

bool NmeaParser::process(uint32_t now_ms) {
  // $<talker><type>,<fields>*<checksum>
  char* star = strrchr(line_, '*');
  if (!star || star - line_ < 7 || strlen(star) < 3) {
    ++bad_;
    return false;
  }
  const int h = hexval(star[1]), l = hexval(star[2]);
  uint8_t sum = 0;
  for (const char* p = line_ + 1; p < star; ++p) sum ^= static_cast<uint8_t>(*p);
  if (h < 0 || l < 0 || sum != static_cast<uint8_t>(h * 16 + l)) {
    ++bad_;
    return false;
  }
  *star = '\0';

  // Split fields in place (empty fields kept).
  char* f[24];
  int n = 0;
  char* p = line_ + 1;
  f[n++] = p;
  while (*p && n < 24) {
    if (*p == ',') {
      *p = '\0';
      f[n++] = p + 1;
    }
    ++p;
  }
  const size_t idlen = strlen(f[0]);
  if (idlen < 5) return false;
  const char* type = f[0] + idlen - 3;  // talker-independent sentence type
  const bool gga = !strcmp(type, "GGA"), rmc = !strcmp(type, "RMC");
  bool ok = true;  // other sentences: valid, not used
  if (gga) ok = parse_gga(f, n);
  else if (rmc) ok = parse_rmc(f, n);
  if (!ok) return false;
  ++sentences_;
  last_ms_ = now_ms ? now_ms : 1;
  if (gga) {
    gga_ms_ = last_ms_;
    ++gga_count_;
  }
  return gga || rmc;
}

bool NmeaParser::parse_gga(char** f, int n) {
  // 1 time, 2 lat, 3 N/S, 4 lon, 5 E/W, 6 quality, 7 sats, 8 hdop, 9 alt, 10 M
  if (n < 11) return false;
  GpsFix x = fix_;
  x.quality = static_cast<uint8_t>(atoi(f[6]));
  x.sats = static_cast<uint8_t>(atoi(f[7]));
  x.hdop = *f[8] ? static_cast<float>(atof(f[8])) : 0.0f;
  if (x.quality > 0) {
    int32_t lat, lon;
    if (!parse_coord(f[2], f[3], &lat) || !parse_coord(f[4], f[5], &lon)) return false;
    x.lat_e7 = lat;
    x.lon_e7 = lon;
    x.alt_msl_m = static_cast<float>(atof(f[9]));
  }
  uint32_t sod;
  uint16_t ms;
  if (parse_time(f[1], &sod, &ms) && date_days_) {
    // Past midnight since the last RMC date: the next day.
    const uint32_t days = date_days_ + (sod + 43200u < date_sod_ ? 1u : 0u);
    x.utc_s = days * 86400u + sod;
    x.utc_ms = ms;
  }
  fix_ = x;
  return true;
}

bool NmeaParser::parse_rmc(char** f, int n) {
  // 1 time, 2 status A/V, 3 lat, 4 N/S, 5 lon, 6 E/W, 7 speed [kn], 8 course, 9 date ddmmyy
  if (n < 10) return false;
  const bool active = f[2][0] == 'A';
  if (active) {
    fix_.speed_mps = static_cast<float>(atof(f[7]) * 0.514444);
    fix_.course_deg = static_cast<float>(atof(f[8]));
  }
  const char* d = f[9];
  uint32_t sod;
  uint16_t ms;
  if (strlen(d) == 6 && parse_time(f[1], &sod, &ms)) {
    const int day = (d[0] - '0') * 10 + (d[1] - '0');
    const int mon = (d[2] - '0') * 10 + (d[3] - '0');
    const int yr = 2000 + (d[4] - '0') * 10 + (d[5] - '0');
    if (day >= 1 && day <= 31 && mon >= 1 && mon <= 12 && yr >= 2020) {
      date_days_ = static_cast<uint32_t>(days_from_civil(yr, mon, day));
      date_sod_ = sod;
      if (active) {
        fix_.utc_s = date_days_ * 86400u + sod;
        fix_.utc_ms = ms;
        time_valid_ = true;
      }
    }
  }
  return true;
}

}  // namespace fc
