// The "param" console command, shared by the flight computer and the ground
// station (which only exposes the radio settings, kParamShared).
//
// Three copies of the settings:
//   running  loaded at boot, used by the firmware until the next boot
//   saved    what is stored in flash
//   edit     being edited ("param set"); "param save" checks it and stores it
//
// Output lines (machine-readable, parsed by tools/ground_station.html):
//   P,<key>,<group>,<type>,<value>,<running>,<default>,<min>,<max>,<unit>,<flags>,<options>,<help>
//       value = edit copy; flags: S safety, R radio (both kits), H hex, G ground station only;
//       options: enum names separated by '|'; help is last and may contain commas
//   PE,<count>,<edit crc>,<running crc>,<saved crc>,<ok | invalid: reason>
//   PERR,<key or *>,<reason>
//   PSAVED,<crc>,<number of values that differ from the defaults>
// Human-readable lines start with '#'.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "fc/settings.h"

namespace fc {

class ParamShell {
 public:
  using Out = void (*)(void* ctx, const char* line);
  // Stores the text in flash; returns false on failure.
  using Store = bool (*)(void* ctx, const char* text, size_t len);

  explicit ParamShell(bool radio_only) : radio_only_(radio_only) {}

  void init(const Settings& running, const Settings& saved) {
    running_ = running;
    saved_ = saved;
    edit_ = saved;
  }

  // argv[0] is the subcommand (may be absent: argc == 0 lists everything).
  // `allowed`: false refuses every change (e.g. in flight); reading still works.
  void exec(int argc, const char* const* argv, bool allowed, Out out, Store store, void* ctx);

  const Settings& edit() const { return edit_; }
  const Settings& saved() const { return saved_; }
  const Settings& running() const { return running_; }
  // Flight computer: everything but the ground station's own settings.
  // Ground station: the radio settings (shared) and its own.
  bool visible(const ParamDef& d) const {
    return radio_only_ ? (d.flags & (kParamShared | kParamGround)) != 0 : (d.flags & kParamGround) == 0;
  }
  bool check(const Settings& s, char* why, size_t cap) const;

  // One P line for `d` (NUL-terminated, truncated to cap).
  void format_line(const ParamDef& d, char* buf, size_t cap) const;

 private:
  void summary(Out out, void* ctx) const;
  Settings running_, saved_, edit_;
  bool radio_only_;
};

}  // namespace fc
