/*
 * Rust Client — In-Game D2D Overlay
 *
 * FPS counter and WASD keystrokes HUD.
 * Both widgets are draggable when the menu is open.
 * Positions are saved to rust_config.json.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1_3.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#include <cstdio>
#include <chrono>
#include <deque>
#include <cstring>

#include "overlay.h"
#include "fps.h"
#include "keystrokes.h"

using Microsoft::WRL::ComPtr;

// ── Colors ────────────────────────────────────────────────
static const D2D1_COLOR_F kBgPanel    = {0.09f, 0.07f, 0.04f, 0.82f};
static const D2D1_COLOR_F kBgKey      = {0.15f, 0.11f, 0.07f, 0.90f};
static const D2D1_COLOR_F kBgKeyPress = {0.48f, 0.32f, 0.18f, 0.95f};
static const D2D1_COLOR_F kBgDrag     = {0.25f, 0.18f, 0.10f, 0.70f};
static const D2D1_COLOR_F kTextMain   = {0.91f, 0.85f, 0.75f, 1.00f};
static const D2D1_COLOR_F kTextSub    = {0.55f, 0.46f, 0.36f, 1.00f};
static const D2D1_COLOR_F kAccent     = {0.72f, 0.51f, 0.35f, 1.00f};
static const D2D1_COLOR_F kGreen      = {0.50f, 0.69f, 0.41f, 1.00f};
static const D2D1_COLOR_F kYellow     = {0.83f, 0.66f, 0.26f, 1.00f};
static const D2D1_COLOR_F kRed        = {0.75f, 0.31f, 0.23f, 1.00f};

// ── Brushes ───────────────────────────────────────────────
static ComPtr<ID2D1SolidColorBrush> g_BrBgPanel, g_BrBgKey, g_BrBgKeyPress;
static ComPtr<ID2D1SolidColorBrush> g_BrBgDrag;
static ComPtr<ID2D1SolidColorBrush> g_BrTextMain, g_BrTextSub, g_BrAccent;
static ComPtr<ID2D1SolidColorBrush> g_BrGreen, g_BrYellow, g_BrRed;

// ── Text formats ─────────────────────────────────────────
static ComPtr<IDWriteTextFormat> g_FmtLarge;
static ComPtr<IDWriteTextFormat> g_FmtSmall;
static ComPtr<IDWriteTextFormat> g_FmtKey;

// ── Widget positions (draggable) ─────────────────────────
static float g_FpsX = 10.0f,  g_FpsY = 10.0f;
static float g_KsX  = 10.0f,  g_KsY  = 140.0f;

// ── Drag state ────────────────────────────────────────────
static int   g_Dragging    = 0;   // 0=none 1=fps 2=keys
static float g_DragOffX    = 0, g_DragOffY = 0;
static bool  g_PrevHeld    = false;

// ── CPS ───────────────────────────────────────────────────
static std::deque<std::chrono::steady_clock::time_point> g_Clicks;
static bool g_PrevLMB = false;

// ── Layout ────────────────────────────────────────────────
static const float kPad   = 7.0f;
static const float kKeySz = 26.0f;
static const float kKeyGp = 4.0f;

static const float kFpsW  = 110.0f;
static const float kFpsH  = 56.0f;  // FPS number + CPS row

static const float kKsW   = 110.0f;
// keystroke panel height: W row + ASD row + Space + gaps + pad*2
static const float kKsH   = kPad + kKeySz + kKeyGp + kKeySz + kKeyGp + kKeySz + kPad;

// ── Helpers ───────────────────────────────────────────────
static D2D1_RECT_F Rc(float x, float y, float w, float h) {
    return D2D1::RectF(x, y, x+w, y+h);
}

static void FRR(ID2D1RenderTarget* rt, ID2D1Brush* br,
                float x, float y, float w, float h, float r = 3.0f) {
    D2D1_ROUNDED_RECT rr = {Rc(x,y,w,h), r, r};
    rt->FillRoundedRectangle(rr, br);
}

static void Txt(ID2D1RenderTarget* rt, IDWriteTextFormat* fmt, ID2D1Brush* br,
                const wchar_t* t, float x, float y, float w, float h) {
    rt->DrawText(t, (UINT32)wcslen(t), fmt, Rc(x,y,w,h), br);
}

static bool Hit(float mx, float my, float x, float y, float w, float h) {
    return mx >= x && mx <= x+w && my >= y && my <= y+h;
}

static void DrawKey(ID2D1RenderTarget* rt, const wchar_t* label,
                    float x, float y, float w, bool pressed) {
    FRR(rt, pressed ? g_BrBgKeyPress.Get() : g_BrBgKey.Get(), x, y, w, kKeySz);
    Txt(rt, g_FmtKey.Get(), g_BrTextMain.Get(), label, x, y+4.0f, w, kKeySz);
}

// ── Init ─────────────────────────────────────────────────

void Overlay_Init(ID2D1RenderTarget* rt, IDWriteFactory3* dwrite) {
    auto mk = [&](const D2D1_COLOR_F& c, ComPtr<ID2D1SolidColorBrush>& br) {
        rt->CreateSolidColorBrush(c, &br);
    };
    mk(kBgPanel,    g_BrBgPanel);
    mk(kBgKey,      g_BrBgKey);
    mk(kBgKeyPress, g_BrBgKeyPress);
    mk(kBgDrag,     g_BrBgDrag);
    mk(kTextMain,   g_BrTextMain);
    mk(kTextSub,    g_BrTextSub);
    mk(kAccent,     g_BrAccent);
    mk(kGreen,      g_BrGreen);
    mk(kYellow,     g_BrYellow);
    mk(kRed,        g_BrRed);

    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 22.0f, L"en-US", &g_FmtLarge);
    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &g_FmtSmall);
    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &g_FmtKey);

    if (g_FmtLarge) g_FmtLarge->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    if (g_FmtSmall) g_FmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    if (g_FmtKey)   g_FmtKey->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    Keys_Init();

    // Load saved positions from rust_config.json
    wchar_t cfgPath[MAX_PATH];
    GetModuleFileNameW(nullptr, cfgPath, MAX_PATH);
    wchar_t* sl = wcsrchr(cfgPath, L'\\');
    if (sl) wcscpy_s(sl+1, MAX_PATH-(sl-cfgPath)-1, L"rust_config.json");

    HANDLE hf = CreateFileW(cfgPath, GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, 0, nullptr);
    if (hf != INVALID_HANDLE_VALUE) {
        char buf[512] = {};
        DWORD n = 0;
        ReadFile(hf, buf, sizeof(buf)-1, &n, nullptr);
        CloseHandle(hf);
        // Parse "fpsX":val  "fpsY":val  "ksX":val  "ksY":val
        auto parseFloat = [&](const char* key, float& out) {
            const char* p = strstr(buf, key);
            if (p) { p = strchr(p, ':'); if (p) out = (float)atof(p+1); }
        };
        parseFloat("\"fpsX\"", g_FpsX);
        parseFloat("\"fpsY\"", g_FpsY);
        parseFloat("\"ksX\"",  g_KsX);
        parseFloat("\"ksY\"",  g_KsY);
    }
}

// ── Drag handling (called by menu when open) ──────────────

void Overlay_HandleDrag(float mx, float my, bool held) {
    if (held && !g_PrevHeld) {
        // Start drag
        if (Hit(mx, my, g_FpsX, g_FpsY, kFpsW, kFpsH)) {
            g_Dragging = 1;
            g_DragOffX = mx - g_FpsX;
            g_DragOffY = my - g_FpsY;
        } else if (Hit(mx, my, g_KsX, g_KsY, kKsW, kKsH)) {
            g_Dragging = 2;
            g_DragOffX = mx - g_KsX;
            g_DragOffY = my - g_KsY;
        }
    }

    if (!held) {
        if (g_Dragging != 0) {
            // Dropped — save positions to config
            wchar_t cfgPath[MAX_PATH];
            GetModuleFileNameW(nullptr, cfgPath, MAX_PATH);
            wchar_t* sl = wcsrchr(cfgPath, L'\\');
            if (sl) wcscpy_s(sl+1, MAX_PATH-(sl-cfgPath)-1, L"rust_config.json");

            // Read existing config
            char buf[1024] = {};
            HANDLE hf = CreateFileW(cfgPath, GENERIC_READ, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
            if (hf != INVALID_HANDLE_VALUE) {
                DWORD n = 0;
                ReadFile(hf, buf, sizeof(buf)-1, &n, nullptr);
                CloseHandle(hf);
            }

            // Write merged config
            // Simple approach: rewrite entire file with known keys
            char out[1024];
            // Extract menuKeybind if present
            char keybind[16] = "R";
            const char* kb = strstr(buf, "\"menuKeybind\"");
            if (kb) {
                const char* q = strchr(strchr(kb, ':'), '"');
                if (q && q[1] && q[2] == '"') { keybind[0] = q[1]; keybind[1] = 0; }
            }
            snprintf(out, sizeof(out),
                "{\n"
                "  \"menuKeybind\": \"%s\",\n"
                "  \"fpsX\": %.1f,\n"
                "  \"fpsY\": %.1f,\n"
                "  \"ksX\": %.1f,\n"
                "  \"ksY\": %.1f\n"
                "}\n",
                keybind, g_FpsX, g_FpsY, g_KsX, g_KsY);

            hf = CreateFileW(cfgPath, GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hf != INVALID_HANDLE_VALUE) {
                DWORD written;
                WriteFile(hf, out, (DWORD)strlen(out), &written, nullptr);
                CloseHandle(hf);
            }
        }
        g_Dragging = 0;
    }

    if (g_Dragging == 1 && held) {
        g_FpsX = mx - g_DragOffX;
        g_FpsY = my - g_DragOffY;
    } else if (g_Dragging == 2 && held) {
        g_KsX = mx - g_DragOffX;
        g_KsY = my - g_DragOffY;
    }

    g_PrevHeld = held;
}

// ── Draw ─────────────────────────────────────────────────

void Overlay_Draw(ID2D1RenderTarget* rt) {
    if (!g_BrTextMain) return;

    float fps = FPS_Get();
    KeyState keys = Keys_Get();

    // CPS
    auto now = std::chrono::steady_clock::now();
    if (keys.lmb && !g_PrevLMB) g_Clicks.push_back(now);
    g_PrevLMB = keys.lmb;
    while (!g_Clicks.empty() &&
           std::chrono::duration_cast<std::chrono::milliseconds>(
               now - g_Clicks.front()).count() > 1000)
        g_Clicks.pop_front();
    int cps = (int)g_Clicks.size();

    // ── FPS panel ─────────────────────────────────────────
    FRR(rt, g_BrBgPanel.Get(), g_FpsX, g_FpsY, kFpsW, kFpsH, 4.0f);

    wchar_t fpsBuf[16];
    swprintf_s(fpsBuf, L"%.0f", fps);
    ID2D1Brush* fpsBr = fps < 60.0f ? g_BrRed.Get()
                      : fps < 100.0f ? g_BrYellow.Get()
                      : g_BrGreen.Get();
    Txt(rt, g_FmtLarge.Get(), fpsBr,
        fpsBuf, g_FpsX + kPad, g_FpsY + 6, kFpsW - kPad*2, 30.0f);
    Txt(rt, g_FmtSmall.Get(), g_BrTextSub.Get(),
        L"FPS", g_FpsX + kPad + 50.0f, g_FpsY + 14.0f, 30.0f, 14.0f);

    wchar_t cpsBuf[16];
    swprintf_s(cpsBuf, L"CPS  %d", cps);
    Txt(rt, g_FmtSmall.Get(), g_BrTextSub.Get(),
        cpsBuf, g_FpsX + kPad, g_FpsY + 38.0f, kFpsW - kPad*2, 14.0f);

    // ── Keystrokes panel ──────────────────────────────────
    FRR(rt, g_BrBgPanel.Get(), g_KsX, g_KsY, kKsW, kKsH, 4.0f);

    float cy = g_KsY + kPad;

    // W
    float wX = g_KsX + (kKsW - kKeySz) * 0.5f;
    DrawKey(rt, L"W", wX, cy, kKeySz, keys.w);
    cy += kKeySz + kKeyGp;

    // A S D
    float rowX = g_KsX + (kKsW - (kKeySz*3 + kKeyGp*2)) * 0.5f;
    DrawKey(rt, L"A", rowX,                          cy, kKeySz, keys.a);
    DrawKey(rt, L"S", rowX + kKeySz + kKeyGp,        cy, kKeySz, keys.s);
    DrawKey(rt, L"D", rowX + (kKeySz + kKeyGp)*2,    cy, kKeySz, keys.d);
    cy += kKeySz + kKeyGp;

    // Space
    DrawKey(rt, L"SPACE", g_KsX + kPad, cy, kKsW - kPad*2, keys.space);
}
