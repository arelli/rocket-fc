// SX1262 LoRa link (RadioLib), non-blocking, half duplex. Both ends transmit
// and receive: telemetry rocket -> ground, heartbeats ground -> rocket.
#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>

namespace hal {

class RadioLink {
 public:
  struct Pins {
    int sck, miso, mosi, nss, dio1, reset, busy, rf_switch;
  };
  struct Params {
    float freq_mhz;
    float bw_khz;
    uint8_t sf;
    uint8_t cr_denom;
    uint8_t sync_word;
    int8_t power_dbm;
    uint16_t preamble;
    float tcxo_v;
  };

  RadioLink() = default;
  RadioLink(const RadioLink&) = delete;
  RadioLink& operator=(const RadioLink&) = delete;

  // Returns true on success; last_error() holds the RadioLib status code.
  bool begin(const Pins& pins, const Params& params);

  // --- Transmit ------------------------------------------------------------
  // Starts sending; returns false if busy or on error.
  bool send(const uint8_t* data, size_t len);
  bool busy() const { return tx_busy_; }
  // RadioLib's own time-on-air figure (cross-check of fc::lora_airtime_us).
  uint32_t time_on_air_us(size_t len);

  // --- Receive ---------------------------------------------------------------
  bool start_receive();
  // If a packet arrived, copies it out and returns its length (0 = none).
  size_t receive(uint8_t* buf, size_t cap, float* rssi_dbm, float* snr_db, bool* crc_error);
  // Instantaneous channel RSSI while listening (the noise floor when nobody
  // transmits). Returns NAN when not receiving.
  float channel_rssi_dbm();

  // Return to receive mode automatically after every transmission.
  void set_listen(bool on) { listen_ = on; }
  // millis() when the last transmission ended (0 = none yet).
  uint32_t tx_done_ms() const { return tx_done_ms_; }

  // Call every loop iteration (finishes transmissions, handles timeouts).
  void poll();

  void sleep();

  bool ok() const { return ok_; }
  int16_t last_error() const { return last_error_; }
  uint32_t tx_count() const { return tx_count_; }
  uint32_t tx_errors() const { return tx_errors_; }

 private:
  static void IRAM_ATTR on_dio1();
  static volatile bool s_irq_;

  SPIClass* spi_ = nullptr;
  Module* module_ = nullptr;
  SX1262* radio_ = nullptr;
  bool ok_ = false;
  bool rx_mode_ = false;
  bool tx_busy_ = false;
  bool listen_ = false;
  uint32_t tx_done_ms_ = 0;
  uint32_t tx_start_ms_ = 0;
  uint32_t tx_timeout_ms_ = 0;
  int16_t last_error_ = 0;
  uint32_t tx_count_ = 0;
  uint32_t tx_errors_ = 0;
};

}  // namespace hal
