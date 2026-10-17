#include "splash.h"
#include "game.h"

#include <Windows.h>
#include <string>

namespace
{
    HBITMAP g_bmp = nullptr;
    int     g_w = 0;
    int     g_h = 0;

    std::wstring game_dir()
    {
        wchar_t buf[MAX_PATH]{};
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::wstring p(buf);
        const size_t slash = p.find_last_of(L"\\/");
        return (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
    }

    std::wstring splash_path()
    {
        return game_dir() + Game::Current().baseFolder + L"\\assets\\splash\\splash.bmp";
    }

    LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_PAINT)
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, g_bmp);
            BitBlt(dc, 0, 0, g_w, g_h, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    DWORD WINAPI splash_thread(LPVOID)
    {
        const std::wstring path = splash_path();
        g_bmp = static_cast<HBITMAP>(LoadImageW(nullptr, path.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE));
        if (!g_bmp)
            return 0; // no custom splash present

        BITMAP bm{};
        GetObject(g_bmp, sizeof(bm), &bm);
        g_w = bm.bmWidth;
        g_h = bm.bmHeight;

        WNDCLASSW wc{};
        wc.lpfnWndProc   = wnd_proc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"iw6sp_splash";
        RegisterClassW(&wc);

        const int sx = GetSystemMetrics(SM_CXSCREEN);
        const int sy = GetSystemMetrics(SM_CYSCREEN);
        HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"",
                                    WS_POPUP, (sx - g_w) / 2, (sy - g_h) / 2, g_w, g_h,
                                    nullptr, nullptr, wc.hInstance, nullptr);
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        const DWORD start = GetTickCount();
        MSG msg;
        while (true)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (GetTickCount() - start > 4000) // show for ~4 seconds
                break;
            Sleep(16);
        }

        DestroyWindow(hwnd);
        DeleteObject(g_bmp);
        g_bmp = nullptr;
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 0;
    }
}

namespace Splash
{
    void Show()
    {
        CreateThread(nullptr, 0, splash_thread, nullptr, 0, nullptr);
    }
}
