// Settings in flash (ESP32 NVS): the "key=value" text of fc/settings.h,
// only the values that differ from the defaults. Survives power-off and
// reflashing (unless the flash is erased).
#pragma once

#include <stddef.h>

namespace hal {

class SettingsStore {
 public:
  static constexpr size_t kMaxText = 4096;
  // Copies the stored text into buf (NUL-terminated). Returns its length;
  // 0 = nothing stored. *ok = false if the flash could not be read.
  size_t load(char* buf, size_t cap, bool* ok);
  // Stores the text; an empty text erases the entry. Returns false on error.
  bool save(const char* text, size_t len);
};

}  // namespace hal
