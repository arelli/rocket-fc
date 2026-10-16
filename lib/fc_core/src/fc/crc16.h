// CRC-16/CCITT-FALSE, used to protect every telemetry/log frame.
//   poly 0x1021, init 0xFFFF, no input/output reflection, xorout 0x0000
//   check value: crc16_ccitt("123456789") == 0x29B1
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fc {

uint16_t crc16_ccitt(const uint8_t* data, size_t len, uint16_t crc = 0xFFFF);

}  // namespace fc
