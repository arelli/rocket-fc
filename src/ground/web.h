// Ground station WiFi: an access point that serves the UI (tools/ground_station.html,
// embedded at build time) and bridges the ground station's line protocol to
// the browser, so a phone or laptop on the WiFi works like the USB connection.
//
//   GET  /        the UI (any other path too: captive portal)
//   GET  /events  Server-Sent Events: every line the ground station prints
//   POST /cmd     one or more command lines (same commands as over USB)
//
// The web server runs in its own task on core 0 (with the WiFi stack), so a
// slow or vanished browser can never delay the radio loop: lines are handed
// over through a buffer and dropped if it is full.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace web {

struct Config {
  bool on = true;
  char ssid[33] = {0};
  char pass[65] = {0};  // WPA2: 8..63 characters
  uint8_t channel = 6;
};

// From flash; on first boot creates "rocketfc-XXXX" and a random password.
Config load_config();
bool save_config(const Config& c);
// Why a value is not acceptable (nullptr = fine).
const char* check_ssid(const char* s);
const char* check_pass(const char* s);

// Starts the access point, DNS (captive portal), mDNS (rocketfc.local) and the web task.
bool begin(const Config& c);
bool running();
int clients();  // browsers receiving events

// Queue one output line (without '\n') for the browsers. Never blocks.
void publish(const char* line, size_t len);
// Next command line from a browser (NUL-terminated); false if none.
bool poll_command(char* buf, size_t cap);

}  // namespace web
