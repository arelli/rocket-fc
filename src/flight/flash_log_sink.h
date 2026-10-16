// The persistent flight log: frames are queued in RAM (write_frame never
// touches the flash) and written to the "flog" flash partition by service(),
// which the App calls only when a flash stall is harmless.
#pragma once

#include <Arduino.h>
#include <esp32-hal-psram.h>

#include "fc/flash_log.h"
#include "fc/frame_ring.h"
#include "hal/log_sink.h"
#include "hal/partition_flash.h"

class FlashLogSink : public hal::LogSink {
 public:
  // Queue: 512 KB of PSRAM = 8192 frames, about 2.7 min at the 50 Hz flight rate.
  bool begin(size_t queue_bytes = 512u * 1024u) {
    if (!flash_.begin("flog") || !log_.begin(&flash_)) return false;
    uint8_t* mem = psramFound() ? static_cast<uint8_t*>(ps_malloc(queue_bytes)) : nullptr;
    if (!mem) mem = static_cast<uint8_t*>(malloc(queue_bytes = 16u * 1024u));
    queue_.attach(mem, mem ? queue_bytes : 0);
    return queue_.ok();
  }

  const char* name() const override { return "flash-log"; }
  bool ok() const override { return log_.ok() && queue_.ok(); }
  void write_frame(const uint8_t* frame, size_t len) override { queue_.push(frame, len); }

  // Writes queued frames for up to budget_us. may_erase: a sector erase
  // (tens of ms) is acceptable now. Returns the number of frames written.
  size_t service(uint32_t budget_us, bool may_erase) {
    if (!ok()) return 0;
    const uint32_t t0 = micros();
    size_t n = 0;
    uint8_t buf[fc::FrameRing::kSlot];
    while (queue_.count() > 0 && micros() - t0 < budget_us) {
      // Peek the length to see whether this append would erase.
      const size_t len = queue_.get(0, buf, sizeof(buf));
      if (!may_erase && log_.needs_erase(len)) break;
      queue_.pop(buf, sizeof(buf));
      log_.append(buf, len);
      ++n;
    }
    return n;
  }
  // Erase one sector ahead if fewer than `want` are ready. Returns true if it did.
  bool prepare(size_t want) { return ok() && log_.prepare_ahead(want); }

  fc::FlashLog& log() { return log_; }
  size_t backlog() const { return queue_.count(); }
  uint32_t dropped() const { return queue_.overwritten(); }

 private:
  hal::PartitionFlash flash_;
  fc::FlashLog log_;
  fc::FrameRing queue_;
};
