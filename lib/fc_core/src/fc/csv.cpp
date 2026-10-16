#include "fc/csv.h"

#include <stdio.h>

namespace fc {
namespace csv {

namespace {
size_t finish(int n, char* buf, size_t cap) {
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    if (cap) buf[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}
}  // namespace

const char* telemetry_header() {
  return "#T,seq,t_ms,state,alt_m,vel_mps,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,"
         "mx_uT,my_uT,mz_uT,p_pa,temp_c,batt_v,flags,qw,qx,qy,qz,link,up_rssi_dbm,beep_s";
}

const char* event_header() { return "#E,seq,t_ms,state,event,arg"; }

const char* gps_header() {
  return "#G,seq,t_ms,state,fix,sats,hdop,lat_deg,lon_deg,alt_msl_m,speed_mps,course_deg,age_ms,utc_s";
}

size_t format(const tlm::Gps& g, char* buf, size_t cap) {
  // Integer degrees + 7 decimals, exact (no float rounding of the position).
  auto deg = [](int32_t e7, char* out, size_t n) {
    const uint32_t a = e7 < 0 ? static_cast<uint32_t>(-static_cast<int64_t>(e7)) : static_cast<uint32_t>(e7);
    snprintf(out, n, "%s%lu.%07lu", e7 < 0 ? "-" : "", static_cast<unsigned long>(a / 10000000u),
             static_cast<unsigned long>(a % 10000000u));
  };
  char lat[16], lon[16];
  deg(g.lat_e7, lat, sizeof(lat));
  deg(g.lon_e7, lon, sizeof(lon));
  const int n = snprintf(buf, cap, "G,%u,%lu,%s,%u,%u,%.2f,%s,%s,%.2f,%.1f,%.1f,%u,%lu",
                         static_cast<unsigned>(g.seq), static_cast<unsigned long>(g.t_ms), to_string(g.state),
                         static_cast<unsigned>(g.quality), static_cast<unsigned>(g.sats),
                         static_cast<double>(g.hdop), lat, lon, static_cast<double>(g.alt_msl_m),
                         static_cast<double>(g.speed_mps), static_cast<double>(g.course_deg),
                         static_cast<unsigned>(g.age_ms), static_cast<unsigned long>(g.utc_s));
  return finish(n, buf, cap);
}

size_t format(const tlm::Telemetry& t, char* buf, size_t cap) {
  const int n = snprintf(
      buf, cap,
      "T,%u,%lu,%s,%.2f,%.2f,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.0f,%.2f,%.3f,0x%02X,"
      "%.4f,%.4f,%.4f,%.4f,%u,%d,%u",
      static_cast<unsigned>(t.seq), static_cast<unsigned long>(t.t_ms), to_string(t.state),
      static_cast<double>(t.alt_m), static_cast<double>(t.vel_mps),
      static_cast<double>(t.accel_g.x), static_cast<double>(t.accel_g.y),
      static_cast<double>(t.accel_g.z), static_cast<double>(t.gyro_dps.x),
      static_cast<double>(t.gyro_dps.y), static_cast<double>(t.gyro_dps.z),
      static_cast<double>(t.mag_uT.x), static_cast<double>(t.mag_uT.y),
      static_cast<double>(t.mag_uT.z), static_cast<double>(t.pressure_pa),
      static_cast<double>(t.temp_c), static_cast<double>(t.batt_v),
      static_cast<unsigned>(t.flags), static_cast<double>(t.q.w), static_cast<double>(t.q.x),
      static_cast<double>(t.q.y), static_cast<double>(t.q.z), static_cast<unsigned>(t.link),
      static_cast<int>(t.uplink_rssi_dbm), static_cast<unsigned>(t.beep_period_s));
  return finish(n, buf, cap);
}

size_t format(const tlm::Event& e, char* buf, size_t cap) {
  const int n = snprintf(buf, cap, "E,%u,%lu,%s,%s,%ld", static_cast<unsigned>(e.seq),
                         static_cast<unsigned long>(e.t_ms), to_string(e.state),
                         to_string(e.code), static_cast<long>(e.arg));
  return finish(n, buf, cap);
}

size_t format_frame(const uint8_t* frame, size_t len, char* buf, size_t cap) {
  if (cap) buf[0] = '\0';
  tlm::FrameType type;
  if (tlm::frame_type(frame, len, &type) != tlm::DecodeResult::Ok) return 0;
  if (type == tlm::FrameType::Uplink) return 0;  // not a rocket frame
  if (type == tlm::FrameType::Gps) {
    tlm::Gps g;
    if (tlm::decode(frame, len, &g) != tlm::DecodeResult::Ok) return 0;
    return format(g, buf, cap);
  }
  if (type == tlm::FrameType::Telemetry) {
    tlm::Telemetry t;
    if (tlm::decode(frame, len, &t) != tlm::DecodeResult::Ok) return 0;
    return format(t, buf, cap);
  }
  tlm::Event e;
  if (tlm::decode(frame, len, &e) != tlm::DecodeResult::Ok) return 0;
  return format(e, buf, cap);
}

}  // namespace csv
}  // namespace fc
