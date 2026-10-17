#pragma once

// Dumps the game's compiled Lua (LUI / HKS) assets to disk by walking the
// asset DB (ASSET_TYPE_LUA_FILE). Single-player addresses from the iw6-mod
// symbol list. What you get is compiled HKS bytecode - decompiling to readable
// Lua needs an external HKS decompiler.
namespace Uidump
{
    bool Init(); // resolve the DB_EnumXAssets / LUI_OpenMenu addresses (rebased)

    // Dump every Lua asset to outDir (keeping each asset's sub-path). Returns
    // the number written, or -1 if the function address wasn't resolved.
    int Dump(const wchar_t* outDir);

    // Write every weapon's internal name (ASSET_TYPE_WEAPON) to outFile, one
    // per line, sorted. Returns the count, or -1 if unresolved.
    int DumpWeapons(const wchar_t* outFile);

    // Write menu (ASSET_TYPE_MENU) names to outFile, one per line, sorted.
    // Returns the count, or -1 if unresolved. (Mostly empty on LUI builds.)
    int DumpMenus(const wchar_t* outFile);

    // Open a LUI menu directly via LUI_OpenMenu. Returns false if unresolved.
    bool OpenMenu(const char* menuName);
}
