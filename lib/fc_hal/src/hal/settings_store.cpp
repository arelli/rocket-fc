#include "hal/settings_store.h"

#include <Preferences.h>

namespace hal {

namespace {
constexpr const char* kNamespace = "rocketfc";
constexpr const char* kKey = "settings";
}  // namespace

size_t SettingsStore::load(char* buf, size_t cap, bool* ok) {
  *ok = true;
  if (cap == 0) return 0;
  buf[0] = '\0';
  Preferences p;
  if (!p.begin(kNamespace, true)) {
    // Read-only open fails when the namespace was never written: nothing stored.
    return 0;
  }
  size_t n = 0;
  if (p.isKey(kKey)) {
    const size_t len = p.getBytesLength(kKey);
    if (len == 0 || len >= cap) {
      *ok = len == 0;
    } else {
      n = p.getBytes(kKey, buf, len);
      if (n != len) {
        *ok = false;
        n = 0;
      }
    }
  }
  p.end();
  buf[n] = '\0';
  return n;
}

bool SettingsStore::save(const char* text, size_t len) {
  Preferences p;
  if (!p.begin(kNamespace, false)) return false;
  bool ok;
  if (len == 0) {
    ok = !p.isKey(kKey) || p.remove(kKey);
  } else {
    ok = p.putBytes(kKey, text, len) == len;
  }
  p.end();
  return ok;
}

}  // namespace hal
