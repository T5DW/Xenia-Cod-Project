#pragma once
#include <cstddef>
#include <cstdint>

// IDA-style byte pattern scanning over the game's main module.
// Patterns use space-separated hex bytes with "?" or "??" as wildcards,
// e.g.  "48 8B 05 ? ? ? ? 48 85 C0"
namespace sig
{
    // Scan the main (executable) module. Returns the address of the first
    // match, or 0 if not found / pattern empty.
    uintptr_t Scan(const char* pattern);

    // Scan an explicit [base, base+size) range.
    uintptr_t Scan(uintptr_t base, size_t size, const char* pattern);

    // Resolve a relative operand (rel32) into an absolute address.
    //   addr     = address of the instruction
    //   offset   = byte offset of the rel32 operand within the instruction
    //   instrLen = total length of the instruction
    // e.g. for `E8 xx xx xx xx` (call rel32): ResolveRelative(addr, 1, 5)
    uintptr_t ResolveRelative(uintptr_t addr, int offset, int instrLen);
}
