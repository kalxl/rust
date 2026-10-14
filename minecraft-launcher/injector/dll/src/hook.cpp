/*
 * Rust Client — DX12 / DXGI Hook
 *
 * Uses kiero-style vtable extraction to find the real Present pointer.
 *
 * DX12 vtable layout (from kiero):
 *   [0..43]   = ID3D12Device      (44 methods)
 *   [44..62]  = ID3D12CommandQueue (19 methods)
 *   [63..71]  = ID3D12CommandAllocator (9 methods)
 *   [72..131] = ID3D12GraphicsCommandList (60 methods)
 *   [132..149]= IDXGISwapChain    (18 methods)
 *
 * IDXGISwapChain::Present is slot 8 within the swap chain vtable,
 * which is global index 132 + 8 = 140.
 *
 * A named mutex "RustClient" is created in DllMain so the launcher
 * can detect successful injection by polling OpenMutex.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d2d1_3.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include "hook.h"
#include "overlay.h"
#include "fps.h"
#include "menu.h"

using Microsoft::WRL::ComPtr;

// ── Globals ───────────────────────────────────────────────

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

static PresentFn  g_OrigPresent  = nullptr;
static bool       g_Initialized  = false;
static HWND       g_GameHwnd     = nullptr;

// D2D resources
static ComPtr<ID2D1Factory3>     g_D2DFactory;
static ComPtr<IDWriteFactory3>   g_DWriteFactory;
static ComPtr<ID2D1RenderTarget> g_RenderTarget;
static ComPtr<IDXGISurface>      g_DXGISurface;

// ── MinHook-style trampoline patch ───────────────────────
// We write a 14-byte absolute jmp: FF 25 00000000 [8-byte addr]
// This patches the function bytes directly so it works across
// all swap chain instances, not just the dummy one.

static uint8_t  g_OrigBytes[14] = {};
static void*    g_PatchAddr     = nullptr;

static void WritePatch(void* target, void* detour) {
    DWORD old;
    VirtualProtect(target, 14, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_OrigBytes, target, 14);

    uint8_t* p = (uint8_t*)target;
    // FF 25 00 00 00 00  = JMP QWORD PTR [RIP+0]
    p[0] = 0xFF; p[1] = 0x25;
    p[2] = p[3] = p[4] = p[5] = 0;
    *(uint64_t*)(p + 6) = (uint64_t)detour;

    VirtualProtect(target, 14, old, &old);
    g_PatchAddr = target;
}

static void RemovePatch() {
    if (!g_PatchAddr) return;
    DWORD old;
    VirtualProtect(g_PatchAddr, 14, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_PatchAddr, g_OrigBytes, 14);
    VirtualProtect(g_PatchAddr, 14, old, &old);
}

// Call original Present by temporarily restoring bytes
static HRESULT CallOrigPresent(IDXGISwapChain* sc, UINT si, UINT flags) {
    RemovePatch();
    HRESULT hr = sc->Present(si, flags);
    WritePatch(g_PatchAddr, (void*)/* re-set below */ g_PatchAddr);
    return hr;
}

// ── Init D2D on top of the swap chain back buffer ─────────

static bool InitD2D(IDXGISwapChain* swapChain) {
    if (g_RenderTarget) return true;

    // Release old surface first
    g_DXGISurface.Reset();
    g_RenderTarget.Reset();

    HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&g_DXGISurface));
    if (FAILED(hr)) return false;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));

    hr = g_D2DFactory->CreateDxgiSurfaceRenderTarget(
        g_DXGISurface.Get(), &props, &g_RenderTarget);

    if (FAILED(hr)) return false;

    Overlay_Init(g_RenderTarget.Get(), g_DWriteFactory.Get());
    Menu_Init(g_RenderTarget.Get(), g_DWriteFactory.Get());

    DXGI_SWAP_CHAIN_DESC desc{};
    swapChain->GetDesc(&desc);
    g_GameHwnd = desc.OutputWindow;

    return true;
}

// ── Hooked Present ────────────────────────────────────────

static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain,
                                                UINT syncInterval,
                                                UINT flags) {
    FPS_Tick();

    // Edge-detect all keys for menu keybind
    static bool s_PrevKeys[256] = {};
    for (int vk = 8; vk < 256; vk++) {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool was  = s_PrevKeys[vk];
        s_PrevKeys[vk] = down;
        if (down && !was) Menu_HandleKey(vk);
    }

    // Mouse for menu interaction
    static bool s_PrevLClick = false;
    {
        POINT pt{};
        GetCursorPos(&pt);
        bool ldown   = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool clicked = ldown && !s_PrevLClick;
        s_PrevLClick = ldown;
        if (g_GameHwnd) ScreenToClient(g_GameHwnd, &pt);
        Menu_HandleMouse((float)pt.x, (float)pt.y, clicked, ldown);
    }

    // Mouse wheel
    // (wheel delta is accumulated externally via WndProc hook — skip for now,
    //  menu scroll will work via keyboard or drag)

    // Lazy init / reinit on resize
    if (!g_Initialized || !g_RenderTarget) {
        if (InitD2D(swapChain)) g_Initialized = true;
    }

    if (g_Initialized && g_RenderTarget) {
        D2D1_SIZE_F sz = g_RenderTarget->GetSize();
        g_RenderTarget->BeginDraw();
        if (!Menu_IsOpen()) Overlay_Draw(g_RenderTarget.Get());
        Menu_Draw(g_RenderTarget.Get(), sz.width, sz.height);
        HRESULT endHr = g_RenderTarget->EndDraw();
        if (endHr == (HRESULT)0x8899000C) {
            g_RenderTarget.Reset();
            g_DXGISurface.Reset();
            g_Initialized = false;
        }
    }

    // Call original Present (patch is a trampoline — restore+call+repatch)
    return g_OrigPresent(swapChain, syncInterval, flags);
}

// ── Hook_Init ─────────────────────────────────────────────

void Hook_Init() {
    // D2D + DWrite factories
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory3),
        reinterpret_cast<void**>(g_D2DFactory.GetAddressOf()));

    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory3),
        reinterpret_cast<IUnknown**>(g_DWriteFactory.GetAddressOf()));

    // Load keybind from rust_config.json
    {
        wchar_t cfgPath[MAX_PATH];
        GetModuleFileNameW(nullptr, cfgPath, MAX_PATH);
        wchar_t* slash = wcsrchr(cfgPath, L'\\');
        if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash - cfgPath) - 1, L"rust_config.json");

        HANDLE hf = CreateFileW(cfgPath, GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf != INVALID_HANDLE_VALUE) {
            char buf[512] = {};
            DWORD n = 0;
            ReadFile(hf, buf, sizeof(buf)-1, &n, nullptr);
            CloseHandle(hf);
            const char* k = strstr(buf, "\"menuKeybind\"");
            if (k) {
                const char* q = strchr(strchr(k, ':'), '"');
                if (q) {
                    char c = q[1];
                    if (c >= 'a' && c <= 'z') c -= 32;
                    Menu_SetKeybind((int)c);
                }
            }
        }
    }

    // ── Get swap chain vtable using kiero's DX12 method ──
    // We need the REAL dxgi.dll swap chain vtable, not just any vtable.
    // Method: create dummy device+queue+swapchain, copy vtable, then
    // patch the ACTUAL function bytes in the DLL so it affects all instances.

    HMODULE hDXGI  = GetModuleHandleW(L"dxgi.dll");
    HMODULE hD3D12 = GetModuleHandleW(L"d3d12.dll");
    if (!hDXGI || !hD3D12) return;

    auto pfnCreateDXGIFactory = (decltype(&CreateDXGIFactory))
        GetProcAddress(hDXGI, "CreateDXGIFactory");
    auto pfnD3D12CreateDevice = (decltype(&D3D12CreateDevice))
        GetProcAddress(hD3D12, "D3D12CreateDevice");
    if (!pfnCreateDXGIFactory || !pfnD3D12CreateDevice) return;

    IDXGIFactory*    factory  = nullptr;
    ID3D12Device*    device   = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGISwapChain*  sc       = nullptr;

    if (FAILED(pfnCreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory))) return;

    IDXGIAdapter* adapter = nullptr;
    factory->EnumAdapters(0, &adapter);

    if (FAILED(pfnD3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                                    __uuidof(ID3D12Device), (void**)&device))) {
        if (adapter) adapter->Release();
        factory->Release();
        return;
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&queue);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"RustDX12Dummy";
    RegisterClassExW(&wc);
    HWND hw = CreateWindowExW(0, L"RustDX12Dummy", L"", WS_POPUP,
                              0, 0, 8, 8, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferDesc.Width  = 8;
    scd.BufferDesc.Height = 8;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count  = 1;
    scd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount       = 2;
    scd.OutputWindow      = hw;
    scd.Windowed          = TRUE;
    scd.SwapEffect        = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Flags             = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    factory->CreateSwapChain(queue, &scd, &sc);

    if (sc) {
        // vtable slot 8 of IDXGISwapChain = Present
        void** vtable = *reinterpret_cast<void***>(sc);
        void*  presentPtr = vtable[8];

        // Store original and patch the function bytes directly
        g_OrigPresent = reinterpret_cast<PresentFn>(presentPtr);
        WritePatch(presentPtr, reinterpret_cast<void*>(HookedPresent));

        sc->Release();
    }

    if (queue)   queue->Release();
    if (device)  device->Release();
    if (adapter) adapter->Release();
    if (factory) factory->Release();
    DestroyWindow(hw);
    UnregisterClassW(L"RustDX12Dummy", wc.hInstance);
}

void Hook_Shutdown() {
    RemovePatch();
    g_RenderTarget.Reset();
    g_DXGISurface.Reset();
    g_D2DFactory.Reset();
    g_DWriteFactory.Reset();
}
