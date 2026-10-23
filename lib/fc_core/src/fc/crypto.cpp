#include "fc/crypto.h"

#include <string.h>

namespace fc {

// ------------------------------------------------------------------ SHA-256 (FIPS 180-4)
namespace {
constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
}  // namespace

Sha256::Sha256()
    : h_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::block(const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i)
    w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) | (uint32_t(p[4 * i + 2]) << 8) | p[4 * i + 3];
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
    const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
}

void Sha256::update(const uint8_t* data, size_t len) {
  total_ += len;
  while (len--) {
    buf_[n_++] = *data++;
    if (n_ == 64) {
      block(buf_);
      n_ = 0;
    }
  }
}

void Sha256::final(uint8_t out[32]) {
  const uint64_t bits = total_ * 8;
  const uint8_t one = 0x80, zero = 0;
  update(&one, 1);
  while (n_ != 56) update(&zero, 1);
  uint8_t len[8];
  for (int i = 0; i < 8; ++i) len[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
  update(len, 8);
  for (int i = 0; i < 8; ++i) {
    out[4 * i] = static_cast<uint8_t>(h_[i] >> 24);
    out[4 * i + 1] = static_cast<uint8_t>(h_[i] >> 16);
    out[4 * i + 2] = static_cast<uint8_t>(h_[i] >> 8);
    out[4 * i + 3] = static_cast<uint8_t>(h_[i]);
  }
}

void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  Sha256 s;
  s.update(data, len);
  s.final(out);
}

// ------------------------------------------------------------------ X25519 (RFC 7748)
// Field elements mod 2^255 - 19 as 16 limbs of 16 bits (TweetNaCl layout).
namespace {
using gf = int64_t[16];

void car25519(gf o) {
  for (int i = 0; i < 16; ++i) {
    o[i] += (int64_t(1) << 16);
    const int64_t c = o[i] >> 16;
    o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
    o[i] -= c << 16;
  }
}
void sel25519(gf p, gf q, int b) {
  const int64_t c = ~(b - 1);
  for (int i = 0; i < 16; ++i) {
    const int64_t t = c & (p[i] ^ q[i]);
    p[i] ^= t;
    q[i] ^= t;
  }
}
void pack25519(uint8_t* o, const gf n) {
  gf m, t;
  for (int i = 0; i < 16; ++i) t[i] = n[i];
  car25519(t);
  car25519(t);
  car25519(t);
  for (int j = 0; j < 2; ++j) {
    m[0] = t[0] - 0xffed;
    for (int i = 1; i < 15; ++i) {
      m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
      m[i - 1] &= 0xffff;
    }
    m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
    const int b = static_cast<int>((m[15] >> 16) & 1);
    m[14] &= 0xffff;
    sel25519(t, m, 1 - b);
  }
  for (int i = 0; i < 16; ++i) {
    o[2 * i] = static_cast<uint8_t>(t[i] & 0xff);
    o[2 * i + 1] = static_cast<uint8_t>(t[i] >> 8);
  }
}
void unpack25519(gf o, const uint8_t* n) {
  for (int i = 0; i < 16; ++i) o[i] = n[2 * i] + (int64_t(n[2 * i + 1]) << 8);
  o[15] &= 0x7fff;
}
void A(gf o, const gf a, const gf b) {
  for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i];
}
void Z(gf o, const gf a, const gf b) {
  for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i];
}
void M(gf o, const gf a, const gf b) {
  int64_t t[31] = {0};
  for (int i = 0; i < 16; ++i)
    for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
  for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
  for (int i = 0; i < 16; ++i) o[i] = t[i];
  car25519(o);
  car25519(o);
}
void S(gf o, const gf a) { M(o, a, a); }
void inv25519(gf o, const gf i) {
  gf c;
  for (int a = 0; a < 16; ++a) c[a] = i[a];
  for (int a = 253; a >= 0; --a) {
    S(c, c);
    if (a != 2 && a != 4) M(c, c, i);
  }
  for (int a = 0; a < 16; ++a) o[a] = c[a];
}
const gf k121665 = {0xDB41, 1};
}  // namespace

void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) {
  uint8_t z[32];
  int64_t x[80];
  gf a, b, c, d, e, f;
  for (int i = 0; i < 31; ++i) z[i] = scalar[i];
  z[31] = static_cast<uint8_t>((scalar[31] & 127) | 64);
  z[0] &= 248;
  unpack25519(x, point);
  for (int i = 0; i < 16; ++i) {
    b[i] = x[i];
    d[i] = a[i] = c[i] = 0;
  }
  a[0] = d[0] = 1;
  for (int i = 254; i >= 0; --i) {
    const int r = (z[i >> 3] >> (i & 7)) & 1;
    sel25519(a, b, r);
    sel25519(c, d, r);
    A(e, a, c);
    Z(a, a, c);
    A(c, b, d);
    Z(b, b, d);
    S(d, e);
    S(f, a);
    M(a, c, a);
    M(c, b, e);
    A(e, a, c);
    Z(a, a, c);
    S(b, a);
    Z(c, d, f);
    M(a, c, k121665);
    A(a, a, d);
    M(c, c, a);
    M(a, d, f);
    M(d, b, x);
    S(b, e);
    sel25519(a, b, r);
    sel25519(c, d, r);
  }
  for (int i = 0; i < 16; ++i) {
    x[i + 16] = a[i];
    x[i + 32] = c[i];
    x[i + 48] = b[i];
    x[i + 64] = d[i];
  }
  inv25519(x + 32, x + 32);
  M(x + 16, x + 16, x + 32);
  pack25519(out, x + 16);
}

void x25519_public(uint8_t out[32], const uint8_t secret[32]) {
  uint8_t base[32] = {9};
  x25519(out, secret, base);
}

// ------------------------------------------------------------------ WiFi password sealing
namespace wifi_secret {

void derive_key(const uint8_t shared[32], uint32_t rf_key, uint8_t key[32]) {
  Sha256 s;
  s.update(shared, 32);
  const uint8_t k[4] = {static_cast<uint8_t>(rf_key), static_cast<uint8_t>(rf_key >> 8),
                        static_cast<uint8_t>(rf_key >> 16), static_cast<uint8_t>(rf_key >> 24)};
  s.update(k, 4);
  static const char kLabel[] = "rocketfc-wifi-v1";
  s.update(reinterpret_cast<const uint8_t*>(kLabel), sizeof(kLabel) - 1);
  s.final(key);
}

void crypt(const uint8_t key[32], const uint8_t* in, uint8_t* out, size_t len) {
  for (size_t off = 0, blk = 0; off < len; off += 32, ++blk) {
    Sha256 s;
    s.update(key, 32);
    s.update(reinterpret_cast<const uint8_t*>("enc"), 3);
    const uint8_t b = static_cast<uint8_t>(blk);
    s.update(&b, 1);
    uint8_t ks[32];
    s.final(ks);
    for (size_t i = 0; i < 32 && off + i < len; ++i) out[off + i] = in[off + i] ^ ks[i];
  }
}

void tag(const uint8_t key[32], const char* ssid, const uint8_t* ct, size_t len, uint8_t out[kTag]) {
  Sha256 s;
  s.update(key, 32);
  s.update(reinterpret_cast<const uint8_t*>("tag"), 3);
  s.update(reinterpret_cast<const uint8_t*>(ssid), strlen(ssid) + 1);  // with its NUL: unambiguous
  s.update(ct, len);
  uint8_t h[32];
  s.final(h);
  memcpy(out, h, kTag);
}

}  // namespace wifi_secret

void to_hex(const uint8_t* in, size_t len, char* out) {
  static const char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < len; ++i) {
    out[2 * i] = kHex[in[i] >> 4];
    out[2 * i + 1] = kHex[in[i] & 15];
  }
  out[2 * len] = '\0';
}

size_t from_hex(const char* in, uint8_t* out, size_t cap) {
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  const size_t n = in ? strlen(in) : 0;
  if (n % 2 || n / 2 > cap) return 0;
  for (size_t i = 0; i < n / 2; ++i) {
    const int h = nib(in[2 * i]), l = nib(in[2 * i + 1]);
    if (h < 0 || l < 0) return 0;
    out[i] = static_cast<uint8_t>(h * 16 + l);
  }
  return n / 2;
}

}  // namespace fc
