#include "uidump.h"
#include "game.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    constexpr uintptr_t kPreferredBase = 0x140000000ull;
    // Asset-type ordinals are filled from the detected game profile in Init().
    int g_assetLua    = 0x36;
    int g_assetWeapon = 0x23;
    int g_assetMenu   = 0x1F;

    // struct LuaFile { const char* name; int len; char strippingType; const char* buffer; }
    struct LuaFile
    {
        const char* name;
        int         len;
        char        strippingType;
        const char* buffer;
    };

    // struct WeaponCompleteDef { const char* szInternalName; WeaponDef* weapDef; }
    struct WeaponCompleteDef
    {
        const char* szInternalName;
        void*       weapDef;
    };

    // XAssetHeader is a union of pointers (8 bytes); passed by value in a register.
    union XAssetHeader
    {
        void*              data;
        LuaFile*           luaFile;
        WeaponCompleteDef* weapon;
    };

    using EnumFunc = void(__cdecl*)(XAssetHeader header, void* inData);
    using DB_EnumXAssets_Internal_t =
        void (*)(int type, EnumFunc func, const void* inData, bool includeOverride);
    using LUI_OpenMenu_t =
        void (*)(int localClientNum, const char* menuName, int isPopup, int isModal, unsigned int isExclusive);

    DB_EnumXAssets_Internal_t g_enum       = nullptr;
    LUI_OpenMenu_t            g_openMenu    = nullptr;

    uintptr_t module_base()
    {
        static uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        return base;
    }

    template <typename T>
    T rebased(uintptr_t addr)
    {
        if (!addr) return nullptr;
        return reinterpret_cast<T>(module_base() + (addr - kPreferredBase));
    }

    struct DumpCtx
    {
        const std::filesystem::path* root;
        int count;
    };

    DumpCtx* g_ctx = nullptr; // the enum callback can't carry a closure

    void write_lua(const LuaFile* lf)
    {
        if (!g_ctx || !lf || !lf->name || !lf->buffer || lf->len <= 0)
            return;

        std::filesystem::path out = *g_ctx->root / lf->name; // keep the asset's sub-path
        if (out.extension().empty())
            out += ".lua";

        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);

        std::ofstream f(out, std::ios::binary);
        if (f)
        {
            f.write(lf->buffer, lf->len);
            ++g_ctx->count;
        }
    }

    void __cdecl enum_cb(XAssetHeader header, void* /*inData*/)
    {
        write_lua(header.luaFile);
    }

    // SEH-only wrapper (no C++ objects here) so a bad address fails safe.
    void seh_enum()
    {
        __try
        {
            g_enum(g_assetLua, enum_cb, nullptr, true);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    // --- weapon name dump ---
    std::vector<std::string>* g_weaponNames = nullptr;

    void __cdecl weapon_cb(XAssetHeader header, void* /*inData*/)
    {
        if (g_weaponNames && header.weapon && header.weapon->szInternalName)
            g_weaponNames->emplace_back(header.weapon->szInternalName);
    }

    void seh_enum_weapons()
    {
        __try
        {
            g_enum(g_assetWeapon, weapon_cb, nullptr, true);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void seh_open_menu(int localClient, const char* menu)
    {
        __try
        {
            g_openMenu(localClient, menu, 0, 0, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    // --- menu name dump (ASSET_TYPE_MENU) ---
    // menuDef_t begins with windowDef_t, whose first field is `const char* name`,
    // so the first pointer of the asset is the menu name.
    std::vector<std::string>* g_menuNames = nullptr;

    void __cdecl menu_cb(XAssetHeader header, void* /*inData*/)
    {
        if (!g_menuNames || !header.data)
            return;
        const char* nm = *reinterpret_cast<const char* const*>(header.data);
        if (nm && static_cast<unsigned char>(nm[0]) >= 0x20) // looks like a string
            g_menuNames->emplace_back(nm);
    }

    void seh_enum_menus()
    {
        __try
        {
            g_enum(g_assetMenu, menu_cb, nullptr, true);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
}

bool Uidump::Init()
{
    const GameProfile& p = Game::Current();
    g_enum        = rebased<DB_EnumXAssets_Internal_t>(p.DB_EnumXAssets_Internal);
    g_openMenu    = rebased<LUI_OpenMenu_t>(p.LUI_OpenMenu);
    g_assetLua    = p.asset_lua;
    g_assetWeapon = p.asset_weapon;
    g_assetMenu   = p.asset_menu;
    return g_enum != nullptr;
}

int Uidump::Dump(const wchar_t* outDir)
{
    if (!g_enum)
        return -1;

    std::filesystem::path root(outDir);
    std::error_code ec;
    std::filesystem::create_directories(root, ec);

    DumpCtx ctx{ &root, 0 };
    g_ctx = &ctx;
    seh_enum();
    g_ctx = nullptr;

    return ctx.count;
}

int Uidump::DumpWeapons(const wchar_t* outFile)
{
    if (!g_enum)
        return -1;

    std::vector<std::string> names;
    g_weaponNames = &names;
    seh_enum_weapons();
    g_weaponNames = nullptr;

    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());

    std::filesystem::path out(outFile);
    std::error_code ec;
    std::filesystem::create_directories(out.parent_path(), ec);

    std::ofstream f(out, std::ios::binary);
    if (f)
        for (const auto& n : names)
            f << n << "\n";

    return static_cast<int>(names.size());
}

int Uidump::DumpMenus(const wchar_t* outFile)
{
    if (!g_enum)
        return -1;

    std::vector<std::string> names;
    g_menuNames = &names;
    seh_enum_menus();
    g_menuNames = nullptr;

    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());

    std::filesystem::path out(outFile);
    std::error_code ec;
    std::filesystem::create_directories(out.parent_path(), ec);

    std::ofstream f(out, std::ios::binary);
    if (f)
        for (const auto& n : names)
            f << n << "\n";

    return static_cast<int>(names.size());
}

bool Uidump::OpenMenu(const char* menuName)
{
    if (!g_openMenu || !menuName)
        return false;
    seh_open_menu(0, menuName);
    return true;
}
