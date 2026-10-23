// Small digital/analog I/O helpers.
#pragma once

#include <Arduino.h>
#include <driver/gpio.h>

namespace hal {

// A digital output with configurable polarity (LED, buzzer).
class DigitalOut {
 public:
  // strong: maximum pin drive (about 40 mA), for a buzzer wired straight to the pin.
  void begin(int pin, bool active_high = true, bool strong = false) {
    pin_ = pin;
    active_high_ = active_high;
    set(false);
    pinMode(pin_, OUTPUT);
    if (strong) gpio_set_drive_capability(static_cast<gpio_num_t>(pin_), GPIO_DRIVE_CAP_3);
    set(false);
  }
  void set(bool on) {
    if (pin_ < 0) return;
    if (on == state_ && initialized_) return;
    state_ = on;
    initialized_ = true;
    digitalWrite(pin_, (on == active_high_) ? HIGH : LOW);
  }
  bool get() const { return state_; }

 private:
  int pin_ = -1;
  bool active_high_ = true;
  bool state_ = false;
  bool initialized_ = false;
};

// An on/off output with adjustable brightness (LEDC PWM), for the status LED.
class PwmOut {
 public:
  void begin(int pin, uint8_t channel, uint8_t percent) {
    pin_ = pin;
    ch_ = channel;
    if (percent > 100) percent = 100;
    duty_ = static_cast<uint32_t>(percent) * 255u / 100u;
    if (duty_ == 0) duty_ = 1;
    ledcSetup(ch_, 5000, 8);
    ledcAttachPin(pin_, ch_);
    ledcWrite(ch_, 0);
  }
  void set(bool on) {
    if (pin_ < 0) return;
    if (on == state_ && initialized_) return;
    state_ = on;
    initialized_ = true;
    ledcWrite(ch_, on ? duty_ : 0);
  }
  bool get() const { return state_; }

 private:
  int pin_ = -1;
  uint8_t ch_ = 0;
  uint32_t duty_ = 255;
  bool state_ = false;
  bool initialized_ = false;
};

// Averaged, factory-calibrated ADC reading in volts (ESP32 analogReadMilliVolts),
// scaled by an external divider ratio.
class AnalogIn {
 public:
  void begin(int pin, float divider_ratio) {
    pin_ = pin;
    ratio_ = divider_ratio;
    pinMode(pin_, INPUT);
    analogSetPinAttenuation(pin_, ADC_11db);  // ~0.15..2.5 V usable range
  }
  // A pin with nothing connected reads about 0 V (not a random level): before
  // each sample the internal pull-down empties it for 10 us. analogRead then
  // switches the pull-down off again, so a connected divider (about 50 kOhm)
  // is back at its true voltage within a microsecond and reads normally.
  float read_volts(uint8_t samples = 8) {
    if (pin_ < 0 || samples == 0) return 0.0f;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < samples; ++i) {
      pinMode(pin_, INPUT_PULLDOWN);
      delayMicroseconds(10);
      sum += analogReadMilliVolts(pin_);  // sets the pin to analog mode, pulls off
    }
    return (sum / static_cast<float>(samples)) / 1000.0f * ratio_;
  }
  void set_ratio(float r) { ratio_ = r; }

 private:
  int pin_ = -1;
  float ratio_ = 1.0f;
};

}  // namespace hal
