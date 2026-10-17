#include <Windows.h>
#include <cstdio>

#include "renderer.h"
#include "splash.h"
#include "game.h"

// Open a console/cmd window at launch, titled and showing a banner.
static void open_cmd_window()
{
    if (!AllocConsole())
        return;

    const GameProfile& p = Game::Current();
    char banner[128];
    std::snprintf(banner, sizeof(banner), "%s V%s By T5DW", p.displayName, p.version);
    SetConsoleTitleA(banner);

    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);

    std::printf("============================\n");
    std::printf(" %s\n", banner);
    std::printf(" exe: %s\n", Game::ExeName());
    std::printf("============================\n");
    std::printf("[+] DLL loaded. Initializing hooks...\n");
    std::fflush(stdout);
}

// Worker thread: do the hooking off the loader-lock so we never deadlock
// inside DllMain.
static DWORD WINAPI MainThread(LPVOID)
{
    open_cmd_window();
    Splash::Show();     // custom splash.bmp if present
    Renderer::Setup();
    std::printf("[+] Ready. Press your console key in-game.\n");
    std::fflush(stdout);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, MainThread, module, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        Renderer::Shutdown();
        break;
    }
    return TRUE;
}
