#include "hal/radio_link.h"

namespace hal {

volatile bool RadioLink::s_irq_ = false;

void IRAM_ATTR RadioLink::on_dio1() { s_irq_ = true; }

bool RadioLink::begin(const Pins& p, const Params& r) {
  spi_ = &SPI;
  spi_->begin(p.sck, p.miso, p.mosi, p.nss);
  module_ = new Module(p.nss, p.dio1, p.reset, p.busy, *spi_);
  radio_ = new SX1262(module_);

  last_error_ = radio_->begin(r.freq_mhz, r.bw_khz, r.sf, r.cr_denom, r.sync_word, r.power_dbm,
                              r.preamble, r.tcxo_v, false);
  if (last_error_ != RADIOLIB_ERR_NONE) return ok_ = false;

  // RF switch: DIO2 drives the antenna switch, GPIO38 enables the RX path
  // (same as Seeed's / Meshtastic's configuration for this kit).
  last_error_ = radio_->setDio2AsRfSwitch(true);
  if (last_error_ != RADIOLIB_ERR_NONE) return ok_ = false;
  if (p.rf_switch >= 0) radio_->setRfSwitchPins(p.rf_switch, RADIOLIB_NC);

  last_error_ = radio_->setCRC(2);  // 16-bit LoRa CRC
  if (last_error_ != RADIOLIB_ERR_NONE) return ok_ = false;
  last_error_ = radio_->setRxBoostedGainMode(r.rx_boost);
  if (last_error_ != RADIOLIB_ERR_NONE) return ok_ = false;

  radio_->setDio1Action(on_dio1);
  s_irq_ = false;
  return ok_ = true;
}

uint32_t RadioLink::time_on_air_us(size_t len) {
  return ok_ ? static_cast<uint32_t>(radio_->getTimeOnAir(len)) : 0;
}

bool RadioLink::send(const uint8_t* data, size_t len) {
  if (!ok_ || tx_busy_) return false;
  s_irq_ = false;
  last_error_ = radio_->startTransmit(const_cast<uint8_t*>(data), len);
  if (last_error_ != RADIOLIB_ERR_NONE) {
    ++tx_errors_;
    return false;
  }
  rx_mode_ = false;
  tx_busy_ = true;
  tx_start_ms_ = millis();
  tx_timeout_ms_ = time_on_air_us(len) / 1000 + 100;
  return true;
}

void RadioLink::poll() {
  if (!ok_ || !tx_busy_) return;
  if (s_irq_) {
    s_irq_ = false;
    radio_->finishTransmit();
    tx_busy_ = false;
    ++tx_count_;
  } else if (millis() - tx_start_ms_ > tx_timeout_ms_) {
    radio_->finishTransmit();  // missed IRQ: abort and carry on
    tx_busy_ = false;
    ++tx_errors_;
  } else {
    return;
  }
  tx_done_ms_ = millis();
  if (tx_done_ms_ == 0) tx_done_ms_ = 1;
  if (listen_) start_receive();
}

bool RadioLink::start_receive() {
  if (!ok_) return false;
  s_irq_ = false;
  last_error_ = radio_->startReceive();
  rx_mode_ = (last_error_ == RADIOLIB_ERR_NONE);
  return rx_mode_;
}

size_t RadioLink::receive(uint8_t* buf, size_t cap, float* rssi, float* snr, bool* crc_error) {
  if (!ok_ || !rx_mode_ || !s_irq_) return 0;
  s_irq_ = false;
  size_t len = radio_->getPacketLength();
  if (len > cap) len = cap;
  const int16_t st = radio_->readData(buf, len);
  *crc_error = (st == RADIOLIB_ERR_CRC_MISMATCH);
  *rssi = radio_->getRSSI();
  *snr = radio_->getSNR();
  last_error_ = st;
  start_receive();  // keep listening
  if (st != RADIOLIB_ERR_NONE && st != RADIOLIB_ERR_CRC_MISMATCH) return 0;
  return len;
}

float RadioLink::channel_rssi_dbm() {
  if (!ok_ || !rx_mode_) return NAN;
  return radio_->getRSSI(false);
}

void RadioLink::sleep() {
  if (ok_) radio_->sleep();
}

}  // namespace hal
