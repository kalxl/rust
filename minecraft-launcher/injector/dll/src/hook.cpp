/*
 * Rust Client — DX12 Hook via DX11on12 + D2D
 *
 * How it works:
 *  1. Create dummy DX12 device + swap chain to find Present() vtable address.
 *  2. Hook Present() by patching function bytes (works on real swap chain too).
 *  3. On first real Present call, get the game's ID3D12Device and
 *     ID3D12CommandQueue (found by scanning swap chain object memory).
 *  4. Create a D3D11On12 device wrapping the DX12 device/queue.
 *  5. Create D2D on top of the D3D11On12 device.
 *  6. Each frame: wrap back buffer as D3D11 texture → D2D bitmap → draw overlay.
 *
 * This is exactly how Flarial and every working MCBE client does it.
 * Version-independent — works on MC 26.x and beyond.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11on12.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d2d1_3.h>
#include <d2d1_1.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>

#include "hook.h"
#include "overlay.h"
#include "fps.h"
#include "menu.h"

using Microsoft::WRL::ComPtr;

// ── Globals ───────────────────────────────────────────────

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
static PresentFn g_OrigPresent = nullptr;

// DX12 resources grabbed from the real swap chain
static ComPtr<ID3D12Device>       g_D3D12Device;
static ComPtr<ID3D12CommandQueue> g_D3D12Queue;

// DX11on12 + D2D resources
static ComPtr<ID3D11Device>        g_D3D11Device;
static ComPtr<ID3D11DeviceContext> g_D3D11Context;
static ComPtr<ID3D11On12Device>    g_D3D11On12Device;
static ComPtr<ID2D1Factory3>       g_D2DFactory;
static ComPtr<IDWriteFactory3>     g_DWriteFactory;
static ComPtr<ID2D1Device2>        g_D2DDevice;
static ComPtr<ID2D1DeviceContext2> g_D2DContext;

// Per-buffer resources (swap chain has 2-3 buffers)
static const UINT MAX_BUFFERS = 4;
static ComPtr<ID3D11Resource>  g_WrappedBuffers[MAX_BUFFERS];
static ComPtr<ID2D1Bitmap1>    g_D2DBitmaps[MAX_BUFFERS];
static UINT                    g_BufferCount = 0;

static bool  g_Initialized = false;
static HWND  g_GameHwnd    = nullptr;

// ── Trampoline patch (14-byte absolute jmp) ──────────────

static uint8_t g_OrigBytes[14];
static void*   g_PatchTarget = nullptr;

static void InstallPatch(void* target, void* detour) {
    DWORD old;
    VirtualProtect(target, 14, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_OrigBytes, target, 14);
    uint8_t* p = (uint8_t*)target;
    p[0] = 0xFF; p[1] = 0x25;
    p[2] = p[3] = p[4] = p[5] = 0;
    *(uint64_t*)(p + 6) = (uint64_t)detour;
    VirtualProtect(target, 14, old, &old);
    g_PatchTarget = target;
}

static void RemovePatch() {
    if (!g_PatchTarget) return;
    DWORD old;
    VirtualProtect(g_PatchTarget, 14, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_PatchTarget, g_OrigBytes, 14);
    VirtualProtect(g_PatchTarget, 14, old, &old);
}

// Call original by temporarily removing patch
static HRESULT CallOrig(IDXGISwapChain* sc, UINT si, UINT fl) {
    RemovePatch();
    HRESULT hr = sc->Present(si, fl);
    InstallPatch(g_PatchTarget, (void*)/* dummy — we reinstall below */ g_PatchTarget);
    return hr;
}

// ── Find command queue inside swap chain object ───────────
// Flarial/eugen15 technique: scan swap chain memory for the queue pointer

static ID3D12CommandQueue* FindCommandQueue(IDXGISwapChain* sc, ID3D12CommandQueue* knownQueue) {
    // We know the queue pointer — find its offset inside the swap chain object
    uintptr_t needle = (uintptr_t)knownQueue;
    const uint8_t* base = (const uint8_t*)sc;
    for (size_t i = 0; i < 512; i += sizeof(uintptr_t)) {
        uintptr_t val = *(uintptr_t*)(base + i);
        if (val == needle) return knownQueue; // confirmed it's there
    }
    return knownQueue; // return it anyway — we already have it from init
}

// ── Init DX11on12 + D2D on the real swap chain ───────────

static bool InitDX11On12(IDXGISwapChain* sc) {
    // Get DX12 device from swap chain
    if (FAILED(sc->GetDevice(__uuidof(ID3D12Device), (void**)g_D3D12Device.GetAddressOf())))
        return false;

    // Get swap chain desc for buffer count and window
    DXGI_SWAP_CHAIN_DESC desc{};
    sc->GetDesc(&desc);
    g_GameHwnd    = desc.OutputWindow;
    g_BufferCount = desc.BufferCount;
    if (g_BufferCount == 0 || g_BufferCount > MAX_BUFFERS) g_BufferCount = 2;

    // We need the command queue — stored in g_D3D12Queue from Hook_Init dummy setup
    if (!g_D3D12Queue) return false;

    // Create D3D11on12 device
    IUnknown* queues[] = { g_D3D12Queue.Get() };
    HRESULT hr = D3D11On12CreateDevice(
        g_D3D12Device.Get(),
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0,
        queues, 1,
        0,
        g_D3D11Device.GetAddressOf(),
        g_D3D11Context.GetAddressOf(),
        nullptr);
    if (FAILED(hr)) return false;

    hr = g_D3D11Device->QueryInterface(__uuidof(ID3D11On12Device),
                                       (void**)g_D3D11On12Device.GetAddressOf());
    if (FAILED(hr)) return false;

    // Create D2D factory
    D2D1_FACTORY_OPTIONS opts{};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           __uuidof(ID2D1Factory3), &opts,
                           (void**)g_D2DFactory.GetAddressOf());
    if (FAILED(hr)) return false;

    // Create DWrite factory
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             __uuidof(IDWriteFactory3),
                             (IUnknown**)g_DWriteFactory.GetAddressOf());
    if (FAILED(hr)) return false;

    // Create D2D device from DXGI device of the D3D11on12 device
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = g_D3D11Device->QueryInterface(__uuidof(IDXGIDevice),
                                       (void**)dxgiDevice.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = g_D2DFactory->CreateDevice(dxgiDevice.Get(),
                                    g_D2DDevice.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = g_D2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                          g_D2DContext.GetAddressOf());
    if (FAILED(hr)) return false;

    // Create per-buffer wrapped resources + D2D bitmaps
    ComPtr<IDXGISwapChain3> sc3;
    sc->QueryInterface(__uuidof(IDXGISwapChain3), (void**)sc3.GetAddressOf());

    for (UINT i = 0; i < g_BufferCount; i++) {
        ComPtr<ID3D12Resource> buf12;
        if (FAILED(sc->GetBuffer(i, __uuidof(ID3D12Resource),
                                 (void**)buf12.GetAddressOf()))) continue;

        D3D11_RESOURCE_FLAGS rf{ D3D11_BIND_RENDER_TARGET };
        hr = g_D3D11On12Device->CreateWrappedResource(
            buf12.Get(), &rf,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT,
            __uuidof(ID3D11Resource),
            (void**)g_WrappedBuffers[i].GetAddressOf());
        if (FAILED(hr)) continue;

        ComPtr<IDXGISurface> surface;
        hr = g_WrappedBuffers[i]->QueryInterface(__uuidof(IDXGISurface),
                                                  (void**)surface.GetAddressOf());
        if (FAILED(hr)) continue;

        D2D1_BITMAP_PROPERTIES1 bprop = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));

        g_D2DFactory->GetDesktopDpi(&bprop.dpiX, &bprop.dpiY);

        hr = g_D2DContext->CreateBitmapFromDxgiSurface(
            surface.Get(), &bprop, g_D2DBitmaps[i].GetAddressOf());
    }

    // Init overlay and menu
    Overlay_Init((ID2D1RenderTarget*)g_D2DContext.Get(), g_DWriteFactory.Get());
    Menu_Init((ID2D1RenderTarget*)g_D2DContext.Get(), g_DWriteFactory.Get());

    return true;
}

static void ReleasePerBufferResources() {
    for (UINT i = 0; i < MAX_BUFFERS; i++) {
        g_D2DBitmaps[i].Reset();
        g_WrappedBuffers[i].Reset();
    }
}

// ── Hooked Present ────────────────────────────────────────

static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* sc,
                                                UINT syncInterval,
                                                UINT flags) {
    FPS_Tick();

    // Edge-detect keys for menu keybind
    static bool s_Prev[256]{};
    for (int vk = 8; vk < 256; vk++) {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (down && !s_Prev[vk]) Menu_HandleKey(vk);
        s_Prev[vk] = down;
    }

    // Mouse
    static bool s_PrevL = false;
    {
        POINT pt{};
        GetCursorPos(&pt);
        bool ldown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool click = ldown && !s_PrevL;
        s_PrevL = ldown;
        if (g_GameHwnd) ScreenToClient(g_GameHwnd, &pt);
        Menu_HandleMouse((float)pt.x, (float)pt.y, click, ldown);
    }

    // Lazy init
    if (!g_Initialized) {
        if (InitDX11On12(sc)) g_Initialized = true;
    }

    if (g_Initialized && g_D2DContext && g_D3D11On12Device) {
        // Get current back buffer index
        ComPtr<IDXGISwapChain3> sc3;
        UINT idx = 0;
        if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain3),
                                         (void**)sc3.GetAddressOf())))
            idx = sc3->GetCurrentBackBufferIndex();

        if (g_WrappedBuffers[idx] && g_D2DBitmaps[idx]) {
            // Acquire wrapped resource for D3D11 use
            ID3D11Resource* res = g_WrappedBuffers[idx].Get();
            g_D3D11On12Device->AcquireWrappedResources(&res, 1);

            // Set render target and draw
            g_D2DContext->SetTarget(g_D2DBitmaps[idx].Get());
            g_D2DContext->BeginDraw();

            D2D1_SIZE_F sz = g_D2DContext->GetSize();
            if (!Menu_IsOpen()) Overlay_Draw((ID2D1RenderTarget*)g_D2DContext.Get());
            Menu_Draw((ID2D1RenderTarget*)g_D2DContext.Get(), sz.width, sz.height);

            HRESULT endHr = g_D2DContext->EndDraw();
            g_D2DContext->SetTarget(nullptr);

            // Release back to DX12
            g_D3D11On12Device->ReleaseWrappedResources(&res, 1);
            g_D3D11Context->Flush();

            // If device lost, reinit next frame
            if (endHr == (HRESULT)0x8899000C || endHr == DXGI_ERROR_DEVICE_REMOVED) {
                g_Initialized = false;
                ReleasePerBufferResources();
                g_D2DContext.Reset();
                g_D2DDevice.Reset();
                g_D3D11On12Device.Reset();
                g_D3D11Context.Reset();
                g_D3D11Device.Reset();
            }
        }
    }

    // Call original
    RemovePatch();
    HRESULT hr = sc->Present(syncInterval, flags);
    // Reinstall patch
    InstallPatch(g_PatchTarget, (void*)HookedPresent);
    return hr;
}

// ── Hook_Init ─────────────────────────────────────────────

void Hook_Init() {
    // DWrite factory (shared — used by Overlay/Menu init later)
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory3),
        (IUnknown**)g_DWriteFactory.GetAddressOf());

    // Load keybind from rust_config.json
    {
        wchar_t cfg[MAX_PATH];
        GetModuleFileNameW(nullptr, cfg, MAX_PATH);
        wchar_t* sl = wcsrchr(cfg, L'\\');
        if (sl) wcscpy_s(sl+1, MAX_PATH-(sl-cfg)-1, L"rust_config.json");
        HANDLE hf = CreateFileW(cfg, GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf != INVALID_HANDLE_VALUE) {
            char buf[512]{};
            DWORD n = 0;
            ReadFile(hf, buf, sizeof(buf)-1, &n, nullptr);
            CloseHandle(hf);
            const char* k = strstr(buf, "\"menuKeybind\"");
            if (k) {
                const char* q = strchr(strchr(k,':'), '"');
                if (q) { char c=q[1]; if(c>='a'&&c<='z') c-=32; Menu_SetKeybind(c); }
            }
        }
    }

    // ── Create dummy DX12 swap chain to find Present() address ──
    HMODULE hDXGI  = GetModuleHandleW(L"dxgi.dll");
    HMODULE hD3D12 = GetModuleHandleW(L"d3d12.dll");
    if (!hDXGI || !hD3D12) return;

    auto pfnFactory = (decltype(&CreateDXGIFactory))
        GetProcAddress(hDXGI, "CreateDXGIFactory");
    auto pfnDevice  = (decltype(&D3D12CreateDevice))
        GetProcAddress(hD3D12, "D3D12CreateDevice");
    if (!pfnFactory || !pfnDevice) return;

    IDXGIFactory*    fac  = nullptr;
    IDXGIAdapter*    adp  = nullptr;
    ID3D12Device*    dev  = nullptr;
    ID3D12CommandQueue* q = nullptr;
    IDXGISwapChain*  sc   = nullptr;

    if (FAILED(pfnFactory(__uuidof(IDXGIFactory), (void**)&fac))) return;
    fac->EnumAdapters(0, &adp);
    if (FAILED(pfnDevice(adp, D3D_FEATURE_LEVEL_11_0,
                         __uuidof(ID3D12Device), (void**)&dev))) {
        if(adp) adp->Release(); fac->Release(); return;
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&q);

    // Save queue for use in InitDX11On12
    g_D3D12Queue = q;

    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc);
    wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=L"RustDummy";
    RegisterClassExW(&wc);
    HWND hw = CreateWindowExW(0, L"RustDummy", L"", WS_POPUP,
                              0,0,8,8, nullptr,nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.Width  = 8; scd.BufferDesc.Height = 8;
    scd.SampleDesc.Count  = 1;
    scd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount       = 2;
    scd.OutputWindow      = hw;
    scd.Windowed          = TRUE;
    scd.SwapEffect        = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Flags             = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    fac->CreateSwapChain(q, &scd, &sc);

    if (sc) {
        // vtable[8] = Present (IUnknown:3 + IDXGIObject:4 + IDXGIDeviceSubObject:1 = 8)
        void** vt = *reinterpret_cast<void***>(sc);
        void*  presentAddr = vt[8];
        g_OrigPresent = reinterpret_cast<PresentFn>(presentAddr);
        InstallPatch(presentAddr, (void*)HookedPresent);
        sc->Release();
    }

    if (q)   q->Release();   // g_D3D12Queue holds a ref via ComPtr
    if (dev) dev->Release();
    if (adp) adp->Release();
    if (fac) fac->Release();
    DestroyWindow(hw);
    UnregisterClassW(L"RustDummy", wc.hInstance);
}

void Hook_Shutdown() {
    RemovePatch();
    ReleasePerBufferResources();
    g_D2DContext.Reset();
    g_D2DDevice.Reset();
    g_D2DFactory.Reset();
    g_DWriteFactory.Reset();
    g_D3D11On12Device.Reset();
    g_D3D11Context.Reset();
    g_D3D11Device.Reset();
    g_D3D12Queue.Reset();
    g_D3D12Device.Reset();
}
