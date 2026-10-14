#include "images.h"

#include <algorithm>
#include <cstring>

#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"

namespace {

uint32_t be32(const std::vector<uint8_t>& d, size_t o) {
  return uint32_t(d[o]) << 24 | uint32_t(d[o + 1]) << 16 | uint32_t(d[o + 2]) << 8 | d[o + 3];
}
uint32_t be16(const std::vector<uint8_t>& d, size_t o) { return uint32_t(d[o]) << 8 | d[o + 1]; }
uint32_t le32(const std::vector<uint8_t>& d, size_t o) {
  return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}

uint32_t align(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }
uint32_t log2_ceil(uint32_t v) {
  uint32_t r = 0;
  while ((1u << r) < v) r++;
  return r;
}
uint32_t next_pow2(uint32_t v) { return 1u << log2_ceil(v); }

// --- Xenos tiled texture addressing, ported from Xenia (BSD license,
// src/xenia/gpu/texture_address.h and texture_util.cc). ---

int32_t tiled_2d(int32_t x, int32_t y, uint32_t pitch_blocks, unsigned bpb_log2) {
  int32_t outer = ((y >> 5) * int32_t(pitch_blocks >> 5) + (x >> 5)) << 6;
  int32_t inner = (((y >> 1) & 7) << 3) | (x & 7);
  int32_t oib = (outer | inner) << bpb_log2;
  uint32_t bank = (y >> 4) & 1;
  uint32_t pipe = ((x >> 3) & 3) ^ (((y >> 3) & 1) << 1);
  return int32_t((uint32_t(y & 1) << 4) | (pipe << 6) | (bank << 11)) | (oib & 0xF) |
         (((oib >> 4) & 1) << 5) | (((oib >> 5) & 7) << 8) | (oib >> 8 << 12);
}

uint32_t packed_mip_level(uint32_t w, uint32_t h) {
  uint32_t s = log2_ceil(std::min(w, h));
  return s > 4 ? s - 4 : 0;
}

// Offset of a packed mip inside the mip tail, in 4x4 blocks.
bool packed_mip_offset(uint32_t w, uint32_t h, uint32_t mip, uint32_t& xb, uint32_t& yb) {
  uint32_t lw = log2_ceil(w), lh = log2_ceil(h), ls = std::min(lw, lh);
  xb = yb = 0;
  if (ls > 4 + mip) return false;
  uint32_t base = ls > 4 ? ls - 4 : 0;
  uint32_t pm = mip - base;
  if (pm < 3) {
    if (lw > lh) yb = 16 >> pm;
    else xb = 16 >> pm;
  } else {
    if (lw > lh) xb = (1u << (lw - base)) >> (pm - 2);
    else yb = (1u << (lh - base)) >> (pm - 2);
  }
  xb /= 4;
  yb /= 4;
  return true;
}

struct LevelLayout {
  size_t offset;  // from the start of the image data
  uint32_t pitch_blocks, x_blocks, y_blocks, width, height;
};

bool level_layout(const CamoImage& img, uint32_t level, uint32_t bpb, LevelLayout& out) {
  uint32_t packed = packed_mip_level(img.width, img.height);
  out.width = std::max(img.width >> level, 1u);
  out.height = std::max(img.height >> level, 1u);
  out.x_blocks = out.y_blocks = 0;
  if (level == 0) {
    out.offset = 0;
    out.pitch_blocks = align((img.pitch_texels + 3) / 4, 32);
    if (packed == 0) packed_mip_offset(img.width, img.height, 0, out.x_blocks, out.y_blocks);
    return true;
  }
  uint32_t stored = packed == 0 ? 0 : std::min(level, packed);
  size_t off = img.mip_offset;
  for (uint32_t l = 1; l < stored; l++) {
    uint32_t pitch = align((std::max(next_pow2(img.width) >> l, 1u) + 3) / 4, 32);
    uint32_t rows = align((std::max(next_pow2(img.height) >> l, 1u) + 3) / 4, 32);
    off += align(pitch * rows * bpb, 4096);
  }
  out.offset = off;
  out.pitch_blocks = align((std::max(next_pow2(img.width) >> stored, 1u) + 3) / 4, 32);
  if (level >= packed) packed_mip_offset(img.width, img.height, level, out.x_blocks, out.y_blocks);
  return true;
}

uint32_t bytes_per_block(uint32_t format) { return (format & 0x3F) == 0x12 ? 8 : 16; }

void swap16(uint8_t* p, size_t n) {
  for (size_t i = 0; i + 1 < n; i += 2) std::swap(p[i], p[i + 1]);
}

void unpack565(uint16_t c, uint8_t* rgb) {
  rgb[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
  rgb[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
  rgb[2] = uint8_t((c & 31) * 255 / 31);
}

// Decodes one PC-order DXT block into a 4x4 RGBA tile.
void decode_block(const uint8_t* b, uint32_t fmt, uint8_t out[64]) {
  const uint8_t* color = (fmt == 0x12) ? b : b + 8;
  uint16_t c0 = uint16_t(color[0] | color[1] << 8), c1 = uint16_t(color[2] | color[3] << 8);
  uint8_t pal[4][4];
  unpack565(c0, pal[0]);
  unpack565(c1, pal[1]);
  pal[0][3] = pal[1][3] = pal[2][3] = 255;
  pal[3][3] = 255;
  if (c0 > c1 || fmt != 0x12) {
    for (int i = 0; i < 3; i++) {
      pal[2][i] = uint8_t((2 * pal[0][i] + pal[1][i]) / 3);
      pal[3][i] = uint8_t((pal[0][i] + 2 * pal[1][i]) / 3);
    }
  } else {
    for (int i = 0; i < 3; i++) {
      pal[2][i] = uint8_t((pal[0][i] + pal[1][i]) / 2);
      pal[3][i] = 0;
    }
    pal[3][3] = 0;
  }
  uint32_t idx = uint32_t(color[4]) | uint32_t(color[5]) << 8 | uint32_t(color[6]) << 16 |
                 uint32_t(color[7]) << 24;
  for (int i = 0; i < 16; i++) std::memcpy(out + 4 * i, pal[(idx >> (2 * i)) & 3], 4);
  if (fmt == 0x13) {
    for (int i = 0; i < 16; i++) {
      uint8_t a = (b[i / 2] >> (4 * (i & 1))) & 15;
      out[4 * i + 3] = uint8_t(a * 17);
    }
  } else if (fmt == 0x14) {
    uint8_t a[8] = {b[0], b[1]};
    if (a[0] > a[1]) {
      for (int i = 1; i < 7; i++) a[i + 1] = uint8_t(((7 - i) * a[0] + i * a[1]) / 7);
    } else {
      for (int i = 1; i < 5; i++) a[i + 1] = uint8_t(((5 - i) * a[0] + i * a[1]) / 5);
      a[6] = 0;
      a[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; i++) out[4 * i + 3] = a[(bits >> (3 * i)) & 7];
  }
}

std::string label_for(const std::string& name, bool& weapon) {
  struct Entry {
    const char* key;
    const char* label;
    bool weapon;
  };
  static const Entry kEntries[] = {
      {"desert_nevada", "Nevada", true},   {"desert_sahara", "Sahara", true},
      {"jungle_erdl", "ERDL", true},       {"jungle_tiger", "Tiger", true},
      {"urban_german", "Berlin", true},    {"urban_warsaw", "Warsaw", true},
      {"winter_siberia", "Siberia", true}, {"winter_yukon", "Yukon", true},
      {"woodland_flora", "Flora", true},   {"woodland", "Woodland", true},
      {"dusty", "Dusty", true},            {"ice", "Ice", true},
      {"camo_od", "Olive", true},          {"gold", "Gold", true},
  };
  weapon = false;
  bool solid = name.rfind("solid_camo_", 0) == 0;
  if (name.rfind("weapon_camo_", 0) == 0) return "No-camo layer (experimental)";
  for (const Entry& e : kEntries) {
    if (name.find(e.key) != std::string::npos) {
      weapon = e.weapon;
      std::string l = e.label;
      if (solid) l += " (far-away tint)";
      else if (name.find("_spec") != std::string::npos) l += " (shine)";
      else if (name.find("_env") != std::string::npos) l += " (reflection)";
      return l;
    }
  }
  return "character camo (not on guns)";
}

bool valid_name_char(uint8_t c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }

}  // namespace

bool CamoImage::editable() const {
  uint32_t f = format & 0x3F;
  return embedded && (f == 0x12 || f == 0x14) && (format & 0x100) && ((format >> 6) & 3) == 1;
}

const char* CamoImage::format_name() const {
  switch (format & 0x3F) {
    case 0x12: return "DXT1";
    case 0x13: return "DXT3";
    case 0x14: return "DXT5";
    default: return "?";
  }
}

std::vector<CamoImage> find_camo_images(const std::vector<uint8_t>& d) {
  std::vector<CamoImage> found;
  for (size_t i = 0x100; i + 12 < d.size(); i++) {
    if (d[i] != 0xFF || be32(d, i) != 0xFFFFFFFF) continue;
    size_t n = i + 8, e = n;
    while (e < d.size() && e - n < 64 && valid_name_char(d[e])) e++;
    if (e >= d.size() || d[e] != 0 || e - n < 4) continue;
    std::string name(reinterpret_cast<const char*>(d.data() + n), e - n);
    if (name.find("camo") == std::string::npos) continue;
    if (name.rfind("mtl_", 0) == 0 || name.rfind("ui_", 0) == 0 || name.rfind("emblem", 0) == 0)
      continue;
    size_t fmt = 0;
    for (size_t f = n - 0x10; f >= n - 0x100 && f >= 0x40; f--) {
      if ((be32(d, f) & 0xFFFFFF00) == 0x1A200100) {
        uint32_t pix = be32(d, f - 4);
        if (pix == 0xFFFFFFFF || pix == 0) {
          fmt = f;
          break;
        }
      }
    }
    if (!fmt) continue;
    CamoImage img;
    img.name = name;
    img.hash = be32(d, n - 4);
    img.format = be32(d, fmt);
    img.card_size = be32(d, fmt - 0x14);
    img.levels = d[fmt - 0x0A];
    img.embedded = be32(d, fmt - 4) == 0xFFFFFFFF && img.card_size > 0;
    uint32_t d0 = le32(d, fmt - 0x30), d2 = le32(d, fmt - 0x28), d5 = le32(d, fmt - 0x1C);
    img.pitch_texels = ((d0 >> 22) & 0x1FF) * 32;
    img.width = (d2 & 0x1FFF) + 1;
    img.height = ((d2 >> 13) & 0x1FFF) + 1;
    img.mip_offset = (d5 >> 12) * 4096;
    if (img.embedded && (img.width != be16(d, fmt - 0x10) || img.height != be16(d, fmt - 0x0E)))
      img.embedded = false;  // header doesn't look like we expect; leave it alone
    img.data_offset = e + 1;
    uint32_t parts = d[n - 12];
    for (uint32_t p = 0; p < parts && p < 4; p++) {
      size_t o = fmt + 4 + 24 * p;
      img.parts.push_back({be32(d, o + 4), uint16_t(be16(d, o + 8)), uint16_t(be16(d, o + 10))});
    }
    img.label = label_for(name, img.weapon_camo);
    found.push_back(img);
    i = e;
  }
  return found;
}

bool decode_level(const std::vector<uint8_t>& zone, const CamoImage& img, uint32_t level,
                  Rgba& out) {
  if (!img.editable() || level >= img.levels) return false;
  uint32_t bpb = bytes_per_block(img.format), fmt = img.format & 0x3F;
  LevelLayout l;
  level_layout(img, level, bpb, l);
  uint32_t wb = (l.width + 3) / 4, hb = (l.height + 3) / 4;
  out.width = l.width;
  out.height = l.height;
  out.pixels.assign(size_t(l.width) * l.height * 4, 0);
  for (uint32_t by = 0; by < hb; by++) {
    for (uint32_t bx = 0; bx < wb; bx++) {
      size_t addr = l.offset + size_t(tiled_2d(int32_t(l.x_blocks + bx), int32_t(l.y_blocks + by),
                                               l.pitch_blocks, bpb == 8 ? 3 : 4));
      if (addr + bpb > img.card_size) return false;
      uint8_t block[16], rgba[64];
      std::memcpy(block, zone.data() + img.data_offset + addr, bpb);
      swap16(block, bpb);
      decode_block(block, fmt, rgba);
      for (uint32_t y = 0; y < 4; y++)
        for (uint32_t x = 0; x < 4; x++) {
          uint32_t px = bx * 4 + x, py = by * 4 + y;
          if (px < l.width && py < l.height)
            std::memcpy(&out.pixels[(size_t(py) * l.width + px) * 4], rgba + 4 * (y * 4 + x), 4);
        }
    }
  }
  return true;
}

Rgba resize(const Rgba& src, uint32_t w, uint32_t h) {
  Rgba out;
  out.width = w;
  out.height = h;
  out.pixels.resize(size_t(w) * h * 4);
  for (uint32_t y = 0; y < h; y++) {
    uint32_t y0 = uint32_t(uint64_t(y) * src.height / h);
    uint32_t y1 = std::max(y0 + 1, uint32_t(uint64_t(y + 1) * src.height / h));
    for (uint32_t x = 0; x < w; x++) {
      uint32_t x0 = uint32_t(uint64_t(x) * src.width / w);
      uint32_t x1 = std::max(x0 + 1, uint32_t(uint64_t(x + 1) * src.width / w));
      uint64_t sum[4] = {};
      for (uint32_t sy = y0; sy < y1; sy++)
        for (uint32_t sx = x0; sx < x1; sx++)
          for (int c = 0; c < 4; c++) sum[c] += src.pixels[(size_t(sy) * src.width + sx) * 4 + c];
      uint64_t count = uint64_t(y1 - y0) * (x1 - x0);
      for (int c = 0; c < 4; c++)
        out.pixels[(size_t(y) * w + x) * 4 + c] = uint8_t((sum[c] + count / 2) / count);
    }
  }
  return out;
}

bool encode_image(std::vector<uint8_t>& zone, const CamoImage& img, const Rgba& src,
                  std::string& err) {
  if (!img.editable()) {
    err = img.name + " can't be edited in common_mp.ff";
    return false;
  }
  uint32_t bpb = bytes_per_block(img.format);
  int alpha = (img.format & 0x3F) == 0x14 ? 1 : 0;
  uint8_t* data = zone.data() + img.data_offset;

  // Tiny images (the far-away tints) become one solid colour, so the exact
  // block layout doesn't matter.
  if (std::min(img.width, img.height) <= 8) {
    Rgba avg = resize(src, 1, 1);
    uint8_t tile[64], block[16];
    for (int i = 0; i < 16; i++) std::memcpy(tile + 4 * i, avg.pixels.data(), 4);
    stb_compress_dxt_block(block, tile, alpha, STB_DXT_HIGHQUAL);
    swap16(block, bpb);
    for (size_t o = 0; o + bpb <= img.card_size; o += bpb) std::memcpy(data + o, block, bpb);
    return true;
  }

  for (uint32_t level = 0; level < img.levels; level++) {
    LevelLayout l;
    level_layout(img, level, bpb, l);
    Rgba lv = resize(src, l.width, l.height);
    uint32_t wb = (l.width + 3) / 4, hb = (l.height + 3) / 4;
    for (uint32_t by = 0; by < hb; by++) {
      for (uint32_t bx = 0; bx < wb; bx++) {
        uint8_t tile[64], block[16];
        for (uint32_t y = 0; y < 4; y++)
          for (uint32_t x = 0; x < 4; x++) {
            uint32_t px = std::min(bx * 4 + x, l.width - 1), py = std::min(by * 4 + y, l.height - 1);
            std::memcpy(tile + 4 * (y * 4 + x), &lv.pixels[(size_t(py) * l.width + px) * 4], 4);
          }
        stb_compress_dxt_block(block, tile, alpha, STB_DXT_HIGHQUAL);
        swap16(block, bpb);
        size_t addr = l.offset + size_t(tiled_2d(int32_t(l.x_blocks + bx), int32_t(l.y_blocks + by),
                                                 l.pitch_blocks, bpb == 8 ? 3 : 4));
        if (addr + bpb > img.card_size) {
          err = img.name + ": mip " + std::to_string(level) + " falls outside its data";
          return false;
        }
        std::memcpy(data + addr, block, bpb);
      }
    }
  }
  return true;
}
