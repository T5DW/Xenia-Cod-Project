// XMemCompress (LZX) streams as used inside Xbox 360 fastfile blocks.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace xmem {

// Decompresses one standalone XMem LZX stream (window 2^17). Returns false on error.
bool decompress(const uint8_t* in, size_t in_len, std::vector<uint8_t>& out);

// Builds an XMem LZX stream that stores `data` in uncompressed LZX blocks.
// Bigger than real compression, but any LZX decoder accepts it.
std::vector<uint8_t> store(const uint8_t* data, size_t len);

}  // namespace xmem
