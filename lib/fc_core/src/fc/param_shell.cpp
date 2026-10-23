#include "fc/param_shell.h"

#include <stdio.h>
#include <string.h>

namespace fc {

namespace {

const Settings kDefaults{};

void flags_text(uint8_t f, char* buf) {
  size_t n = 0;
  if (f & kParamSafety) buf[n++] = 'S';
  if (f & kParamShared) buf[n++] = 'R';
  if (f & kParamHex) buf[n++] = 'H';
  if (f & kParamGround) buf[n++] = 'G';
  buf[n] = '\0';
}

void emit(ParamShell::Out out, void* ctx, const char* fmt, const char* a = "", const char* b = "") {
  char line[200];
  snprintf(line, sizeof(line), fmt, a, b);
  out(ctx, line);
}

}  // namespace

bool ParamShell::check(const Settings& s, char* why, size_t cap) const {
  return radio_only_ ? validate_radio(s, why, cap) : validate(s, why, cap);
}

void ParamShell::format_line(const ParamDef& d, char* buf, size_t cap) const {
  char v[32], r[32], def[32], fl[6];
  format_param(edit_, d, v, sizeof(v));
  format_param(running_, d, r, sizeof(r));
  format_param(kDefaults, d, def, sizeof(def));
  flags_text(d.flags, fl);
  // Integer limits in full (%.7g would print 4294967295 as 4.294967e+09).
  const char* lim = d.type == ParamType::F32 ? "%.7g" : "%.0f";
  char mn[24], mx[24];
  snprintf(mn, sizeof(mn), lim, d.min);
  snprintf(mx, sizeof(mx), lim, d.max);
  snprintf(buf, cap, "P,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s", d.key, d.group, to_string(d.type), v, r, def,
           mn, mx, d.unit, fl, d.options, d.help);
}

void ParamShell::line_for(const ParamDef& d, char* buf, size_t cap) const {
  if (!compact_) return format_line(d, buf, cap);
  char v[32], r[32];
  format_param(edit_, d, v, sizeof(v));
  format_param(running_, d, r, sizeof(r));
  snprintf(buf, cap, "V,%s,%s,%s", d.key, v, r);
}

void ParamShell::summary(Out out, void* ctx) const {
  size_t n = 0;
  for (size_t i = 0; i < param_count(); ++i) n += visible(param_def(i)) ? 1 : 0;
  char why[160] = "ok";
  char status[180];
  if (check(edit_, why, sizeof(why))) {
    snprintf(status, sizeof(status), "ok");
  } else {
    snprintf(status, sizeof(status), "invalid: %s", why);
  }
  char line[260];
  snprintf(line, sizeof(line), "PE,%u,%08lX,%08lX,%08lX,%s", static_cast<unsigned>(n),
           static_cast<unsigned long>(settings_crc(edit_)), static_cast<unsigned long>(settings_crc(running_)),
           static_cast<unsigned long>(settings_crc(saved_)), status);
  out(ctx, line);
}

void ParamShell::exec(int argc, const char* const* argv, bool allowed, Out out, Store store, void* ctx) {
  static char text[4096];  // one instance per firmware: keep it off the stack
  char line[400];
  const char* sub = argc > 0 ? argv[0] : "list";
  const char* key = argc > 1 ? argv[1] : nullptr;

  if (!strcmp(sub, "list")) {
    for (size_t i = 0; i < param_count(); ++i) {
      const ParamDef& d = param_def(i);
      if (!visible(d) || (key && strcmp(key, d.group) != 0)) continue;
      if (compact_ && param_equal(edit_, kDefaults, d) && param_equal(running_, kDefaults, d)) continue;
      line_for(d, line, sizeof(line));
      out(ctx, line);
    }
    summary(out, ctx);
    return;
  }
  if (!strcmp(sub, "export")) {
    char head[96];
    snprintf(head, sizeof(head), "# rocket-fc settings (all values), crc %08lX",
             static_cast<unsigned long>(settings_crc(edit_)));
    out(ctx, head);
    for (size_t i = 0; i < param_count(); ++i) {
      const ParamDef& d = param_def(i);
      if (!visible(d)) continue;
      char v[32];
      format_param(edit_, d, v, sizeof(v));
      snprintf(line, sizeof(line), "%s=%s", d.key, v);
      out(ctx, line);
    }
    out(ctx, "# end of settings");
    return;
  }
  if (!strcmp(sub, "get")) {
    const ParamDef* d = find_param(key);
    if (!d || !visible(*d)) return emit(out, ctx, "PERR,%s,unknown setting", key ? key : "");
    line_for(*d, line, sizeof(line));
    out(ctx, line);
    return;
  }

  const bool changes = !strcmp(sub, "set") || !strcmp(sub, "reset") || !strcmp(sub, "revert") ||
                       !strcmp(sub, "save");
  if (!changes) {
    out(ctx, "# param [list [group]] | get <key> | set <key> <value> | reset <key>|all | revert | save | export");
    return;
  }
  if (!allowed) return emit(out, ctx, "PERR,*,refused: in flight");

  if (!strcmp(sub, "set")) {
    const ParamDef* d = find_param(key);
    if (!d || !visible(*d)) return emit(out, ctx, "PERR,%s,unknown setting", key ? key : "");
    if (argc < 3) return emit(out, ctx, "PERR,%s,missing value", d->key);
    if (const char* why = set_param(edit_, *d, argv[2])) return emit(out, ctx, "PERR,%s,%s", d->key, why);
    line_for(*d, line, sizeof(line));
    out(ctx, line);
  } else if (!strcmp(sub, "reset")) {
    if (key && !strcmp(key, "all")) {
      for (size_t i = 0; i < param_count(); ++i)
        if (visible(param_def(i))) reset_param(edit_, param_def(i));
      out(ctx, "# all settings back to their defaults (not saved yet)");
    } else {
      const ParamDef* d = find_param(key);
      if (!d || !visible(*d)) return emit(out, ctx, "PERR,%s,unknown setting", key ? key : "");
      reset_param(edit_, *d);
      line_for(*d, line, sizeof(line));
      out(ctx, line);
    }
  } else if (!strcmp(sub, "revert")) {
    edit_ = saved_;
    out(ctx, "# unsaved changes discarded");
  } else {  // save
    char why[160];
    if (!check(edit_, why, sizeof(why))) return emit(out, ctx, "PERR,*,not saved: %s", why);
    // Store everything that differs from the defaults (for the ground station
    // the other settings are defaults anyway).
    const size_t n = settings_to_text(edit_, true, text, sizeof(text));
    if (n == 0 && settings_crc(edit_) != settings_crc(kDefaults)) return emit(out, ctx, "PERR,*,too large");
    if (!store(ctx, text, n)) return emit(out, ctx, "PERR,*,flash write failed");
    saved_ = edit_;
    size_t changed = 0;
    for (size_t i = 0; i < param_count(); ++i) changed += param_equal(saved_, kDefaults, param_def(i)) ? 0 : 1;
    snprintf(line, sizeof(line), "PSAVED,%08lX,%u", static_cast<unsigned long>(settings_crc(saved_)),
             static_cast<unsigned>(changed));
    out(ctx, line);
    out(ctx, "# saved: takes effect after a reboot");
  }
  summary(out, ctx);
}

}  // namespace fc
