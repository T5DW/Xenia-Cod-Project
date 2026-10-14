// Black Ops II Xbox 360 fastfile (TAffx100, version 0x81) unpack and repack.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class FastFile {
 public:
  bool load(const std::filesystem::path& path, std::string& err);

  // Writes a fastfile whose zone is `new_zone` (same size as zone()). Blocks
  // whose bytes did not change keep their original compressed data.
  bool save(const std::filesystem::path& path, const std::vector<uint8_t>& new_zone,
            std::string& err) const;

  const std::vector<uint8_t>& zone() const { return zone_; }
  const std::string& name() const { return name_; }

 private:
  std::vector<uint8_t> header_;                 // magic, version, name, signature
  std::vector<std::vector<uint8_t>> blocks_;    // decrypted XMem streams
  std::vector<size_t> block_raw_sizes_;
  std::vector<uint8_t> trailer_;                // bytes after the end marker
  std::vector<uint8_t> zone_;
  std::string name_;
};
