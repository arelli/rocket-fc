// fc::FlashDevice on an ESP32 flash partition (esp_partition API).
// Every write and erase stalls the processor while it runs (an erase takes
// tens of milliseconds): the caller decides when that is acceptable.
#pragma once

#include <esp_partition.h>

#include "fc/flash_log.h"

namespace hal {

class PartitionFlash : public fc::FlashDevice {
 public:
  // Finds the data partition by name (subtype 0x40). Returns false if absent.
  bool begin(const char* label) {
    part_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), label);
    return part_ != nullptr;
  }
  size_t size() const override { return part_ ? (part_->size / fc::FlashLog::kSector) * fc::FlashLog::kSector : 0; }
  bool read(size_t off, void* buf, size_t len) override {
    return part_ && esp_partition_read(part_, off, buf, len) == ESP_OK;
  }
  bool write(size_t off, const void* buf, size_t len) override {
    return part_ && esp_partition_write(part_, off, buf, len) == ESP_OK;
  }
  bool erase_sector(size_t off) override {
    return part_ && esp_partition_erase_range(part_, off, fc::FlashLog::kSector) == ESP_OK;
  }

 private:
  const esp_partition_t* part_ = nullptr;
};

}  // namespace hal
