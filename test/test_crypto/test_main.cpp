// SHA-256 and X25519 against published / independent vectors, and the WiFi
// password sealing used over the radio (fc/crypto.h). The X25519 pairs were
// produced with the Python "cryptography" package (OpenSSL).
#include <string.h>
#include <unity.h>

#include "fc/crypto.h"

using namespace fc;

void setUp() {}
void tearDown() {}

namespace {
void hex32(const char* h, uint8_t* out) { TEST_ASSERT_EQUAL(32, from_hex(h, out, 32)); }
void expect(const char* hex, const uint8_t* got, size_t n) {
  char s[2 * 64 + 1];
  to_hex(got, n, s);
  TEST_ASSERT_EQUAL_STRING(hex, s);
}
}  // namespace

static void test_sha256() {
  uint8_t h[32];
  sha256(reinterpret_cast<const uint8_t*>("abc"), 3, h);
  expect("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", h, 32);
  sha256(nullptr, 0, h);
  expect("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", h, 32);
  const char* m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  sha256(reinterpret_cast<const uint8_t*>(m), strlen(m), h);
  expect("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", h, 32);
  uint8_t a[1000];
  memset(a, 'a', sizeof(a));
  Sha256 s;  // in odd-sized pieces
  for (size_t off = 0; off < sizeof(a); off += 37) s.update(a + off, sizeof(a) - off < 37 ? sizeof(a) - off : 37);
  s.final(h);
  expect("41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3", h, 32);
}

static void test_x25519_rfc7748() {
  uint8_t k[32], u[32], out[32];
  // RFC 7748 section 5.2, first vector.
  hex32("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", k);
  hex32("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", u);
  x25519(out, k, u);
  expect("c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", out, 32);
  // RFC 7748 section 6.1: Alice's public key.
  hex32("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", k);
  x25519_public(out, k);
  expect("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", out, 32);
}

static void test_x25519_exchange() {
  static const char* const v[][5] = {
      {"05101b26313c47525d68737e89949faab5c0cbd6e1ecf7020d18232e39444f5a", "14ee0e07f34e3a17030c6eecf66d3705e80d1e153f7a0ca5b345e83ce9d62643", "030a11181f262d343b424950575e656c737a81888f969da4abb2b9c0c7ced5dc", "bb50ff9e82a574cfbf820e97f60fb9c143ec7415cf514f8cfd98eff59e059614", "32dfa776c16b37746565aff7031b504c4208d9a0e4b23076a0887249aaf7b50e"},
      {"2a35404b56616c77828d98a3aeb9c4cfdae5f0fb06111c27323d48535e69747f", "c9bdf9835eebc9fa43fd203d078a2fab27a0214669ceed435be0dc59b61e1f0b", "5e656c737a81888f969da4abb2b9c0c7ced5dce3eaf1f8ff060d141b22293037", "618532d7afd1977f04d3378c6e4ab78ec3509c67acd7019cc2eff507b3bdcd6d", "d22eda6f485fd97e67fd8bf0582d4987a2c38ed943987a3f30da3a62c689b24e"},
      {"4f5a65707b86919ca7b2bdc8d3dee9f4ff0a15202b36414c57626d78838e99a4", "148f596db540bde69533716b51127e24a322d90fadff1454c173e703ecf74670", "b9c0c7ced5dce3eaf1f8ff060d141b222930373e454c535a61686f767d848b92", "1476ae402e7cd6c65f31241887f79abc4f7a9986aa8167b30506b9c9aea9724b", "dd4c95ac38c3827041cc70cb0b9515a16c36c7ebf8fb94e55ba51ea004948040"},
  };
  for (const auto& c : v) {
    uint8_t a[32], b[32], pa[32], pb[32], s1[32], s2[32];
    hex32(c[0], a);
    hex32(c[2], b);
    x25519_public(pa, a);
    x25519_public(pb, b);
    expect(c[1], pa, 32);
    expect(c[3], pb, 32);
    x25519(s1, a, pb);
    x25519(s2, b, pa);
    expect(c[4], s1, 32);
    expect(c[4], s2, 32);
  }
}

static void test_wifi_sealing() {
  uint8_t shared[32], key[32];
  hex32("32dfa776c16b37746565aff7031b504c4208d9a0e4b23076a0887249aaf7b50e", shared);
  wifi_secret::derive_key(shared, 0x6B1D4F37u, key);
  expect("0ccc1aa102921423938026ab1c95f4a44ae01622e5819d33e4581ba2e8ba7c29", key, 32);
  const char* pw = "abcd2345xy";
  uint8_t ct[64], back[64], tag[wifi_secret::kTag];
  wifi_secret::crypt(key, reinterpret_cast<const uint8_t*>(pw), ct, strlen(pw));
  expect("02fd576131cc50331777", ct, strlen(pw));
  wifi_secret::tag(key, "rocketfc-1A2B", ct, strlen(pw), tag);
  expect("d4187ba7b0acec57", tag, wifi_secret::kTag);
  wifi_secret::crypt(key, ct, back, strlen(pw));
  TEST_ASSERT_EQUAL_MEMORY(pw, back, strlen(pw));
  // Another rf_key gives another key (and so a failing tag).
  uint8_t key2[32];
  wifi_secret::derive_key(shared, 0x6B1D4F38u, key2);
  TEST_ASSERT_TRUE(memcmp(key, key2, 32) != 0);
  // Longest password (63 bytes, two keystream blocks) round-trips.
  char longpw[64];
  memset(longpw, 'Z', 63);
  longpw[63] = '\0';
  wifi_secret::crypt(key, reinterpret_cast<const uint8_t*>(longpw), ct, 63);
  wifi_secret::crypt(key, ct, back, 63);
  TEST_ASSERT_EQUAL_MEMORY(longpw, back, 63);
}

static void test_hex() {
  uint8_t b[4];
  TEST_ASSERT_EQUAL(2, from_hex("aB0f", b, 4));
  TEST_ASSERT_EQUAL_HEX8(0xAB, b[0]);
  TEST_ASSERT_EQUAL(0, from_hex("abc", b, 4));     // odd
  TEST_ASSERT_EQUAL(0, from_hex("zz", b, 4));      // not hex
  TEST_ASSERT_EQUAL(0, from_hex("0011223344", b, 4));  // too long
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sha256);
  RUN_TEST(test_x25519_rfc7748);
  RUN_TEST(test_x25519_exchange);
  RUN_TEST(test_wifi_sealing);
  RUN_TEST(test_hex);
  return UNITY_END();
}
