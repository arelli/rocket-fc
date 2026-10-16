// Text (CSV) form of telemetry and event frames, shared by the ground station
// (radio packets), the flight computer's "dump" command (onboard log) and the
// serial "log" stream, so every data file has the same columns.
//
//   T,seq,t_ms,state,alt_m,vel_mps,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,
//     mx_uT,my_uT,mz_uT,p_pa,temp_c,batt_v,flags,qw,qx,qy,qz,link,up_rssi_dbm,beep_s
//   E,seq,t_ms,state,event,arg
//   G,seq,t_ms,state,fix,sats,hdop,lat_deg,lon_deg,alt_msl_m,speed_mps,course_deg,age_ms,utc_s
//     (GPS, only with the optional module)
//
// Header lines start with '#'. The ground station appends ",rssi_dbm,snr_db".
#pragma once

#include <stddef.h>

#include "fc/telemetry.h"

namespace fc {
namespace csv {

const char* telemetry_header();
const char* event_header();
const char* gps_header();

// Return the number of characters written (excluding the terminating NUL),
// or 0 if the buffer is too small. kLineMax is always enough.
constexpr size_t kLineMax = 200;
size_t format(const tlm::Telemetry& t, char* buf, size_t cap);
size_t format(const tlm::Event& e, char* buf, size_t cap);
size_t format(const tlm::Gps& g, char* buf, size_t cap);

// Decode a raw frame of either type and format it.
// Returns 0 (and leaves buf empty) if the frame is invalid.
size_t format_frame(const uint8_t* frame, size_t len, char* buf, size_t cap);

}  // namespace csv
}  // namespace fc
