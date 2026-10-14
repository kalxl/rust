/*
 * Rust Client — In-Game D2D Overlay
 *
 * Draws directly into Minecraft's swap chain via D2D1.
 * Renders: FPS counter, WASD keystrokes, LMB/RMB, CPS.
 *
 * Color scheme: brown/beige matching the launcher UI.
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

#include "overlay.h"
#include "fps.h"
#include "keystrokes.h"

using Microsoft::WRL::ComPtr;

// ── Colors (brown/beige palette) ─────────────────────────
static const D2D1_COLOR_F kBgPanel    = {0.09f, 0.07f, 0.04f, 0.72f};
static const D2D1_COLOR_F kBgKey      = {0.15f, 0.11f, 0.07f, 0.85f};
static const D2D1_COLOR_F kBgKeyPress = {0.48f, 0.32f, 0.18f, 0.90f};
static const D2D1_COLOR_F kTextMain   = {0.91f, 0.85f, 0.75f, 1.00f};
static const D2D1_COLOR_F kTextSub    = {0.55f, 0.46f, 0.36f, 1.00f};
static const D2D1_COLOR_F kAccent     = {0.72f, 0.51f, 0.35f, 1.00f};
static const D2D1_COLOR_F kGreen      = {0.50f, 0.69f, 0.41f, 1.00f};
static const D2D1_COLOR_F kYellow     = {0.83f, 0.66f, 0.26f, 1.00f};
static const D2D1_COLOR_F kRed        = {0.75f, 0.31f, 0.23f, 1.00f};

// ── Brushes ───────────────────────────────────────────────
static ComPtr<ID2D1SolidColorBrush> g_BrBgPanel;
static ComPtr<ID2D1SolidColorBrush> g_BrBgKey;
static ComPtr<ID2D1SolidColorBrush> g_BrBgKeyPress;
static ComPtr<ID2D1SolidColorBrush> g_BrTextMain;
static ComPtr<ID2D1SolidColorBrush> g_BrTextSub;
static ComPtr<ID2D1SolidColorBrush> g_BrAccent;
static ComPtr<ID2D1SolidColorBrush> g_BrGreen;
static ComPtr<ID2D1SolidColorBrush> g_BrYellow;
static ComPtr<ID2D1SolidColorBrush> g_BrRed;

// ── Text formats ─────────────────────────────────────────
static ComPtr<IDWriteTextFormat> g_FmtLarge;   // FPS number
static ComPtr<IDWriteTextFormat> g_FmtSmall;   // labels / values
static ComPtr<IDWriteTextFormat> g_FmtKey;     // key letters

static IDWriteFactory3* g_DWrite = nullptr;

// ── CPS tracking ─────────────────────────────────────────
static std::deque<std::chrono::steady_clock::time_point> g_Clicks;
static bool g_PrevLMB = false;

// ── Layout constants ─────────────────────────────────────
static const float kX      = 10.0f;   // left edge
static const float kY      = 10.0f;   // top edge
static const float kW      = 110.0f;  // panel width
static const float kPad    = 7.0f;
static const float kKeySz  = 26.0f;   // key square size
static const float kKeyGap = 4.0f;

// ── Helpers ───────────────────────────────────────────────

static D2D1_RECT_F Rect(float x, float y, float w, float h) {
    return D2D1::RectF(x, y, x + w, y + h);
}

static void DrawRoundRect(ID2D1RenderTarget* rt,
                          ID2D1Brush* fill,
                          float x, float y, float w, float h,
                          float r = 3.0f) {
    D2D1_ROUNDED_RECT rr = {Rect(x, y, w, h), r, r};
    rt->FillRoundedRectangle(rr, fill);
}

static void DrawText(ID2D1RenderTarget* rt,
                     IDWriteTextFormat* fmt,
                     ID2D1Brush* brush,
                     const wchar_t* text,
                     float x, float y, float w, float h) {
    rt->DrawText(text, (UINT32)wcslen(text), fmt,
                 D2D1::RectF(x, y, x + w, y + h), brush);
}

static void DrawKey(ID2D1RenderTarget* rt,
                    const wchar_t* label,
                    float x, float y, float w,
                    bool pressed) {
    auto* bg = pressed ? g_BrBgKeyPress.Get() : g_BrBgKey.Get();
    DrawRoundRect(rt, bg, x, y, w, kKeySz);
    DrawText(rt, g_FmtKey.Get(), g_BrTextMain.Get(),
             label, x, y + 4.0f, w, kKeySz);
}

// ── Init ─────────────────────────────────────────────────

void Overlay_Init(ID2D1RenderTarget* rt, IDWriteFactory3* dwrite) {
    g_DWrite = dwrite;

    // Create brushes
    auto mk = [&](const D2D1_COLOR_F& c, ComPtr<ID2D1SolidColorBrush>& br) {
        rt->CreateSolidColorBrush(c, &br);
    };
    mk(kBgPanel,    g_BrBgPanel);
    mk(kBgKey,      g_BrBgKey);
    mk(kBgKeyPress, g_BrBgKeyPress);
    mk(kTextMain,   g_BrTextMain);
    mk(kTextSub,    g_BrTextSub);
    mk(kAccent,     g_BrAccent);
    mk(kGreen,      g_BrGreen);
    mk(kYellow,     g_BrYellow);
    mk(kRed,        g_BrRed);

    // Create text formats
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
}

// ── Draw (called every frame) ────────────────────────────

void Overlay_Draw(ID2D1RenderTarget* rt) {
    if (!g_BrTextMain) return;

    float fps = FPS_Get();
    KeyState keys = Keys_Get();

    // ── CPS tracking ─────────────────────────────────────
    auto now = std::chrono::steady_clock::now();
    if (keys.lmb && !g_PrevLMB) g_Clicks.push_back(now);
    g_PrevLMB = keys.lmb;

    // Remove clicks older than 1 second
    while (!g_Clicks.empty() &&
           std::chrono::duration_cast<std::chrono::milliseconds>(
               now - g_Clicks.front()).count() > 1000) {
        g_Clicks.pop_front();
    }
    int cps = (int)g_Clicks.size();

    // ── Panel background ──────────────────────────────────
    float panelH = kPad
                 + 36.0f   // FPS row
                 + 6.0f
                 + 14.0f   // CPS row
                 + 10.0f
                 + kKeySz  // W row
                 + kKeyGap
                 + kKeySz  // ASD row
                 + kKeyGap
                 + kKeySz  // Space
                 + kPad;

    DrawRoundRect(rt, g_BrBgPanel.Get(), kX, kY, kW, panelH, 4.0f);

    float cx = kX + kW * 0.5f;  // center x of panel
    float cy = kY + kPad;

    // ── FPS ───────────────────────────────────────────────
    wchar_t fpsBuf[16];
    swprintf_s(fpsBuf, L"%.0f", fps);

    // Color FPS by value
    ID2D1Brush* fpsBrush = g_BrGreen.Get();
    if (fps < 60.0f)  fpsBrush = g_BrRed.Get();
    else if (fps < 100.0f) fpsBrush = g_BrYellow.Get();

    DrawText(rt, g_FmtLarge.Get(), fpsBrush,
             fpsBuf, kX + kPad, cy, kW - kPad * 2, 32.0f);

    DrawText(rt, g_FmtSmall.Get(), g_BrTextSub.Get(),
             L"FPS", kX + kPad + 52.0f, cy + 12.0f, 30.0f, 14.0f);

    cy += 36.0f + 6.0f;

    // ── CPS ───────────────────────────────────────────────
    wchar_t cpsBuf[16];
    swprintf_s(cpsBuf, L"CPS  %d", cps);
    DrawText(rt, g_FmtSmall.Get(), g_BrTextSub.Get(),
             cpsBuf, kX + kPad, cy, kW - kPad * 2, 14.0f);

    cy += 14.0f + 10.0f;

    // ── Keystroke grid ────────────────────────────────────
    // W centered
    float wX = kX + (kW - kKeySz) * 0.5f;
    DrawKey(rt, L"W", wX, cy, kKeySz, keys.w);
    cy += kKeySz + kKeyGap;

    // A S D
    float rowX = kX + (kW - (kKeySz * 3 + kKeyGap * 2)) * 0.5f;
    DrawKey(rt, L"A", rowX,                          cy, kKeySz, keys.a);
    DrawKey(rt, L"S", rowX + kKeySz + kKeyGap,       cy, kKeySz, keys.s);
    DrawKey(rt, L"D", rowX + (kKeySz + kKeyGap) * 2, cy, kKeySz, keys.d);
    cy += kKeySz + kKeyGap;

    // Space bar (full width)
    DrawKey(rt, L"SPACE", kX + kPad, cy, kW - kPad * 2, keys.space);
}
