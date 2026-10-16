#include "hal/gps_uart.h"

#include <driver/gpio.h>

namespace hal {

void GpsUart::begin(int rx_pin, uint32_t baud) {
  Serial1.setRxBufferSize(1024);  // ~1 s of NMEA at 9600 baud: survives a slow loop
  Serial1.begin(baud, SERIAL_8N1, rx_pin, -1);
  gpio_pullup_en(static_cast<gpio_num_t>(rx_pin));
  started_ = true;
}

}  // namespace hal
