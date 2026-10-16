#include "fc/types.h"

namespace fc {

const char* to_string(FlightState s) {
  switch (s) {
    case FlightState::Init: return "INIT";
    case FlightState::Fault: return "FAULT";
    case FlightState::Pad: return "PAD";
    case FlightState::Boost: return "BOOST";
    case FlightState::Coast: return "COAST";
    case FlightState::Descent: return "DESCENT";
    case FlightState::Landed: return "LANDED";
  }
  return "?";
}

const char* to_string(EventCode e) {
  switch (e) {
    case EventCode::Boot: return "BOOT";
    case EventCode::SelfTestOk: return "SELFTEST_OK";
    case EventCode::SelfTestFail: return "SELFTEST_FAIL";
    case EventCode::Launch: return "LAUNCH";
    case EventCode::Burnout: return "BURNOUT";
    case EventCode::Apogee: return "APOGEE";
    case EventCode::PyroFire: return "PYRO_FIRE";
    case EventCode::PyroBackupFire: return "PYRO_BACKUP_FIRE";
    case EventCode::Landed: return "LANDED";
    case EventCode::BattLow: return "BATT_LOW";
    case EventCode::BattCritical: return "BATT_CRITICAL";
    case EventCode::PyroOpen: return "PYRO_OPEN";
    case EventCode::SensorLost: return "SENSOR_LOST";
    case EventCode::Resumed: return "RESUMED";
    case EventCode::ApogeeNoDeploy: return "APOGEE_NO_DEPLOY";
    case EventCode::BenchFire: return "BENCH_FIRE";
    case EventCode::GroundLink: return "GROUND_LINK";
    case EventCode::TimeSync: return "TIME_SYNC";
    case EventCode::RemoteArmed: return "REMOTE_ARMED";
    case EventCode::RemoteDisarmed: return "REMOTE_DISARMED";
    case EventCode::RemoteRefused: return "REMOTE_REFUSED";
  }
  return "?";
}

}  // namespace fc
