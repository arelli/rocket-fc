// Small cryptography for the rocket's WiFi credentials over the radio.
//
// The ground station's WiFi password must never cross the LoRa link in
// clear. Both kits make a fresh X25519 key pair for each request and
// exchange the public halves by radio; each computes the same shared secret,
// which (mixed with the pairing key rf_key) encrypts the password. Someone
// recording the radio sees only public keys and ciphertext.
//
//   station -> rocket   wifi key <station public key, hex>
//   rocket  -> station  wifi: key <rocket public key, hex>
//   station -> rocket   wifi join <ssid> <encrypted password, hex> <tag, hex>
//
// key = SHA-256(X25519 shared secret | rf_key (u32 LE) | "rocketfc-wifi-v1")
// encrypted password = password XOR SHA-256(key | "enc" | block number)...
// tag = first 8 bytes of SHA-256(key | "tag" | ssid | encrypted password)
//
// X25519 follows RFC 7748 (implementation after TweetNaCl, public domain);
// tests: test/test_crypto with the RFC's vectors. It protects against
// listening, not against an active attacker who also knows rf_key.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fc {

void sha256(const uint8_t* data, size_t len, uint8_t out[32]);

class Sha256 {
 public:
  Sha256();
  void update(const uint8_t* data, size_t len);
  void final(uint8_t out[32]);

 private:
  void block(const uint8_t* p);
  uint32_t h_[8];
  uint8_t buf_[64];
  size_t n_ = 0;
  uint64_t total_ = 0;
};

// out = scalar * point (32-byte little-endian u-coordinates). The scalar is
// clamped as RFC 7748 requires.
void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
// out = scalar * base point (9): the public key.
void x25519_public(uint8_t out[32], const uint8_t secret[32]);

namespace wifi_secret {
constexpr size_t kMaxPass = 63;
constexpr size_t kTag = 8;
void derive_key(const uint8_t shared[32], uint32_t rf_key, uint8_t key[32]);
// XOR with the keystream (encrypts and decrypts).
void crypt(const uint8_t key[32], const uint8_t* in, uint8_t* out, size_t len);
void tag(const uint8_t key[32], const char* ssid, const uint8_t* ct, size_t len, uint8_t out[kTag]);
}  // namespace wifi_secret

// Hex helpers: to_hex writes 2*len chars + NUL; from_hex returns bytes decoded
// (0 on a bad character or odd length, or if more than cap).
void to_hex(const uint8_t* in, size_t len, char* out);
size_t from_hex(const char* in, uint8_t* out, size_t cap);

}  // namespace fc
