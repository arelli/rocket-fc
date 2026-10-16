// Persistent flash log: ring of sectors, sessions, power loss, wrap-around.
#include <string.h>
#include <unity.h>

#include <vector>

#include "fc/flash_log.h"
#include "fc/frame_ring.h"
#include "fc/telemetry.h"

using namespace fc;

void setUp() {}
void tearDown() {}

namespace {
// NOR flash: erase sets 0xFF, writes can only clear bits.
class SimFlash : public FlashDevice {
 public:
  explicit SimFlash(size_t sectors) : mem(sectors * FlashLog::kSector, 0x00) {}
  size_t size() const override { return mem.size(); }
  bool read(size_t off, void* buf, size_t len) override {
    if (off + len > mem.size()) return false;
    memcpy(buf, &mem[off], len);
    return true;
  }
  bool write(size_t off, const void* buf, size_t len) override {
    if (off + len > mem.size()) return false;
    const uint8_t* b = static_cast<const uint8_t*>(buf);
    size_t n = len;
    if (cut_after >= 0) {  // simulate a power loss part way through this write
      n = static_cast<size_t>(cut_after) < len ? static_cast<size_t>(cut_after) : len;
      cut_after = -1;
    }
    for (size_t i = 0; i < n; ++i) mem[off + i] &= b[i];
    ++writes;
    return true;
  }
  bool erase_sector(size_t off) override {
    if (off % FlashLog::kSector || off >= mem.size()) return false;
    memset(&mem[off], 0xFF, FlashLog::kSector);
    ++erases;
    return true;
  }
  std::vector<uint8_t> mem;
  int erases = 0, writes = 0;
  long cut_after = -1;
};

size_t event_frame(uint16_t seq, uint32_t t, uint8_t* buf) {
  tlm::Event e;
  e.seq = seq;
  e.t_ms = t;
  e.code = EventCode::Launch;
  e.arg = static_cast<int32_t>(seq);
  return tlm::encode(e, buf, 64);
}

struct Collected {
  std::vector<uint32_t> sessions;
  std::vector<uint16_t> seqs;
  int bad = 0;
};
bool collect(void* ctx, uint32_t session, const uint8_t* fr, size_t len) {
  Collected* c = static_cast<Collected*>(ctx);
  tlm::Event e;
  if (tlm::decode(fr, len, &e) != tlm::DecodeResult::Ok) {
    ++c->bad;
    return true;
  }
  c->sessions.push_back(session);
  c->seqs.push_back(e.seq);
  return true;
}
}  // namespace

static void test_sessions_and_order() {
  SimFlash f(16);  // all zeros: not a valid log, like random old contents
  FlashLog log;
  TEST_ASSERT_TRUE(log.begin(&f));
  TEST_ASSERT_EQUAL_UINT32(1, log.session());
  uint8_t buf[64];
  for (uint16_t i = 0; i < 300; ++i) TEST_ASSERT_TRUE(log.append(buf, event_frame(i, i * 10, buf)));
  // "Reboot": a new session continues on a fresh sector.
  FlashLog log2;
  TEST_ASSERT_TRUE(log2.begin(&f));
  TEST_ASSERT_EQUAL_UINT32(2, log2.session());
  TEST_ASSERT_EQUAL_UINT(1, log2.stored_sessions());
  for (uint16_t i = 0; i < 50; ++i) TEST_ASSERT_TRUE(log2.append(buf, event_frame(1000 + i, i, buf)));

  FlashLog::Session s[4];
  TEST_ASSERT_EQUAL_UINT(2, log2.list(s, 4));
  TEST_ASSERT_EQUAL_UINT32(1, s[0].session);
  TEST_ASSERT_EQUAL_UINT32(300, s[0].frames);
  TEST_ASSERT_EQUAL_UINT32(0, s[0].first_t_ms);
  TEST_ASSERT_EQUAL_UINT32(2990, s[0].last_t_ms);
  TEST_ASSERT_EQUAL_UINT32(2, s[1].session);
  TEST_ASSERT_EQUAL_UINT32(50, s[1].frames);

  Collected c;
  log2.read(1, collect, &c);
  TEST_ASSERT_EQUAL_UINT(300, c.seqs.size());
  for (uint16_t i = 0; i < 300; ++i) TEST_ASSERT_EQUAL_UINT16(i, c.seqs[i]);
  Collected all;
  log2.read(0, collect, &all);
  TEST_ASSERT_EQUAL_UINT(350, all.seqs.size());
  TEST_ASSERT_EQUAL_UINT16(1049, all.seqs.back());
  TEST_ASSERT_EQUAL_INT(0, all.bad);
}

static void test_wraps_and_keeps_newest() {
  SimFlash f(8);
  FlashLog log;
  log.begin(&f);
  uint8_t buf[64];
  // 8 sectors x ~240 events = ~1900 frames fit; write 5000 in 3 sessions.
  uint16_t seq = 0;
  for (int boot = 0; boot < 3; ++boot) {
    if (boot) log.begin(&f);
    for (int i = 0; i < 1700; ++i, ++seq) TEST_ASSERT_TRUE(log.append(buf, event_frame(seq, seq, buf)));
  }
  Collected c;
  log.read(0, collect, &c);
  TEST_ASSERT_TRUE(c.seqs.size() > 1000);
  TEST_ASSERT_EQUAL_UINT16(seq - 1, c.seqs.back());
  for (size_t i = 1; i < c.seqs.size(); ++i) TEST_ASSERT_EQUAL_UINT16(c.seqs[i - 1] + 1, c.seqs[i]);
  TEST_ASSERT_EQUAL_UINT32(3, c.sessions.back());
}

static void test_power_loss_mid_record() {
  SimFlash f(8);
  FlashLog log;
  log.begin(&f);
  uint8_t buf[64];
  for (uint16_t i = 0; i < 10; ++i) log.append(buf, event_frame(i, i, buf));
  f.cut_after = 7;  // the 11th record is cut after 7 of its 18 bytes
  log.append(buf, event_frame(10, 10, buf));
  FlashLog log2;  // power back on
  log2.begin(&f);
  for (uint16_t i = 20; i < 25; ++i) log2.append(buf, event_frame(i, i, buf));
  Collected c;
  log2.read(0, collect, &c);
  TEST_ASSERT_EQUAL_INT(1, c.bad);  // the cut record is detected by its CRC
  TEST_ASSERT_EQUAL_UINT(15, c.seqs.size());
  TEST_ASSERT_EQUAL_UINT16(9, c.seqs[9]);
  TEST_ASSERT_EQUAL_UINT16(20, c.seqs[10]);
}

static void test_prepare_ahead_avoids_erase_later() {
  SimFlash f(16);
  FlashLog log;
  log.begin(&f);
  TEST_ASSERT_TRUE(log.needs_erase(20));
  int n = 0;
  while (log.prepare_ahead(4)) ++n;
  TEST_ASSERT_EQUAL_INT(4, n);
  const int erases = f.erases;
  uint8_t buf[64];
  // About 4 sectors of records: no erase while appending them.
  for (uint16_t i = 0; i < 900; ++i) {
    TEST_ASSERT_FALSE(log.needs_erase(18));
    log.append(buf, event_frame(i, i, buf));
  }
  TEST_ASSERT_EQUAL_INT(erases, f.erases);
  Collected c;
  log.read(0, collect, &c);
  TEST_ASSERT_EQUAL_UINT(900, c.seqs.size());
}

static void test_erase_all_and_garbage() {
  SimFlash f(8);
  memset(f.mem.data(), 0xA5, f.mem.size());  // random old contents
  FlashLog log;
  TEST_ASSERT_TRUE(log.begin(&f));
  FlashLog::Session s[2];
  TEST_ASSERT_EQUAL_UINT(0, log.list(s, 2));
  uint8_t buf[64];
  log.append(buf, event_frame(1, 1, buf));
  TEST_ASSERT_EQUAL_UINT(1, log.list(s, 2));
  TEST_ASSERT_TRUE(log.erase_all(nullptr, nullptr));
  TEST_ASSERT_EQUAL_UINT(0, log.list(s, 2));
  log.append(buf, event_frame(2, 2, buf));
  TEST_ASSERT_EQUAL_UINT(1, log.list(s, 2));
  TEST_ASSERT_FALSE(log.append(buf, 0));
  SimFlash tiny(2);
  FlashLog t;
  TEST_ASSERT_FALSE(t.begin(&tiny));
  TEST_ASSERT_FALSE(t.append(buf, 10));
}

static void test_frame_ring_pop() {
  static uint8_t mem[64 * 4];
  FrameRing r;
  r.attach(mem, sizeof(mem));
  uint8_t buf[64], out[64];
  for (uint16_t i = 0; i < 6; ++i) r.push(buf, event_frame(i, i, buf));  // 2 overwritten
  TEST_ASSERT_EQUAL_UINT(4, r.count());
  tlm::Event e;
  const size_t n = r.pop(out, sizeof(out));
  TEST_ASSERT_EQUAL(tlm::DecodeResult::Ok, tlm::decode(out, n, &e));
  TEST_ASSERT_EQUAL_UINT16(2, e.seq);
  TEST_ASSERT_EQUAL_UINT(3, r.count());
  while (r.pop(out, sizeof(out))) {}
  TEST_ASSERT_EQUAL_UINT(0, r.pop(out, sizeof(out)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sessions_and_order);
  RUN_TEST(test_wraps_and_keeps_newest);
  RUN_TEST(test_power_loss_mid_record);
  RUN_TEST(test_prepare_ahead_avoids_erase_later);
  RUN_TEST(test_erase_all_and_garbage);
  RUN_TEST(test_frame_ring_pop);
  return UNITY_END();
}
