#include "fc/telemetry.h"

#include "fc/bytes.h"
#include "fc/crc16.h"

namespace fc {
namespace tlm {

namespace {

void write_header(ByteWriter& w, FrameType type, FlightState state, uint16_t seq,
                  uint32_t t_ms) {
  w.u8(kMagic);
  w.u8(kVersion);
  w.u8(static_cast<uint8_t>(type));
  w.u8(static_cast<uint8_t>(state));
  w.u16(seq);
  w.u32(t_ms);
}

size_t finish(ByteWriter& w, uint8_t* out) {
  if (!w.ok()) return 0;
  const uint16_t crc = crc16_ccitt(out, w.size());
  w.u16(crc);
  return w.ok() ? w.size() : 0;
}

DecodeResult check_frame(const uint8_t* buf, size_t len, FrameType* type) {
  if (len < kHeaderLen + 2) return DecodeResult::TooShort;
  if (buf[0] != kMagic) return DecodeResult::BadMagic;
  if (buf[1] != kVersion) return DecodeResult::BadVersion;
  size_t expected;
  switch (static_cast<FrameType>(buf[2])) {
    case FrameType::Telemetry: expected = kTelemetryLen; break;
    case FrameType::Event: expected = kEventLen; break;
    case FrameType::Uplink: expected = kUplinkLen; break;
    case FrameType::Gps: expected = kGpsLen; break;
    case FrameType::TextCmd:
    case FrameType::TextOut: {
      const bool cmd = static_cast<FrameType>(buf[2]) == FrameType::TextCmd;
      const size_t at = kHeaderLen + (cmd ? 4 : 0);
      if (len < at + 1 + 2) return DecodeResult::TooShort;
      const size_t n = buf[at];
      if (n > (cmd ? kMaxCmdText : kMaxText)) return DecodeResult::BadLength;
      expected = text_frame_len(cmd, n);
      break;
    }
    default: return DecodeResult::BadType;
  }
  if (len != expected) return DecodeResult::BadLength;
  const uint16_t want = static_cast<uint16_t>(buf[len - 2] | (buf[len - 1] << 8));
  if (crc16_ccitt(buf, len - 2) != want) return DecodeResult::BadCrc;
  *type = static_cast<FrameType>(buf[2]);
  return DecodeResult::Ok;
}

}  // namespace

size_t encode(const Telemetry& t, uint8_t* out, size_t cap) {
  ByteWriter w(out, cap);
  write_header(w, FrameType::Telemetry, t.state, t.seq, t.t_ms);
  w.i32(sat_i32(t.alt_m * 100.0f));
  w.i16(sat_i16(t.vel_mps * 100.0f));
  w.i16(sat_i16(t.accel_g.x * 1000.0f));
  w.i16(sat_i16(t.accel_g.y * 1000.0f));
  w.i16(sat_i16(t.accel_g.z * 1000.0f));
  w.i16(sat_i16(t.gyro_dps.x * 10.0f));
  w.i16(sat_i16(t.gyro_dps.y * 10.0f));
  w.i16(sat_i16(t.gyro_dps.z * 10.0f));
  w.i16(sat_i16(t.mag_uT.x * 10.0f));
  w.i16(sat_i16(t.mag_uT.y * 10.0f));
  w.i16(sat_i16(t.mag_uT.z * 10.0f));
  w.u32(sat_u32(t.pressure_pa));
  w.i16(sat_i16(t.temp_c * 100.0f));
  w.u16(sat_u16(t.batt_v * 1000.0f));
  w.u8(t.flags);
  w.i16(sat_i16(t.q.w * 32767.0f));
  w.i16(sat_i16(t.q.x * 32767.0f));
  w.i16(sat_i16(t.q.y * 32767.0f));
  w.i16(sat_i16(t.q.z * 32767.0f));
  w.u8(t.link);
  w.u8(static_cast<uint8_t>(t.uplink_rssi_dbm));
  w.u8(t.beep_period_s);
  return finish(w, out);
}

size_t encode(const Event& e, uint8_t* out, size_t cap) {
  ByteWriter w(out, cap);
  write_header(w, FrameType::Event, e.state, e.seq, e.t_ms);
  w.u8(static_cast<uint8_t>(e.code));
  w.i32(e.arg);
  return finish(w, out);
}

size_t encode(const Uplink& u, uint8_t* out, size_t cap) {
  ByteWriter w(out, cap);
  write_header(w, FrameType::Uplink, FlightState::Init, u.seq, u.t_ms);
  w.u8(static_cast<uint8_t>(u.kind));
  if (u.kind == UplinkKind::Heartbeat) {
    w.u8(u.beep_period_s);
    w.u64(u.utc_ms);
  } else {  // same 9-byte payload area: code, pulse, 3 bytes padding
    w.u32(u.code);
    w.u16(u.pulse_ms);
    w.u8(0);
    w.u16(0);
  }
  return finish(w, out);
}

DecodeResult decode(const uint8_t* buf, size_t len, Uplink* out) {
  FrameType type;
  DecodeResult r = check_frame(buf, len, &type);
  if (r != DecodeResult::Ok) return r;
  if (type != FrameType::Uplink) return DecodeResult::BadType;
  ByteReader rd(buf, len);
  rd.u8();
  rd.u8();
  rd.u8();
  rd.u8();  // state (unused)
  out->seq = rd.u16();
  out->t_ms = rd.u32();
  const uint8_t kind = rd.u8();
  if (kind < 1 || kind > 8) return DecodeResult::BadType;  // unknown kind
  out->kind = static_cast<UplinkKind>(kind);
  if (out->kind == UplinkKind::Heartbeat) {
    out->beep_period_s = rd.u8();
    out->utc_ms = rd.u64();
  } else {
    out->code = rd.u32();
    out->pulse_ms = rd.u16();
  }
  return rd.ok() ? DecodeResult::Ok : DecodeResult::BadLength;
}

DecodeResult frame_type(const uint8_t* buf, size_t len, FrameType* type) {
  return check_frame(buf, len, type);
}

DecodeResult decode(const uint8_t* buf, size_t len, Telemetry* out) {
  FrameType type;
  DecodeResult r = check_frame(buf, len, &type);
  if (r != DecodeResult::Ok) return r;
  if (type != FrameType::Telemetry) return DecodeResult::BadType;
  ByteReader rd(buf, len);
  rd.u8();  // magic
  rd.u8();  // version
  rd.u8();  // type
  out->state = static_cast<FlightState>(rd.u8());
  out->seq = rd.u16();
  out->t_ms = rd.u32();
  out->alt_m = rd.i32() / 100.0f;
  out->vel_mps = rd.i16() / 100.0f;
  out->accel_g.x = rd.i16() / 1000.0f;
  out->accel_g.y = rd.i16() / 1000.0f;
  out->accel_g.z = rd.i16() / 1000.0f;
  out->gyro_dps.x = rd.i16() / 10.0f;
  out->gyro_dps.y = rd.i16() / 10.0f;
  out->gyro_dps.z = rd.i16() / 10.0f;
  out->mag_uT.x = rd.i16() / 10.0f;
  out->mag_uT.y = rd.i16() / 10.0f;
  out->mag_uT.z = rd.i16() / 10.0f;
  out->pressure_pa = static_cast<float>(rd.u32());
  out->temp_c = rd.i16() / 100.0f;
  out->batt_v = rd.u16() / 1000.0f;
  out->flags = rd.u8();
  out->q.w = rd.i16() / 32767.0f;
  out->q.x = rd.i16() / 32767.0f;
  out->q.y = rd.i16() / 32767.0f;
  out->q.z = rd.i16() / 32767.0f;
  out->link = rd.u8();
  out->uplink_rssi_dbm = static_cast<int8_t>(rd.u8());
  out->beep_period_s = rd.u8();
  return rd.ok() ? DecodeResult::Ok : DecodeResult::BadLength;
}

DecodeResult decode(const uint8_t* buf, size_t len, Event* out) {
  FrameType type;
  DecodeResult r = check_frame(buf, len, &type);
  if (r != DecodeResult::Ok) return r;
  if (type != FrameType::Event) return DecodeResult::BadType;
  ByteReader rd(buf, len);
  rd.u8();
  rd.u8();
  rd.u8();
  out->state = static_cast<FlightState>(rd.u8());
  out->seq = rd.u16();
  out->t_ms = rd.u32();
  out->code = static_cast<EventCode>(rd.u8());
  out->arg = rd.i32();
  return rd.ok() ? DecodeResult::Ok : DecodeResult::BadLength;
}

size_t encode(const Gps& g, uint8_t* out, size_t cap) {
  ByteWriter w(out, cap);
  write_header(w, FrameType::Gps, g.state, g.seq, g.t_ms);
  w.u8(g.quality);
  w.u8(g.sats);
  w.u16(sat_u16(g.hdop * 100.0f));
  w.i32(g.lat_e7);
  w.i32(g.lon_e7);
  w.i32(sat_i32(g.alt_msl_m * 100.0f));
  w.u16(sat_u16(g.speed_mps * 10.0f));
  w.u16(sat_u16(g.course_deg * 100.0f));
  w.u16(g.age_ms);
  w.u32(g.utc_s);
  return finish(w, out);
}

DecodeResult decode(const uint8_t* buf, size_t len, Gps* out) {
  FrameType type;
  DecodeResult r = check_frame(buf, len, &type);
  if (r != DecodeResult::Ok) return r;
  if (type != FrameType::Gps) return DecodeResult::BadType;
  ByteReader rd(buf, len);
  rd.u8();
  rd.u8();
  rd.u8();
  out->state = static_cast<FlightState>(rd.u8());
  out->seq = rd.u16();
  out->t_ms = rd.u32();
  out->quality = rd.u8();
  out->sats = rd.u8();
  out->hdop = rd.u16() / 100.0f;
  out->lat_e7 = rd.i32();
  out->lon_e7 = rd.i32();
  out->alt_msl_m = rd.i32() / 100.0f;
  out->speed_mps = rd.u16() / 10.0f;
  out->course_deg = rd.u16() / 100.0f;
  out->age_ms = rd.u16();
  out->utc_s = rd.u32();
  return rd.ok() ? DecodeResult::Ok : DecodeResult::BadLength;
}

size_t encode(const Text& t, uint8_t* out, size_t cap) {
  const size_t n = t.len;
  if (n > (t.cmd ? kMaxCmdText : kMaxText)) return 0;
  ByteWriter w(out, cap);
  write_header(w, t.cmd ? FrameType::TextCmd : FrameType::TextOut, t.state, t.seq, t.t_ms);
  if (t.cmd) w.u32(t.code);
  w.u8(static_cast<uint8_t>(n));
  for (size_t i = 0; i < n; ++i) w.u8(static_cast<uint8_t>(t.text[i]));
  return finish(w, out);
}

DecodeResult decode(const uint8_t* buf, size_t len, Text* out) {
  FrameType type;
  DecodeResult r = check_frame(buf, len, &type);
  if (r != DecodeResult::Ok) return r;
  if (type != FrameType::TextCmd && type != FrameType::TextOut) return DecodeResult::BadType;
  ByteReader rd(buf, len);
  rd.u8();
  rd.u8();
  rd.u8();
  out->cmd = type == FrameType::TextCmd;
  out->state = static_cast<FlightState>(rd.u8());
  out->seq = rd.u16();
  out->t_ms = rd.u32();
  out->code = out->cmd ? rd.u32() : 0;
  out->len = rd.u8();
  for (size_t i = 0; i < out->len; ++i) out->text[i] = static_cast<char>(rd.u8());
  out->text[out->len] = '\0';
  return rd.ok() ? DecodeResult::Ok : DecodeResult::BadLength;
}

const char* to_string(DecodeResult r) {
  switch (r) {
    case DecodeResult::Ok: return "ok";
    case DecodeResult::TooShort: return "too_short";
    case DecodeResult::BadMagic: return "bad_magic";
    case DecodeResult::BadVersion: return "bad_version";
    case DecodeResult::BadType: return "bad_type";
    case DecodeResult::BadLength: return "bad_length";
    case DecodeResult::BadCrc: return "bad_crc";
  }
  return "?";
}

}  // namespace tlm
}  // namespace fc
