// Persistent flight log in the processor's own flash (survives power-off).
//
// The log is a ring of 4 KB sectors in a dedicated flash partition. Each boot
// is a new "session" that starts on a fresh sector. A sector holds:
//   header (16 bytes): u32 magic "RFL1" | u32 sector sequence | u32 session |
//                      u16 0xFFFF | u16 CRC-16 of the first 14 bytes
//   records: u8 length (1..254) + one telemetry/event/GPS frame (each frame
//            has its own CRC, so a record cut by a power loss is detected)
//   0xFF from the first unused byte to the end
// Sector sequence numbers increase by one per sector across boots; the oldest
// sectors are overwritten when the partition is full.
//
// Writing flash stalls the processor (an erase takes tens of milliseconds),
// so the firmware calls append() only when that is harmless (never between
// launch and deployment, see App::update_flash_log), and erases sectors ahead
// of time on the pad (prepare_ahead).
//
// Pure logic over a FlashDevice interface: tested on the PC with a simulated
// NOR flash (test/test_flash_log).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fc {

class FlashDevice {
 public:
  virtual ~FlashDevice() = default;
  virtual size_t size() const = 0;  // bytes, a multiple of 4096
  virtual bool read(size_t offset, void* buf, size_t len) = 0;
  // NOR semantics: writing can only clear bits (1 -> 0).
  virtual bool write(size_t offset, const void* buf, size_t len) = 0;
  virtual bool erase_sector(size_t offset) = 0;  // 4096 bytes -> 0xFF
};

class FlashLog {
 public:
  static constexpr size_t kSector = 4096;
  static constexpr size_t kHeader = 16;
  static constexpr uint32_t kMagic = 0x314C4652u;  // "RFL1"
  static constexpr size_t kMaxRecord = 254;

  struct Session {
    uint32_t session = 0;
    uint32_t sectors = 0;
    uint32_t frames = 0;
    uint32_t first_t_ms = 0, last_t_ms = 0;  // from the frames' headers
    uint32_t bytes = 0;
  };

  // Scans the device: finds the newest sector and the next session number.
  // Returns false if the device is unusable (too small, read error).
  bool begin(FlashDevice* dev);
  bool ok() const { return dev_ != nullptr; }

  uint32_t session() const { return session_; }    // this boot's session
  // Sessions found at begin() (approximate if old ones were partly overwritten).
  size_t stored_sessions() const { return stored_sessions_; }
  size_t sectors() const { return n_sectors_; }
  uint32_t bytes_written() const { return bytes_written_; }  // this boot
  uint32_t errors() const { return errors_; }

  // Stores one frame. May erase a sector (when none was prepared ahead).
  bool append(const uint8_t* frame, size_t len);
  // Would the next append() have to erase a sector?
  bool needs_erase(size_t len) const;
  // Erases up to `want` sectors ahead of the write position (one per call,
  // returns true if it erased one) so later appends do not have to.
  bool prepare_ahead(size_t want);
  size_t erased_ahead() const { return erased_ahead_; }

  // Erases everything (calls progress(ctx) between sectors). Starts a new session.
  bool erase_all(void (*progress)(void*), void* ctx);

  // Sessions, oldest first. Returns how many were found (fills up to cap).
  size_t list(Session* out, size_t cap);
  // Calls visit for every record of `session` (0 = all sessions), oldest first.
  // Stops early if visit returns false.
  using Visit = bool (*)(void* ctx, uint32_t session, const uint8_t* frame, size_t len);
  void read(uint32_t session, Visit visit, void* ctx);

 private:
  struct Header {
    uint32_t seq = 0, session = 0;
    bool valid = false;
  };
  Header read_header(size_t sector);
  bool start_sector(size_t sector);
  size_t next(size_t s) const { return (s + 1) % n_sectors_; }
  template <typename F>
  void walk(F&& f);  // every valid sector, oldest first

  FlashDevice* dev_ = nullptr;
  size_t n_sectors_ = 0;
  uint32_t session_ = 1;
  uint32_t next_seq_ = 1;
  size_t cur_ = 0;          // sector being written
  size_t pos_ = 0;          // write offset inside it (0 = not started this boot)
  bool have_cur_ = false;
  size_t newest_ = 0;       // newest valid sector (for reading)
  bool have_newest_ = false;
  size_t erased_ahead_ = 0;  // sectors after cur_ erased this boot
  size_t stored_sessions_ = 0;
  uint32_t bytes_written_ = 0, errors_ = 0;
};

}  // namespace fc
