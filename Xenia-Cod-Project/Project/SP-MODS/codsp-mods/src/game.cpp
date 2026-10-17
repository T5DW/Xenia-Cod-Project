#include "game.h"

#include <Windows.h>
#include <string>

namespace
{
    // ---- Ghosts (IW6) : iw6sp64_ship.exe -----------------------------------
    const GameProfile kIW6 = {
        GameId::IW6,
        "IW6SP-MOD", "1.1", L".iw6sp-mod",
        { 0.00f, 0.70f, 1.00f, 1.00f }, // title  (blue)
        { 0.20f, 0.20f, 0.20f, 0.90f }, // inbox
        { 0.00f, 0.70f, 1.00f, 1.00f }, // outbox (blue)
        0x1403B3050, // Cbuf_AddText
        0x140429E70, // Dvar_FindVar
        0x14042E710, // Dvar_ValueToString
        0x14042D000, // Dvar_SetFromStringByNameFromSource
        0x1458CBA60, // sortedDvars
        0x1458CBA3C, // dvarCount
        0x140271FC0, // DB_EnumXAssets_Internal
        0x1403FD460, // LUI_OpenMenu
        0x36, 0x23, 0x1F, // lua, weapon, menu
    };

    // ---- Advanced Warfare (S1) : s1_sp64_ship.exe --------------------------
    const GameProfile kS1 = {
        GameId::S1,
        "S1SP-MOD", "1.0", L".s1sp-mod",
        { 1.00f, 0.85f, 0.00f, 1.00f }, // title  (yellow)
        { 0.00f, 0.00f, 0.00f, 0.92f }, // inbox  (black)
        { 1.00f, 0.85f, 0.00f, 1.00f }, // outbox (yellow)
        0x1402ED890, // Cbuf_AddText
        0x140370860, // Dvar_FindVar
        0x140374E10, // Dvar_ValueToString
        0x1403737D0, // Dvar_SetFromStringByNameFromSource
        0x14A7BFF50, // sortedDvars
        0x14A7BFF34, // dvarCount
        0x14017D830, // DB_EnumXAssets_Internal
        0x0,         // LUI_OpenMenu (MP-only in S1)
        0x3B, 0x28, 0x24, // lua, weapon, menu
    };

    // ---- Unknown exe : safe no-op profile ----------------------------------
    const GameProfile kUnknown = {
        GameId::Unknown,
        "COD-SP-MOD", "1.0", L".codsp-mod",
        { 0.00f, 0.70f, 1.00f, 1.00f },
        { 0.10f, 0.10f, 0.10f, 0.90f },
        { 0.00f, 0.70f, 1.00f, 1.00f },
        0, 0, 0, 0, 0, 0, 0, 0,
        0x36, 0x23, 0x1F,
    };

    std::string g_exeName;
    const GameProfile* g_profile = nullptr;

    std::string exe_basename_lower()
    {
        char buf[MAX_PATH]{};
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
        std::string p(buf);
        const size_t slash = p.find_last_of("\\/");
        std::string base = (slash == std::string::npos) ? p : p.substr(slash + 1);
        for (char& c : base) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        return base;
    }

    const GameProfile& detect()
    {
        g_exeName = exe_basename_lower();
        if (g_exeName == "iw6sp64_ship.exe")
            return kIW6;
        if (g_exeName == "s1_sp64_ship.exe")
            return kS1;
        return kUnknown;
    }
}

const GameProfile& Game::Current()
{
    if (!g_profile)
        g_profile = &detect();
    return *g_profile;
}

bool Game::Known()
{
    return Current().id != GameId::Unknown;
}

const char* Game::ExeName()
{
    Current(); // ensure detection ran
    return g_exeName.c_str();
}
