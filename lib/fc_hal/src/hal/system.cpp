#include "hal/system.h"

#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <string.h>

namespace hal {

namespace {
RTC_NOINIT_ATTR uint8_t g_record[sizeof(fc::FlightRecord)];
}

fc::ResetCause reset_cause() {
  switch (esp_reset_reason()) {
    case ESP_RST_SW: return fc::ResetCause::Software;
    case ESP_RST_PANIC: return fc::ResetCause::Panic;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return fc::ResetCause::Watchdog;
    case ESP_RST_BROWNOUT: return fc::ResetCause::Brownout;
    // Power-on, reset button, USB (re-flash), deep-sleep wake, unknown:
    // a person is involved, start fresh.
    default: return fc::ResetCause::PowerOn;
  }
}

const char* reset_reason_name() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_BROWNOUT: return "brown-out";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}

fc::FlightRecord load_flight_record() {
  fc::FlightRecord r;
  memcpy(&r, g_record, sizeof(r));
  return r;
}

void save_flight_record(const fc::FlightRecord& rec) { memcpy(g_record, &rec, sizeof(rec)); }

void clear_flight_record() { memset(g_record, 0, sizeof(g_record)); }

void watchdog_begin(uint32_t timeout_s) {
  esp_task_wdt_init(timeout_s, true);  // panic (-> reset) on timeout
  esp_task_wdt_add(nullptr);           // watch the Arduino loop task
}

void watchdog_feed() { esp_task_wdt_reset(); }

void deep_sleep_forever() {
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_deep_sleep_start();
  for (;;) {
  }
}

}  // namespace hal
