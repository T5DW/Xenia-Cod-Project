#include "signature.h"

#include <Windows.h>
#include <cstdlib>
#include <vector>

namespace
{
    // Parse "48 8B ? ?" into bytes + a match mask (false = wildcard).
    bool ParsePattern(const char* pattern, std::vector<uint8_t>& bytes, std::vector<bool>& mask)
    {
        for (const char* p = pattern; *p; )
        {
            if (*p == ' ')
            {
                ++p;
                continue;
            }

            if (*p == '?')
            {
                bytes.push_back(0);
                mask.push_back(false);
                ++p;
                if (*p == '?') ++p; // allow "??"
            }
            else
            {
                bytes.push_back(static_cast<uint8_t>(std::strtoul(p, nullptr, 16)));
                mask.push_back(true);
                // advance past the two hex digits of this token
                p += (p[1] && p[1] != ' ') ? 2 : 1;
            }
        }
        return !bytes.empty();
    }
}

uintptr_t sig::Scan(uintptr_t base, size_t size, const char* pattern)
{
    std::vector<uint8_t> bytes;
    std::vector<bool>    mask;
    if (!base || !ParsePattern(pattern, bytes, mask))
        return 0;

    const size_t len = bytes.size();
    if (size < len)
        return 0;

    const auto* data = reinterpret_cast<const uint8_t*>(base);
    for (size_t i = 0; i <= size - len; ++i)
    {
        bool found = true;
        for (size_t j = 0; j < len; ++j)
        {
            if (mask[j] && data[i + j] != bytes[j])
            {
                found = false;
                break;
            }
        }
        if (found)
            return base + i;
    }
    return 0;
}

uintptr_t sig::Scan(const char* pattern)
{
    HMODULE module = GetModuleHandleA(nullptr);
    if (!module)
        return 0;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);

    return Scan(reinterpret_cast<uintptr_t>(module),
                nt->OptionalHeader.SizeOfImage,
                pattern);
}

uintptr_t sig::ResolveRelative(uintptr_t addr, int offset, int instrLen)
{
    if (!addr)
        return 0;
    const int32_t rel = *reinterpret_cast<int32_t*>(addr + offset);
    return addr + instrLen + rel;
}
