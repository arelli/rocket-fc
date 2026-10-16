// Where flight data is recorded. Every telemetry and event frame (the same
// CRC-protected binary format that goes over LoRa, see docs/TELEMETRY.md) is
// handed to every registered sink.
//
// Adding storage later (e.g. an SD card on SPI; it needs a free chip-select pin) means
// writing one class that implements this interface and registering it in
// src/flight/app.cpp; nothing else changes.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace hal {

class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual const char* name() const = 0;
  virtual bool ok() const = 0;
  // Must not block for long: called from the 100 Hz control loop.
  virtual void write_frame(const uint8_t* frame, size_t len) = 0;
  // Called at a low rate and on landing.
  virtual void flush() {}
};

}  // namespace hal
