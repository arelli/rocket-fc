// Onboard log of the current boot in PSRAM (the flash log keeps it after power-off).
//
// Keeps the most recent frames (2 MB = 32,768 frames: about 11 minutes at
// the 50 Hz flight rate) while powered ("dump"). The flash log (flash_log_sink.h)
// keeps them after power-off ("flog").
#pragma once

#include <Arduino.h>
#include <esp32-hal-psram.h>

#include "fc/frame_ring.h"
#include "hal/log_sink.h"

class RamLog : public hal::LogSink {
 public:
  // Tries PSRAM first, then a small buffer in internal RAM.
  void begin(size_t psram_bytes = 2u * 1024u * 1024u, size_t fallback_bytes = 48u * 1024u) {
    uint8_t* mem = nullptr;
    size_t bytes = 0;
    if (psramFound()) {
      mem = static_cast<uint8_t*>(ps_malloc(psram_bytes));
      if (mem) bytes = psram_bytes;
    }
    if (!mem) {
      mem = static_cast<uint8_t*>(malloc(fallback_bytes));
      if (mem) bytes = fallback_bytes;
    }
    in_psram_ = mem && bytes == psram_bytes;
    ring_.attach(mem, bytes);
  }

  const char* name() const override { return in_psram_ ? "ram-log(psram)" : "ram-log(internal)"; }
  bool ok() const override { return ring_.ok(); }
  void write_frame(const uint8_t* frame, size_t len) override { ring_.push(frame, len); }

  fc::FrameRing& ring() { return ring_; }
  const fc::FrameRing& ring() const { return ring_; }

 private:
  fc::FrameRing ring_;
  bool in_psram_ = false;
};
