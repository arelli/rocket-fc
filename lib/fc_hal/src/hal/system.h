// ESP32 system services: reset cause, RTC-RAM flight record, watchdog, sleep.
#pragma once

#include <Arduino.h>

#include "fc/resume.h"

namespace hal {

fc::ResetCause reset_cause();
const char* reset_reason_name();

// Flight record kept in RTC RAM: survives watchdog/panic/brown-out resets,
// lost on power-off. Stored as raw bytes (no constructor runs at boot).
fc::FlightRecord load_flight_record();
void save_flight_record(const fc::FlightRecord& rec);
void clear_flight_record();

void watchdog_begin(uint32_t timeout_s);
void watchdog_feed();

// Deep sleep with no wake source: only a power cycle or reset restarts.
[[noreturn]] void deep_sleep_forever();

}  // namespace hal
