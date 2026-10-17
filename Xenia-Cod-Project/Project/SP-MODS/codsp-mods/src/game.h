#pragma once
#include <cstdint>

// COD-SP-MOD runs as ONE d3d11.dll for multiple Call of Duty single-player
// titles. At load it detects which game executable it is inside and selects
// the matching address set + theme, so the wrong addresses are never used on
// the wrong exe.
//
//   iw6sp64_ship.exe  -> Ghosts (IW6)            -> IW6SP-MOD, blue theme
//   s1_sp64_ship.exe  -> Advanced Warfare (S1)   -> S1SP-MOD,  yellow theme
//
// All addresses are the single-player (first) values from the AlterWare
// iw6-mod / s1-mod symbol lists, preferred image base 0x140000000.

enum class GameId { Unknown = 0, IW6 = 1, S1 = 2 };

struct GameProfile
{
    GameId id;
    const char*    displayName; // "IW6SP-MOD"
    const char*    version;     // "1.1"
    const wchar_t* baseFolder;  // L".iw6sp-mod"

    // Default console theme (RGBA 0..1). User overrides in console.json still win.
    float titleColor[4];
    float inboxColor[4];
    float outboxColor[4];

    // Engine addresses (0 = unavailable for this title).
    uintptr_t Cbuf_AddText;
    uintptr_t Dvar_FindVar;
    uintptr_t Dvar_ValueToString;
    uintptr_t Dvar_SetFromString;
    uintptr_t sortedDvars;
    uintptr_t dvarCount;
    uintptr_t DB_EnumXAssets_Internal;
    uintptr_t LUI_OpenMenu;

    // XAssetType ordinals (differ per title).
    int asset_lua;
    int asset_weapon;
    int asset_menu;
};

namespace Game
{
    // Detected profile for the host exe (lazy, cached). Unknown exe -> a safe
    // profile with all addresses 0 so nothing resolves.
    const GameProfile& Current();

    bool Known();            // true if the host exe matched a known title
    const char* ExeName();   // detected executable basename (for diagnostics)
}
