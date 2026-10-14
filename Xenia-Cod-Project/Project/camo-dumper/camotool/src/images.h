// Camo images (GfxImage assets) inside an unpacked Black Ops II zone.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct StreamedPart {
  uint32_t hash;
  uint16_t width, height;
};

struct CamoImage {
  std::string name;
  std::string label;       // what the camo is called in game, or what it's for
  bool weapon_camo = false;
  uint32_t hash = 0;
  uint32_t format = 0;     // D3DFORMAT, e.g. 0x1A200152 = tiled DXT1
  uint32_t card_size = 0;  // bytes of pixel data stored in the zone
  uint32_t width = 0, height = 0, levels = 0;
  uint32_t pitch_texels = 0;
  uint32_t mip_offset = 0; // offset of mip levels from the base level
  size_t data_offset = 0;  // zone offset of the pixel data
  bool embedded = false;   // pixels are in the zone (false = streamed from an .ipak)
  std::vector<StreamedPart> parts;

  bool editable() const;
  const char* format_name() const;
};

struct Rgba {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> pixels;  // RGBA8
};

std::vector<CamoImage> find_camo_images(const std::vector<uint8_t>& zone);

// Decodes one mip level of an embedded image.
bool decode_level(const std::vector<uint8_t>& zone, const CamoImage& img, uint32_t level,
                  Rgba& out);

// Re-encodes `src` (any size) into every mip level of `img`, in place in `zone`.
bool encode_image(std::vector<uint8_t>& zone, const CamoImage& img, const Rgba& src,
                  std::string& err);

Rgba resize(const Rgba& src, uint32_t width, uint32_t height);
