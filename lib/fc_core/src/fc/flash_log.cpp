#include "fc/flash_log.h"

#include <string.h>

#include "fc/crc16.h"

namespace fc {

namespace {

uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void put32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

// One sector buffer for reading (the log is used from one task only).
uint8_t g_sector[FlashLog::kSector];

// Records of a sector buffer: calls f(frame, len) for each.
template <typename F>
void records(const uint8_t* s, F&& f) {
  size_t p = FlashLog::kHeader;
  while (p < FlashLog::kSector) {
    const size_t len = s[p];
    if (len == 0xFF || len == 0 || p + 1 + len > FlashLog::kSector) break;
    if (!f(s + p + 1, len)) return;
    p += 1 + len;
  }
}

}  // namespace

FlashLog::Header FlashLog::read_header(size_t sector) {
  Header h;
  uint8_t b[kHeader];
  if (!dev_->read(sector * kSector, b, sizeof(b))) return h;
  if (le32(b) != kMagic) return h;
  const uint16_t crc = static_cast<uint16_t>(b[14] | (b[15] << 8));
  if (crc16_ccitt(b, 14) != crc) return h;
  h.seq = le32(b + 4);
  h.session = le32(b + 8);
  h.valid = true;
  return h;
}

bool FlashLog::begin(FlashDevice* dev) {
  dev_ = nullptr;
  if (!dev || dev->size() < 4 * kSector) return false;
  dev_ = dev;
  n_sectors_ = dev->size() / kSector;
  uint32_t max_seq = 0, max_session = 0, min_session = 0xFFFFFFFFu;
  have_newest_ = false;
  for (size_t s = 0; s < n_sectors_; ++s) {
    const Header h = read_header(s);
    if (!h.valid) continue;
    if (!have_newest_ || h.seq > max_seq) {
      max_seq = h.seq;
      newest_ = s;
      have_newest_ = true;
    }
    if (h.session > max_session) max_session = h.session;
    if (h.session < min_session) min_session = h.session;
  }
  stored_sessions_ = have_newest_ ? max_session - min_session + 1 : 0;
  session_ = max_session + 1;
  next_seq_ = max_seq + 1;
  cur_ = have_newest_ ? next(newest_) : 0;  // every boot starts a fresh sector
  have_cur_ = false;
  pos_ = 0;
  erased_ahead_ = 0;
  bytes_written_ = 0;
  return true;
}

bool FlashLog::needs_erase(size_t len) const {
  if (!dev_) return false;
  const bool new_sector = !have_cur_ || pos_ + 1 + len > kSector;
  return new_sector && erased_ahead_ == 0;
}

bool FlashLog::start_sector(size_t s) {
  if (erased_ahead_ > 0) {
    --erased_ahead_;
  } else if (!dev_->erase_sector(s * kSector)) {
    ++errors_;
    return false;
  }
  uint8_t h[kHeader];
  put32(h, kMagic);
  put32(h + 4, next_seq_);
  put32(h + 8, session_);
  h[12] = h[13] = 0xFF;
  const uint16_t crc = crc16_ccitt(h, 14);
  h[14] = static_cast<uint8_t>(crc);
  h[15] = static_cast<uint8_t>(crc >> 8);
  if (!dev_->write(s * kSector, h, sizeof(h))) {
    ++errors_;
    return false;
  }
  ++next_seq_;
  cur_ = s;
  pos_ = kHeader;
  have_cur_ = true;
  newest_ = s;
  have_newest_ = true;
  return true;
}

bool FlashLog::append(const uint8_t* frame, size_t len) {
  if (!dev_ || len == 0 || len > kMaxRecord) return false;
  if (!have_cur_ || pos_ + 1 + len > kSector) {
    const size_t s = have_cur_ ? next(cur_) : cur_;
    if (!start_sector(s)) return false;
  }
  uint8_t rec[kMaxRecord + 1];
  rec[0] = static_cast<uint8_t>(len);  // length first: a cut record is skipped whole
  memcpy(rec + 1, frame, len);
  if (!dev_->write(cur_ * kSector + pos_, rec, len + 1)) {
    ++errors_;
    pos_ = kSector;  // do not write over a failed area: continue in the next sector
    return false;
  }
  pos_ += len + 1;
  bytes_written_ += static_cast<uint32_t>(len + 1);
  return true;
}

bool FlashLog::prepare_ahead(size_t want) {
  if (!dev_) return false;
  if (want > n_sectors_ - 2) want = n_sectors_ - 2;
  if (erased_ahead_ >= want) return false;
  const size_t first = have_cur_ ? next(cur_) : cur_;
  const size_t s = (first + erased_ahead_) % n_sectors_;
  if (!dev_->erase_sector(s * kSector)) {
    ++errors_;
    return false;
  }
  ++erased_ahead_;
  // The erased sector may have been the oldest valid one: if it was the
  // newest-known, nothing older remains to read past it (handled by walk()).
  return true;
}

bool FlashLog::erase_all(void (*progress)(void*), void* ctx) {
  if (!dev_) return false;
  bool ok = true;
  for (size_t s = 0; s < n_sectors_; ++s) {
    ok &= dev_->erase_sector(s * kSector);
    if (progress) progress(ctx);
  }
  have_newest_ = false;
  have_cur_ = false;
  cur_ = 0;
  pos_ = 0;
  erased_ahead_ = n_sectors_ - 2;
  return ok;
}

template <typename F>
void FlashLog::walk(F&& f) {
  if (!dev_ || !have_newest_) return;
  // Sectors are written in physical order (wrapping), so the oldest valid
  // sector follows the newest one.
  size_t s = next(newest_);
  for (size_t i = 0; i < n_sectors_; ++i, s = next(s)) {
    const Header h = read_header(s);
    if (h.valid && !f(s, h)) return;
  }
}

size_t FlashLog::list(Session* out, size_t cap) {
  size_t n = 0;
  Session cur;
  bool open = false;
  walk([&](size_t s, const Header& h) {
    if (!open || h.session != cur.session) {
      if (open) {
        if (n < cap) out[n] = cur;
        ++n;
      }
      cur = Session{};
      cur.session = h.session;
      open = true;
    }
    ++cur.sectors;
    if (!dev_->read(s * kSector, g_sector, kSector)) return true;
    records(g_sector, [&](const uint8_t* fr, size_t len) {
      if (len >= 10) {
        const uint32_t t = le32(fr + 6);
        if (cur.frames == 0) cur.first_t_ms = t;
        cur.last_t_ms = t;
      }
      ++cur.frames;
      cur.bytes += static_cast<uint32_t>(len);
      return true;
    });
    return true;
  });
  if (open) {
    if (n < cap) out[n] = cur;
    ++n;
  }
  return n;
}

void FlashLog::read(uint32_t session, Visit visit, void* ctx) {
  bool stop = false;
  walk([&](size_t s, const Header& h) {
    if (session != 0 && h.session != session) return true;
    if (!dev_->read(s * kSector, g_sector, kSector)) return true;
    records(g_sector, [&](const uint8_t* fr, size_t len) {
      if (!visit(ctx, h.session, fr, len)) stop = true;
      return !stop;
    });
    return !stop;
  });
}

}  // namespace fc
