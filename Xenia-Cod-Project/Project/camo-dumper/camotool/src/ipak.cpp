#include "ipak.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

struct Reader {
  std::ifstream f;
  bool big_endian = false;
  uint32_t u32(const uint8_t* p) const {
    return big_endian ? uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]
                      : uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
  }
  bool read_at(uint64_t off, void* buf, size_t n) {
    f.clear();
    f.seekg(std::streamoff(off));
    return bool(f.read(static_cast<char*>(buf), std::streamsize(n)));
  }
};

std::string hex(const uint8_t* p, size_t n) {
  std::string s;
  char b[4];
  for (size_t i = 0; i < n; i++) {
    std::snprintf(b, sizeof(b), "%02x", p[i]);
    s += b;
    if (i % 16 == 15) s += '\n';
    else s += ' ';
  }
  return s;
}

}  // namespace

std::string inspect_ipak(const std::filesystem::path& path, const std::vector<CamoImage>& images) {
  std::ostringstream r;
  Reader rd;
  rd.f.open(path, std::ios::binary);
  if (!rd.f) return "can't open " + path.string() + "\n";
  std::error_code ec;
  r << "file: " << path.filename().string() << " (" << std::filesystem::file_size(path, ec)
    << " bytes)\n";
  uint8_t head[16];
  if (!rd.read_at(0, head, 16)) return r.str() + "file is too small\n";
  r << "header: " << hex(head, 16) << "\n";
  if (std::memcmp(head, "KAPI", 4) == 0) rd.big_endian = false;
  else if (std::memcmp(head, "IPAK", 4) == 0) rd.big_endian = true;
  else return r.str() + "unknown magic\n";
  r << "endian: " << (rd.big_endian ? "big" : "little") << "\n";
  uint32_t version = rd.u32(head + 4), size = rd.u32(head + 8), sections = rd.u32(head + 12);
  r << "version: 0x" << std::hex << version << " size: 0x" << size << std::dec
    << " sections: " << sections << "\n";
  if (sections > 16) return r.str() + "too many sections, layout differs from retail\n";

  uint64_t index_off = 0, data_off = 0;
  uint32_t index_count = 0;
  for (uint32_t s = 0; s < sections; s++) {
    uint8_t sec[16];
    if (!rd.read_at(16 + 16 * s, sec, 16)) break;
    uint32_t type = rd.u32(sec), off = rd.u32(sec + 4), sz = rd.u32(sec + 8), items = rd.u32(sec + 12);
    r << "section " << s << ": type " << type << " offset 0x" << std::hex << off << " size 0x" << sz
      << std::dec << " items " << items << "\n";
    if (type == 1) {
      index_off = off;
      index_count = items;
    } else if (type == 2) {
      data_off = off;
    }
  }
  if (!index_off || !index_count || index_count > 1000000) return r.str() + "no index section\n";

  std::vector<uint8_t> index(size_t(index_count) * 16);
  if (!rd.read_at(index_off, index.data(), index.size())) return r.str() + "index unreadable\n";
  r << "first index entries:\n";
  for (uint32_t i = 0; i < index_count && i < 4; i++) r << hex(&index[i * 16], 16);

  r << "camo matches:\n";
  int dumped = 0;
  for (const CamoImage& img : images) {
    if (img.embedded) continue;
    std::vector<uint32_t> want = {img.hash};
    for (const StreamedPart& p : img.parts) want.push_back(p.hash);
    for (uint32_t i = 0; i < index_count; i++) {
      const uint8_t* e = &index[i * 16];
      uint32_t a = rd.u32(e), b = rd.u32(e + 4);
      bool hit = false;
      for (uint32_t w : want) hit |= (a == w || b == w);
      if (!hit) continue;
      uint32_t off = rd.u32(e + 8), sz = rd.u32(e + 12);
      r << "  " << img.name << ": " << hex(e, 16);
      if (dumped < 3) {
        uint8_t blob[128];
        if (rd.read_at(data_off + off, blob, sizeof(blob)))
          r << "  data+off:\n" << hex(blob, sizeof(blob));
        if (rd.read_at(off, blob, sizeof(blob))) r << "  off:\n" << hex(blob, sizeof(blob));
        dumped++;
      }
      (void)sz;
    }
  }
  return r.str();
}
