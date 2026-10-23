// Console output of the flight computer. Everything goes to USB serial; while
// a command received by radio runs (capture on), its output is also queued
// and sent to the ground station in TextOut frames (fc/telemetry.h).
#pragma once

#include <Arduino.h>

class ConsoleOut : public Print {
 public:
  static constexpr size_t kQueue = 8192;

  size_t write(uint8_t c) override {
    Serial.write(c);
    if (capture_) put(static_cast<char>(c));
    return 1;
  }
  size_t write(const uint8_t* b, size_t n) override {
    Serial.write(b, n);
    if (capture_)
      for (size_t i = 0; i < n; ++i) put(static_cast<char>(b[i]));
    return n;
  }

  void capture(bool on) { capture_ = on; }
  bool capturing() const { return capture_; }
  size_t pending() const { return count_; }
  bool overflowed() const { return overflow_; }

  // Up to `max` queued characters, ending at a line break when one is in
  // reach (a longer line is split). Returns the number taken.
  size_t take(char* dst, size_t max) {
    size_t n = count_ < max ? count_ : max;
    if (n == 0) return 0;
    size_t cut = 0;
    for (size_t i = 0; i < n; ++i)
      if (at(i) == '\n') cut = i + 1;
    if (cut && count_ > max) n = cut;  // keep lines whole when more follows
    for (size_t i = 0; i < n; ++i) dst[i] = at(i);
    head_ = (head_ + n) % kQueue;
    count_ -= n;
    return n;
  }

 private:
  char at(size_t i) const { return q_[(head_ + i) % kQueue]; }
  void put(char c) {
    if (c == '\r') return;
    if (count_ == kQueue) {  // full: drop the newest output
      overflow_ = true;
      return;
    }
    q_[(head_ + count_) % kQueue] = c;
    ++count_;
  }
  char q_[kQueue];
  size_t head_ = 0, count_ = 0;
  bool capture_ = false;
  bool overflow_ = false;
};

extern ConsoleOut con;  // app.cpp
