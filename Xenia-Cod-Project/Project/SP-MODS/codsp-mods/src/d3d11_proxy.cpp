// d3d11.dll proxy.
//
// When this DLL is named "d3d11.dll" and placed next to iw6sp64_ship.exe, the
// game loads it (local directory wins over System32). We forward the D3D11
// entry points the game imports to the genuine system d3d11.dll, so the game
// runs normally - and DllMain (see dllmain.cpp) spins up the mod on load, so
// no external injector is needed.
//
// Forwarding is lazy: the first time a proxied export is called we load the
// real d3d11.dll by full path (no recursion - different path, real file) and
// cache its function pointers. Our own renderer's call to
// D3D11CreateDeviceAndSwapChain binds to this wrapper too, which simply passes
// through to the real implementation.

#include <Windows.h>
#include <d3d11.h>

namespace
{
    HMODULE g_real = nullptr;

    HMODULE ensure_real()
    {
        if (!g_real)
        {
            wchar_t path[MAX_PATH]{};
            GetSystemDirectoryW(path, MAX_PATH);
            lstrcatW(path, L"\\d3d11.dll");
            g_real = LoadLibraryW(path);
        }
        return g_real;
    }

    template <typename T>
    T real_proc(const char* name)
    {
        HMODULE module = ensure_real();
        return module ? reinterpret_cast<T>(GetProcAddress(module, name)) : nullptr;
    }
}

extern "C"
{
    HRESULT WINAPI D3D11CreateDevice(
        IDXGIAdapter* adapter, D3D_DRIVER_TYPE driverType, HMODULE software, UINT flags,
        const D3D_FEATURE_LEVEL* featureLevels, UINT featureLevelCount, UINT sdkVersion,
        ID3D11Device** device, D3D_FEATURE_LEVEL* obtainedLevel, ID3D11DeviceContext** context)
    {
        using fn_t = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                      const D3D_FEATURE_LEVEL*, UINT, UINT,
                                      ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
        static fn_t real = real_proc<fn_t>("D3D11CreateDevice");
        if (!real)
            return E_FAIL;
        return real(adapter, driverType, software, flags, featureLevels, featureLevelCount,
                    sdkVersion, device, obtainedLevel, context);
    }

    HRESULT WINAPI D3D11CreateDeviceAndSwapChain(
        IDXGIAdapter* adapter, D3D_DRIVER_TYPE driverType, HMODULE software, UINT flags,
        const D3D_FEATURE_LEVEL* featureLevels, UINT featureLevelCount, UINT sdkVersion,
        const DXGI_SWAP_CHAIN_DESC* swapChainDesc, IDXGISwapChain** swapChain,
        ID3D11Device** device, D3D_FEATURE_LEVEL* obtainedLevel, ID3D11DeviceContext** context)
    {
        using fn_t = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                      const D3D_FEATURE_LEVEL*, UINT, UINT,
                                      const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
                                      ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
        static fn_t real = real_proc<fn_t>("D3D11CreateDeviceAndSwapChain");
        if (!real)
            return E_FAIL;
        return real(adapter, driverType, software, flags, featureLevels, featureLevelCount,
                    sdkVersion, swapChainDesc, swapChain, device, obtainedLevel, context);
    }
}
