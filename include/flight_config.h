// Fixed firmware constants: values the code is built and tested around, not
// meant to be changed in the field.
//
// Everything you may want to tune (thresholds, timings, filters, battery,
// buzzer, LED, radio) is a SETTING instead: lib/fc_core/src/fc/settings.h,
// changed from the ground station UI or with the "param" console command and
// stored in flash.
#pragma once

#include <stdint.h>

namespace cfg {

constexpr uint32_t kLoopMs = 10;    // 100 Hz control loop (the flight tests assume it)
constexpr uint32_t kSlowMs = 100;   // 10 Hz: ADC, battery, continuity, console
constexpr uint32_t kWatchdogS = 2;  // reset if the loop stalls this long

}  // namespace cfg
