#include "fastfile.h"

#include <cstring>
#include <fstream>

#include "crypto.h"
#include "xmem.h"

namespace {

constexpr size_t kHeaderSize = 0x138;
constexpr size_t kNameOffset = 0x18;
constexpr size_t kNameLength = 0x20;
constexpr int kStreams = 4;

const uint8_t kKey[32] = {0x1a, 0xc1, 0xd1, 0x2d, 0x52, 0x7c, 0x59, 0xb4, 0x0e, 0xca, 0x61,
                          0x91, 0x20, 0xff, 0x82, 0x17, 0xcc, 0xff, 0x09, 0xcd, 0x16, 0x89,
                          0x6f, 0x81, 0xb8, 0x29, 0xc7, 0xf5, 0x27, 0x93, 0x40, 0x5d};

// Per-stream Salsa20 nonces. Each block's SHA-1 is folded into the table, so
// changing one block changes the nonces of every later block in its stream.
class IvTable {
 public:
  explicit IvTable(const std::string& name) {
    for (size_t i = 0; i < sizeof(table_); i++) table_[i] = uint8_t(name[(i / 4) % name.size()]);
    for (int& c : counter_) c = 1;
  }
  void iv(int stream, uint8_t out[8]) const {
    size_t pos = size_t((stream + 4 * (counter_[stream] - 1)) % 800) * 20;
    std::memcpy(out, table_ + pos, 8);
  }
  void update(int stream, const uint8_t hash[20]) {
    for (int i = 0; i < 20; i += 5) {
      size_t value = size_t((stream + 4 * counter_[stream]) % 800) * 5;
      for (int x = 0; x < 5; x++) table_[4 * value + x + i] ^= hash[i + x];
    }
    counter_[stream]++;
  }

 private:
  uint8_t table_[16000];
  int counter_[kStreams];
};

uint32_t read_be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}

void write_be32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(uint8_t(v >> 24));
  out.push_back(uint8_t(v >> 16));
  out.push_back(uint8_t(v >> 8));
  out.push_back(uint8_t(v));
}

}  // namespace

bool FastFile::load(const std::filesystem::path& path, std::string& err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    err = "can't open " + path.string();
    return false;
  }
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (d.size() < kHeaderSize || std::memcmp(d.data(), "TAffx100", 8) != 0 ||
      read_be32(d.data() + 8) != 0x81) {
    err = path.filename().string() + " is not a TAffx100 v0x81 fastfile";
    return false;
  }
  header_.assign(d.begin(), d.begin() + kHeaderSize);
  const char* name = reinterpret_cast<const char*>(d.data() + kNameOffset);
  name_.assign(name, strnlen(name, kNameLength));
  if (name_.empty()) {
    err = "fastfile has no zone name";
    return false;
  }

  IvTable ivs(name_);
  blocks_.clear();
  block_raw_sizes_.clear();
  zone_.clear();
  size_t off = kHeaderSize;
  int stream = 0;
  std::vector<uint8_t> raw;
  while (true) {
    if (off + 4 > d.size()) {
      err = "fastfile ends without an end marker";
      return false;
    }
    uint32_t size = read_be32(d.data() + off);
    off += 4;
    if (size == 0) break;
    if (off + size > d.size()) {
      err = "block runs past the end of the file";
      return false;
    }
    std::vector<uint8_t> block(d.begin() + off, d.begin() + off + size);
    off += size;
    uint8_t iv[8], hash[20];
    ivs.iv(stream, iv);
    crypto::salsa20_xor(kKey, iv, block.data(), block.size());
    crypto::sha1(block.data(), block.size(), hash);
    ivs.update(stream, hash);
    if (!xmem::decompress(block.data(), block.size(), raw)) {
      err = "block " + std::to_string(blocks_.size()) + " failed to decompress";
      return false;
    }
    zone_.insert(zone_.end(), raw.begin(), raw.end());
    block_raw_sizes_.push_back(raw.size());
    blocks_.push_back(std::move(block));
    stream = (stream + 1) % kStreams;
  }
  trailer_.assign(d.begin() + off, d.end());
  return true;
}

bool FastFile::save(const std::filesystem::path& path, const std::vector<uint8_t>& new_zone,
                    std::string& err) const {
  if (new_zone.size() != zone_.size()) {
    err = "zone size changed";
    return false;
  }
  std::vector<uint8_t> out(header_);
  IvTable ivs(name_);
  size_t zone_off = 0;
  int stream = 0;
  for (size_t i = 0; i < blocks_.size(); i++) {
    size_t n = block_raw_sizes_[i];
    std::vector<uint8_t> block;
    if (std::memcmp(new_zone.data() + zone_off, zone_.data() + zone_off, n) == 0) {
      block = blocks_[i];
    } else {
      block = xmem::store(new_zone.data() + zone_off, n);
    }
    zone_off += n;
    uint8_t iv[8], hash[20];
    ivs.iv(stream, iv);
    crypto::sha1(block.data(), block.size(), hash);
    ivs.update(stream, hash);
    crypto::salsa20_xor(kKey, iv, block.data(), block.size());
    write_be32(out, uint32_t(block.size()));
    out.insert(out.end(), block.begin(), block.end());
    stream = (stream + 1) % kStreams;
  }
  write_be32(out, 0);
  out.insert(out.end(), trailer_.begin(), trailer_.end());

  std::filesystem::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()))) {
      err = "can't write " + tmp.string();
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    std::filesystem::rename(tmp, path, ec);
  }
  if (ec) {
    err = "can't replace " + path.string() + ": " + ec.message();
    return false;
  }
  return true;
}
