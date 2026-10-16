// Receive-only serial port for the optional GPS module (UART1, any RX pin).
// The module's own RX input is not used: its default NMEA output is all the
// firmware needs.
#pragma once

#include <Arduino.h>

namespace hal {

class GpsUart {
 public:
  // Starts UART1 on rx_pin. The pin gets a pull-up so an unconnected input
  // stays quiet instead of producing noise.
  void begin(int rx_pin, uint32_t baud);
  bool started() const { return started_; }
  // Next received byte; false if none waiting.
  bool read(char* c) {
    if (!started_ || Serial1.available() <= 0) return false;
    const int v = Serial1.read();
    if (v < 0) return false;
    *c = static_cast<char>(v);
    return true;
  }

 private:
  bool started_ = false;
};

}  // namespace hal
