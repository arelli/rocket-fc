// Optional GPS: NMEA parsing, the GPS frame and its CSV line.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>

#include "fc/csv.h"
#include "fc/gps.h"
#include "fc/telemetry.h"

using namespace fc;

void setUp() {}
void tearDown() {}

namespace {
// "$body*CS\r\n" with the correct checksum.
std::string nmea(const char* body) {
  uint8_t x = 0;
  for (const char* p = body; *p; ++p) x ^= static_cast<uint8_t>(*p);
  char cs[8];
  snprintf(cs, sizeof(cs), "*%02X\r\n", x);
  return std::string("$") + body + cs;
}
int feed(NmeaParser& p, const std::string& s, uint32_t now = 1000) {
  int used = 0;
  for (char c : s) used += p.feed(c, now) ? 1 : 0;
  return used;
}
}  // namespace

static void test_unix_seconds() {
  TEST_ASSERT_EQUAL_UINT32(946684800u, unix_seconds(2000, 1, 1, 0, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(1792154120u, unix_seconds(2026, 10, 16, 12, 35, 20));
}

static void test_gga_rmc_fix() {
  NmeaParser p;
  TEST_ASSERT_EQUAL_INT(1, feed(p, nmea("GNRMC,123520.00,A,3757.1234,N,02343.5678,E,0.5,10.0,161026,,,A")));
  TEST_ASSERT_TRUE(p.time_valid());
  TEST_ASSERT_EQUAL_INT(1, feed(p, nmea("GNGGA,123520.00,3757.1234,N,02343.5678,E,1,11,0.75,95.3,M,32.1,M,,"), 2000));
  const GpsFix& f = p.fix();
  TEST_ASSERT_TRUE(f.valid());
  TEST_ASSERT_EQUAL_UINT8(1, f.quality);
  TEST_ASSERT_EQUAL_UINT8(11, f.sats);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.75f, f.hdop);
  // 37 deg 57.1234 min = 37.952056667 deg; 23 deg 43.5678 min = 23.72613 deg
  TEST_ASSERT_INT32_WITHIN(1, 379520567, f.lat_e7);
  TEST_ASSERT_INT32_WITHIN(1, 237261300, f.lon_e7);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 95.3f, f.alt_msl_m);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f * 0.514444f, f.speed_mps);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 10.0f, f.course_deg);
  TEST_ASSERT_EQUAL_UINT32(1792154120u, f.utc_s);
  TEST_ASSERT_EQUAL_UINT32(2000, p.last_gga_ms());
  TEST_ASSERT_EQUAL_UINT32(1, p.gga_count());
  TEST_ASSERT_EQUAL_UINT32(2, p.sentences());
}

static void test_southern_western_and_midnight() {
  NmeaParser p;
  feed(p, nmea("GNRMC,235959.00,A,3757.1234,N,02343.5678,E,0.0,0.0,161026,,,A"));
  feed(p, nmea("GNGGA,000000.50,3757.1234,S,02343.5678,W,1,05,2.10,12.0,M,,M,,"));
  TEST_ASSERT_TRUE(p.fix().lat_e7 < 0);
  TEST_ASSERT_TRUE(p.fix().lon_e7 < 0);
  TEST_ASSERT_EQUAL_UINT32(1792195200u, p.fix().utc_s);  // next day
  TEST_ASSERT_EQUAL_UINT16(500, p.fix().utc_ms);
}

static void test_no_fix_and_other_sentences() {
  NmeaParser p;
  TEST_ASSERT_EQUAL_INT(1, feed(p, nmea("GPGGA,,,,,,0,00,99.99,,,,,,")));
  TEST_ASSERT_FALSE(p.fix().valid());
  TEST_ASSERT_EQUAL_UINT32(1, p.gga_count());  // data present, no fix
  TEST_ASSERT_EQUAL_INT(1, feed(p, nmea("GNRMC,,V,,,,,,,,,,N")));
  TEST_ASSERT_FALSE(p.time_valid());
  TEST_ASSERT_EQUAL_INT(0, feed(p, nmea("GNGSV,1,1,00,0")));  // valid, not used
  TEST_ASSERT_EQUAL_UINT32(3, p.sentences());
  TEST_ASSERT_EQUAL_UINT32(0, p.checksum_errors());
}

static void test_garbage_and_bad_checksums() {
  NmeaParser p;
  std::string s = nmea("GNGGA,123520.00,3757.1234,N,02343.5678,E,1,11,0.75,95.3,M,32.1,M,,");
  s[20] = '9';  // corrupt a digit
  TEST_ASSERT_EQUAL_INT(0, feed(p, s));
  TEST_ASSERT_EQUAL_UINT32(1, p.checksum_errors());
  TEST_ASSERT_FALSE(p.fix().valid());
  // Line noise from a floating input, an overlong line, a missing checksum.
  feed(p, std::string("\xff\x00\x13 garbage\n", 12));
  feed(p, "$" + std::string(300, 'A') + "\n");
  feed(p, "$GNGGA,1,2,3\r\n");
  TEST_ASSERT_EQUAL_UINT32(0, p.sentences());
  // A sentence cut short by a new '$' is dropped; the next one parses.
  const std::string good = nmea("GNGGA,123520.00,3757.1234,N,02343.5678,E,1,11,0.75,95.3,M,32.1,M,,");
  TEST_ASSERT_EQUAL_INT(1, feed(p, "$GNGGA,1235" + good));
  TEST_ASSERT_TRUE(p.fix().valid());
}

static void test_gps_frame_and_csv() {
  tlm::Gps g;
  g.state = FlightState::Landed;
  g.seq = 77;
  g.t_ms = 123456;
  g.quality = 1;
  g.sats = 9;
  g.hdop = 1.23f;
  g.lat_e7 = -379520567;
  g.lon_e7 = 237261300;
  g.alt_msl_m = 1234.56f;
  g.speed_mps = 12.3f;
  g.course_deg = 271.5f;
  g.age_ms = 420;
  g.utc_s = 1792154120u;
  uint8_t buf[64];
  const size_t n = tlm::encode(g, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT(tlm::kGpsLen, n);
  tlm::Gps o;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(buf, n, &o));
  TEST_ASSERT_EQUAL_INT32(g.lat_e7, o.lat_e7);
  TEST_ASSERT_EQUAL_INT32(g.lon_e7, o.lon_e7);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1234.56f, o.alt_msl_m);
  TEST_ASSERT_EQUAL_UINT32(g.utc_s, o.utc_s);
  tlm::Telemetry t;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::BadType, tlm::decode(buf, n, &t));
  char line[csv::kLineMax];
  TEST_ASSERT_TRUE(csv::format_frame(buf, n, line, sizeof(line)) > 0);
  TEST_ASSERT_EQUAL_STRING("G,77,123456,LANDED,1,9,1.23,-37.9520567,23.7261300,1234.56,12.3,271.5,420,1792154120", line);
  buf[15] ^= 1;
  TEST_ASSERT_EQUAL(tlm::DecodeResult::BadCrc, tlm::decode(buf, n, &o));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_unix_seconds);
  RUN_TEST(test_gga_rmc_fix);
  RUN_TEST(test_southern_western_and_midnight);
  RUN_TEST(test_no_fix_and_other_sentences);
  RUN_TEST(test_garbage_and_bad_checksums);
  RUN_TEST(test_gps_frame_and_csv);
  return UNITY_END();
}
