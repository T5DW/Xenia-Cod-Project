#include "renderer.h"
#include "features.h"
#include "console.h"
#include "uidump.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <MinHook.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

// Declared by imgui_impl_win32.cpp.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ---- Hook typedefs ---------------------------------------------------------
using Present_t       = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffers_t = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

static Present_t       oPresent       = nullptr;
static ResizeBuffers_t oResizeBuffers = nullptr;
static WNDPROC         oWndProc       = nullptr;

// ---- D3D state captured from the game --------------------------------------
static ID3D11Device*        g_device  = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static ID3D11RenderTargetView* g_rtv  = nullptr;
static HWND  g_window = nullptr;
static bool  g_initialized = false;

static void CreateRenderTarget(IDXGISwapChain* swapchain)
{
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer))) && backBuffer)
    {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
        backBuffer->Release();
    }
}

static void ReleaseRenderTarget()
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

// ---- Window procedure hook -------------------------------------------------
static LRESULT __stdcall hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (Console::IsOpen())
    {
        // Feed input to ImGui and swallow it so it doesn't reach the game.
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        return TRUE;
    }

    return CallWindowProc(oWndProc, hWnd, msg, wParam, lParam);
}

// ---- Hotkey polling --------------------------------------------------------
// Games often read the keyboard via raw/DirectInput, so WM_KEYDOWN may never
// reach our WndProc. Polling the async key state each frame is reliable.
static void PollHotkeys()
{
    auto pressed = [](int vk, bool& prev)
    {
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool edge = down && !prev; // rising edge = a fresh press
        prev = down;
        return edge;
    };

    static bool prevTilde = false;

    if (pressed(Console::ToggleVk(), prevTilde)) // user-bound toggle key (con_togglekey)
    {
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
            Console::ToggleOutput();
        else
            Console::Toggle();
    }
}

// ---- Present hook (the per-frame draw) -------------------------------------
static HRESULT __stdcall hkPresent(IDXGISwapChain* swapchain, UINT syncInterval, UINT flags)
{
    if (!g_initialized)
    {
        if (SUCCEEDED(swapchain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device))))
        {
            g_device->GetImmediateContext(&g_context);

            DXGI_SWAP_CHAIN_DESC desc;
            swapchain->GetDesc(&desc);
            g_window = desc.OutputWindow;

            CreateRenderTarget(swapchain);

            ImGui::CreateContext();
            ImGui::StyleColorsDark();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr; // don't litter imgui.ini next to the game

            ImGui_ImplWin32_Init(g_window);
            ImGui_ImplDX11_Init(g_device, g_context);

            oWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtr(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hkWndProc)));

            g_initialized = true;
        }
        else
        {
            return oPresent(swapchain, syncInterval, flags);
        }
    }

    PollHotkeys();
    Console::BeginFrame(); // rebuild font atlas before the backend's NewFrame

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    Console::Draw();

    ImGui::Render();
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    return oPresent(swapchain, syncInterval, flags);
}

static HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* swapchain, UINT bufferCount,
                                         UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    ReleaseRenderTarget();
    HRESULT hr = oResizeBuffers(swapchain, bufferCount, width, height, format, flags);
    CreateRenderTarget(swapchain);
    return hr;
}

// ---- Find the swapchain vtable by spinning up a throwaway device -----------
// IDXGISwapChain::Present is vtable index 8, ResizeBuffers is 13.
static bool ResolveSwapchainMethods(void** outPresent, void** outResize)
{
    WNDCLASSEXA wc = { sizeof(wc) };
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = "iw6sp_dummy_wnd";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowA(wc.lpszClassName, "dummy", WS_OVERLAPPEDWINDOW,
                              0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                        = 1;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtained;
    IDXGISwapChain*      swapchain = nullptr;
    ID3D11Device*        device    = nullptr;
    ID3D11DeviceContext* context   = nullptr;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 1, D3D11_SDK_VERSION,
        &sd, &swapchain, &device, &obtained, &context);

    bool ok = false;
    if (SUCCEEDED(hr))
    {
        void** vtable = *reinterpret_cast<void***>(swapchain);
        *outPresent = vtable[8];
        *outResize  = vtable[13];
        ok = true;

        swapchain->Release();
        device->Release();
        context->Release();
    }

    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return ok;
}

namespace Renderer
{
    void Setup()
    {
        if (MH_Initialize() != MH_OK)
            return;

        void* presentAddr = nullptr;
        void* resizeAddr  = nullptr;
        if (!ResolveSwapchainMethods(&presentAddr, &resizeAddr))
            return;

        MH_CreateHook(presentAddr, &hkPresent, reinterpret_cast<void**>(&oPresent));
        MH_CreateHook(resizeAddr,  &hkResizeBuffers, reinterpret_cast<void**>(&oResizeBuffers));
        MH_EnableHook(MH_ALL_HOOKS);

        // Resolve engine addresses for the dvar/features layer. The main
        // module is already mapped at inject time, so sig scanning is safe here.
        Features::Init();
        Uidump::Init();
        Console::Setup();
    }

    void Shutdown()
    {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();

        if (g_initialized)
        {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
        ReleaseRenderTarget();

        if (oWndProc && g_window)
            SetWindowLongPtr(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(oWndProc));
    }
}
