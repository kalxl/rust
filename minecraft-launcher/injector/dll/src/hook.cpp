/*
 * Rust Client — DX12 / DXGI Hook
 *
 * Strategy:
 *  1. Create a dummy DX12 device + swap chain to get the vtable.
 *  2. Read IDXGISwapChain::Present pointer from the vtable (slot 8).
 *  3. Patch it with our own Present, which calls the D2D overlay each frame.
 *
 * This is the same approach used by Flarial, ImGui overlays, etc.
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

// Track previous key state to detect press (not hold)
static bool g_PrevMenuKey = false;
// Track previous mouse state
static bool g_PrevLClick  = false;

using Microsoft::WRL::ComPtr;

// ── VMT hook helpers ──────────────────────────────────────

// Read a function pointer from a COM vtable
template<typename T>
static T VMT(void* obj, int slot) {
    return (*reinterpret_cast<T**>(obj))[slot];
}

// Patch a vtable slot with our function, returns original pointer
static void* PatchVMT(void* obj, int slot, void* newFn) {
    void** vtable = *reinterpret_cast<void***>(obj);
    void*  orig   = vtable[slot];

    DWORD oldProt;
    VirtualProtect(&vtable[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt);
    vtable[slot] = newFn;
    VirtualProtect(&vtable[slot], sizeof(void*), oldProt, &oldProt);

    return orig;
}

// ── Globals ───────────────────────────────────────────────

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

static PresentFn  g_OrigPresent   = nullptr;
static bool       g_Initialized   = false;
static HWND       g_GameHwnd      = nullptr;

// D2D/DWrite resources (created once on first Present)
static ComPtr<ID2D1Factory3>        g_D2DFactory;
static ComPtr<IDWriteFactory3>      g_DWriteFactory;
static ComPtr<ID2D1RenderTarget>    g_RenderTarget;
static ComPtr<IDXGISurface>         g_DXGISurface;

// ── Init D2D on top of the swap chain back buffer ─────────

static bool InitD2D(IDXGISwapChain* swapChain) {
    if (g_RenderTarget) return true;

    HRESULT hr;

    // Get the back buffer as a DXGI surface
    hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&g_DXGISurface));
    if (FAILED(hr)) return false;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));

    hr = g_D2DFactory->CreateDxgiSurfaceRenderTarget(
        g_DXGISurface.Get(), &props, &g_RenderTarget);

    return SUCCEEDED(hr);
}

// ── Hooked Present ────────────────────────────────────────

static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain,
                                                UINT syncInterval,
                                                UINT flags) {
    // Tick FPS counter every frame
    FPS_Tick();

    // Poll menu keybind (detect press edge, not held state)
    {
        // Track previous key state across frames for edge detection
        static bool s_PrevKeys[256] = {};
        for (int vk = 8; vk < 256; vk++) {
            bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
            bool wasDown = s_PrevKeys[vk];
            s_PrevKeys[vk] = down;
            // Only fire on key-down edge (press, not hold)
            // Menu_HandleKey internally checks if vk == g_Keybind
            if (down && !wasDown) {
                Menu_HandleKey(vk);
            }
        }
    }

    // Poll mouse position for menu interaction
    {
        POINT pt{};
        GetCursorPos(&pt);
        bool ldown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool clicked = ldown && !g_PrevLClick;
        g_PrevLClick  = ldown;
        if (g_GameHwnd) ScreenToClient(g_GameHwnd, &pt);
        Menu_HandleMouse((float)pt.x, (float)pt.y, clicked);
    }

    // Lazy init D2D
    if (!g_Initialized) {
        if (InitD2D(swapChain)) {
            Overlay_Init(g_RenderTarget.Get(), g_DWriteFactory.Get());
            Menu_Init(g_RenderTarget.Get(), g_DWriteFactory.Get());

            // Grab game window for mouse coords
            DXGI_SWAP_CHAIN_DESC desc{};
            swapChain->GetDesc(&desc);
            g_GameHwnd = desc.OutputWindow;

            g_Initialized = true;
        }
    }

    // Draw overlay (FPS + keystrokes) when menu is closed
    if (g_Initialized && g_RenderTarget) {
        ModuleState ms = Menu_GetState();
        g_RenderTarget->BeginDraw();

        // Get render target size for menu centering
        D2D1_SIZE_F sz = g_RenderTarget->GetSize();

        // Draw HUD overlay only when menu is closed
        if (!Menu_IsOpen()) {
            Overlay_Draw(g_RenderTarget.Get());
        }

        // Draw menu on top (handles its own visibility/alpha)
        Menu_Draw(g_RenderTarget.Get(), sz.width, sz.height);

        g_RenderTarget->EndDraw();
    }

    return g_OrigPresent(swapChain, syncInterval, flags);
}

// ── Hook_Init ─────────────────────────────────────────────

void Hook_Init() {
    // Create D2D factory
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                      __uuidof(ID2D1Factory3),
                      reinterpret_cast<void**>(g_D2DFactory.GetAddressOf()));

    // Create DWrite factory
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                        __uuidof(IDWriteFactory3),
                        reinterpret_cast<IUnknown**>(g_DWriteFactory.GetAddressOf()));

    // Load keybind from rust_config.json (written by the launcher)
    {
        wchar_t selfPath[MAX_PATH];
        GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
        // Walk up to find resources folder
        wchar_t cfgPath[MAX_PATH];
        wcscpy_s(cfgPath, selfPath);
        // Replace filename with rust_config.json
        wchar_t* lastSlash = wcsrchr(cfgPath, L'\\');
        if (lastSlash) {
            wcscpy_s(lastSlash + 1, MAX_PATH - (lastSlash - cfgPath) - 1,
                     L"rust_config.json");
        }

        HANDLE hFile = CreateFileW(cfgPath, GENERIC_READ, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            char buf[512] = {};
            DWORD read = 0;
            ReadFile(hFile, buf, sizeof(buf) - 1, &read, nullptr);
            CloseHandle(hFile);

            // Simple parse: find "menuKeybind":"X"
            const char* key = strstr(buf, "\"menuKeybind\"");
            if (key) {
                const char* colon = strchr(key, ':');
                if (colon) {
                    const char* q1 = strchr(colon, '"');
                    if (q1) {
                        char keyChar = q1[1];
                        if (keyChar >= 'a' && keyChar <= 'z') keyChar -= 32;
                        Menu_SetKeybind((int)keyChar);
                    }
                }
            }
        }
    }

    // ── Create dummy D3D12 device to read swap chain vtable ──

    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device)))) return;

    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC qDesc{};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&queue)))) return;

    // Need a real (but hidden) window for swap chain creation
    HWND dummyHwnd = CreateWindowExW(0, L"STATIC", L"", WS_POPUP,
                                     0, 0, 8, 8, nullptr, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);

    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        DestroyWindow(dummyHwnd);
        return;
    }

    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.Width              = 8;
    scDesc.Height             = 8;
    scDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.SampleDesc.Count   = 1;
    scDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount        = 2;
    scDesc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(factory->CreateSwapChainForHwnd(queue.Get(), dummyHwnd,
                                               &scDesc, nullptr, nullptr,
                                               &sc1))) {
        DestroyWindow(dummyHwnd);
        return;
    }

    // VMT slot 8 = IDXGISwapChain::Present
    g_OrigPresent = reinterpret_cast<PresentFn>(
        PatchVMT(sc1.Get(), 8, reinterpret_cast<void*>(HookedPresent)));

    // Cleanup dummy resources (hook stays active via vtable patch)
    DestroyWindow(dummyHwnd);
}

void Hook_Shutdown() {
    // Restore original Present if we have it
    // (In practice the game is exiting, so this is a best-effort cleanup)
    g_RenderTarget.Reset();
    g_DXGISurface.Reset();
    g_D2DFactory.Reset();
    g_DWriteFactory.Reset();
}
