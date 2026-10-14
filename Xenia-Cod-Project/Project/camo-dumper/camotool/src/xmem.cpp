#include "xmem.h"

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

#include <system.h>
#include <lzx.h>

namespace xmem {
namespace {

constexpr int kWindowBits = 17;
constexpr size_t kFrameSize = 0x8000;

struct MemFile {
  uint8_t* buf;
  size_t len;
  size_t pos;
};

int mem_read(struct mspack_file* f, void* buffer, int bytes) {
  auto* m = reinterpret_cast<MemFile*>(f);
  size_t n = std::min<size_t>(size_t(bytes), m->len - m->pos);
  std::memcpy(buffer, m->buf + m->pos, n);
  m->pos += n;
  return int(n);
}

int mem_write(struct mspack_file* f, void* buffer, int bytes) {
  auto* m = reinterpret_cast<MemFile*>(f);
  size_t n = std::min<size_t>(size_t(bytes), m->len - m->pos);
  std::memcpy(m->buf + m->pos, buffer, n);
  m->pos += n;
  return int(n);
}

void* mem_alloc(struct mspack_system*, size_t n) { return std::malloc(n); }
void mem_free(void* p) { std::free(p); }
void mem_copy(void* src, void* dst, size_t n) { std::memmove(dst, src, n); }
void mem_message(struct mspack_file*, const char*, ...) {}

mspack_system g_sys = {nullptr, nullptr, mem_read, mem_write, nullptr, nullptr,
                       mem_message, mem_alloc, mem_free, mem_copy, nullptr};

// LZX bitstream writer: 16-bit little-endian words, most significant bit first.
struct BitWriter {
  std::vector<uint8_t>& out;
  uint32_t acc = 0;
  int count = 0;
  void put(uint32_t value, int bits) {
    for (int i = bits - 1; i >= 0; i--) {
      acc = (acc << 1) | ((value >> i) & 1);
      if (++count == 16) {
        out.push_back(uint8_t(acc));
        out.push_back(uint8_t(acc >> 8));
        acc = 0;
        count = 0;
      }
    }
  }
  void align16() {
    if (count) put(0, 16 - count);
  }
};

}  // namespace

bool decompress(const uint8_t* in, size_t in_len, std::vector<uint8_t>& out) {
  // Strip the XMem chunk headers: each chunk is one LZX frame.
  std::vector<uint8_t> raw;
  raw.reserve(in_len);
  size_t total = 0, p = 0;
  while (p < in_len) {
    size_t frame, block;
    if (in[p] == 0xFF) {
      if (p + 5 > in_len) break;
      frame = size_t(in[p + 1]) << 8 | in[p + 2];
      block = size_t(in[p + 3]) << 8 | in[p + 4];
      p += 5;
    } else {
      if (p + 2 > in_len) break;
      frame = kFrameSize;
      block = size_t(in[p]) << 8 | in[p + 1];
      p += 2;
    }
    if (!frame || !block) break;
    block = std::min(block, in_len - p);
    raw.insert(raw.end(), in + p, in + p + block);
    p += block;
    total += frame;
  }
  out.assign(total, 0);
  MemFile mi{raw.data(), raw.size(), 0};
  MemFile mo{out.data(), out.size(), 0};
  lzxd_stream* s = lzxd_init(&g_sys, reinterpret_cast<mspack_file*>(&mi),
                             reinterpret_cast<mspack_file*>(&mo), kWindowBits, 0,
                             int(kFrameSize), off_t(total), 0);
  if (!s) return false;
  int err = lzxd_decompress(s, off_t(total));
  lzxd_free(s);
  return err == MSPACK_ERR_OK && mo.pos == total;
}

std::vector<uint8_t> store(const uint8_t* data, size_t len) {
  std::vector<uint8_t> out;
  bool first = true;
  for (size_t off = 0; off < len; off += kFrameSize) {
    size_t frame = std::min(kFrameSize, len - off);
    std::vector<uint8_t> chunk;
    BitWriter bw{chunk};
    if (first) bw.put(0, 1);  // no Intel E8 translation
    first = false;
    bw.put(3, 3);  // uncompressed block
    bw.put(uint32_t(frame >> 8), 16);
    bw.put(uint32_t(frame & 0xFF), 8);
    bw.align16();
    for (int r = 0; r < 3; r++) {  // R0, R1, R2 = 1
      chunk.push_back(1);
      chunk.push_back(0);
      chunk.push_back(0);
      chunk.push_back(0);
    }
    chunk.insert(chunk.end(), data + off, data + off + frame);
    if (frame & 1) chunk.push_back(0);
    if (frame == kFrameSize) {
      out.push_back(uint8_t(chunk.size() >> 8));
      out.push_back(uint8_t(chunk.size()));
    } else {
      out.push_back(0xFF);
      out.push_back(uint8_t(frame >> 8));
      out.push_back(uint8_t(frame));
      out.push_back(uint8_t(chunk.size() >> 8));
      out.push_back(uint8_t(chunk.size()));
    }
    out.insert(out.end(), chunk.begin(), chunk.end());
  }
  return out;
}

}  // namespace xmem
