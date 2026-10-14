// Salsa20 and SHA-1, just enough for Black Ops II fastfiles.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crypto {

inline uint32_t rotl32(uint32_t v, int c) { return (v << c) | (v >> (32 - c)); }

inline uint32_t load_le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// XORs `data` with the Salsa20/20 keystream (256-bit key, 64-bit nonce, counter from 0).
inline void salsa20_xor(const uint8_t key[32], const uint8_t nonce[8], uint8_t* data, size_t len) {
  uint32_t in[16];
  in[0] = 0x61707865; in[5] = 0x3320646e; in[10] = 0x79622d32; in[15] = 0x6b206574;
  for (int i = 0; i < 4; i++) {
    in[1 + i] = load_le32(key + 4 * i);
    in[11 + i] = load_le32(key + 16 + 4 * i);
  }
  in[6] = load_le32(nonce);
  in[7] = load_le32(nonce + 4);
  uint64_t counter = 0;
  for (size_t off = 0; off < len; off += 64, counter++) {
    in[8] = uint32_t(counter);
    in[9] = uint32_t(counter >> 32);
    uint32_t x[16];
    std::memcpy(x, in, sizeof(x));
    for (int r = 0; r < 10; r++) {
#define QR(a, b, c, d)                \
  x[b] ^= rotl32(x[a] + x[d], 7);     \
  x[c] ^= rotl32(x[b] + x[a], 9);     \
  x[d] ^= rotl32(x[c] + x[b], 13);    \
  x[a] ^= rotl32(x[d] + x[c], 18);
      QR(0, 4, 8, 12) QR(5, 9, 13, 1) QR(10, 14, 2, 6) QR(15, 3, 7, 11)
      QR(0, 1, 2, 3) QR(5, 6, 7, 4) QR(10, 11, 8, 9) QR(15, 12, 13, 14)
#undef QR
    }
    uint8_t ks[64];
    for (int i = 0; i < 16; i++) {
      uint32_t v = x[i] + in[i];
      ks[4 * i] = uint8_t(v);
      ks[4 * i + 1] = uint8_t(v >> 8);
      ks[4 * i + 2] = uint8_t(v >> 16);
      ks[4 * i + 3] = uint8_t(v >> 24);
    }
    size_t n = std::min<size_t>(64, len - off);
    for (size_t i = 0; i < n; i++) data[off + i] ^= ks[i];
  }
}

inline void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  auto process = [&h](const uint8_t* chunk) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
      w[i] = uint32_t(chunk[4 * i]) << 24 | uint32_t(chunk[4 * i + 1]) << 16 |
             uint32_t(chunk[4 * i + 2]) << 8 | uint32_t(chunk[4 * i + 3]);
    for (int i = 16; i < 80; i++) w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t t = rotl32(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rotl32(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  };
  size_t full = len / 64;
  for (size_t i = 0; i < full; i++) process(data + 64 * i);
  uint8_t tail[128] = {};
  size_t rem = len - full * 64;
  std::memcpy(tail, data + full * 64, rem);
  tail[rem] = 0x80;
  size_t tail_len = rem + 1 + 8 <= 64 ? 64 : 128;
  uint64_t bits = uint64_t(len) * 8;
  for (int i = 0; i < 8; i++) tail[tail_len - 1 - i] = uint8_t(bits >> (8 * i));
  process(tail);
  if (tail_len == 128) process(tail + 64);
  for (int i = 0; i < 5; i++) {
    out[4 * i] = uint8_t(h[i] >> 24);
    out[4 * i + 1] = uint8_t(h[i] >> 16);
    out[4 * i + 2] = uint8_t(h[i] >> 8);
    out[4 * i + 3] = uint8_t(h[i]);
  }
}

}  // namespace crypto
