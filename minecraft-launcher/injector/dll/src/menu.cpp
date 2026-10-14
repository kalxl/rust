/*
 * Rust Client — In-Game ClickGUI
 * Full module grid with categories, scrolling, enable/disable.
 * Drawn entirely with D2D1 — no emoji, no external fonts for icons.
 * Colors: brown/beige palette.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1_3.h>
#include <d2d1helper.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

#include "menu.h"

using Microsoft::WRL::ComPtr;

// ── Palette ───────────────────────────────────────────────
static const D2D1_COLOR_F kBg        = {0.09f, 0.06f, 0.03f, 0.95f};
static const D2D1_COLOR_F kBgHeader  = {0.06f, 0.04f, 0.02f, 1.00f};
static const D2D1_COLOR_F kBgCard    = {0.13f, 0.09f, 0.05f, 1.00f};
static const D2D1_COLOR_F kBgCardHov = {0.20f, 0.14f, 0.08f, 1.00f};
static const D2D1_COLOR_F kBgOn      = {0.48f, 0.32f, 0.16f, 1.00f};
static const D2D1_COLOR_F kBgOff     = {0.16f, 0.11f, 0.06f, 1.00f};
static const D2D1_COLOR_F kBgTab     = {0.11f, 0.07f, 0.04f, 1.00f};
static const D2D1_COLOR_F kBgTabSel  = {0.22f, 0.15f, 0.08f, 1.00f};
static const D2D1_COLOR_F kText      = {0.91f, 0.85f, 0.75f, 1.00f};
static const D2D1_COLOR_F kTextSub   = {0.55f, 0.45f, 0.33f, 1.00f};
static const D2D1_COLOR_F kTextMuted = {0.32f, 0.24f, 0.16f, 1.00f};
static const D2D1_COLOR_F kAccent    = {0.72f, 0.51f, 0.35f, 1.00f};
static const D2D1_COLOR_F kAccentBt  = {0.85f, 0.64f, 0.44f, 1.00f};
static const D2D1_COLOR_F kIconClr   = {0.91f, 0.85f, 0.75f, 0.50f};

// ── Brushes ───────────────────────────────────────────────
static ComPtr<ID2D1SolidColorBrush> g_BrBg, g_BrHeader, g_BrCard, g_BrCardHov;
static ComPtr<ID2D1SolidColorBrush> g_BrOn, g_BrOff, g_BrTab, g_BrTabSel;
static ComPtr<ID2D1SolidColorBrush> g_BrText, g_BrTextSub, g_BrTextMuted;
static ComPtr<ID2D1SolidColorBrush> g_BrAccent, g_BrAccentBt, g_BrIcon;
static ComPtr<ID2D1SolidColorBrush> g_BrScrim;

// ── Text formats ─────────────────────────────────────────
static ComPtr<IDWriteTextFormat> g_FmtTitle;   // header "Rust"
static ComPtr<IDWriteTextFormat> g_FmtTab;     // category tabs
static ComPtr<IDWriteTextFormat> g_FmtCard;    // module name
static ComPtr<IDWriteTextFormat> g_FmtBtn;     // Enabled/Disabled
static ComPtr<IDWriteTextFormat> g_FmtSub;     // desc / small

// ── Module data ───────────────────────────────────────────
struct Module {
    const wchar_t* name;
    const wchar_t* desc;
    bool enabled;
};

struct Category {
    const wchar_t* name;
    std::vector<Module> modules;
};

static std::vector<Category> g_Cats;

static void InitModules() {
    g_Cats = {
        { L"HUD", {
            { L"Keystrokes",       L"Show WASD key presses",           true  },
            { L"FPS Counter",      L"Display frames per second",        true  },
            { L"CPS Counter",      L"Show clicks per second",           false },
            { L"Coordinates",      L"Show XYZ position",                false },
            { L"Armor HUD",        L"Show equipped armor",              false },
            { L"Clock",            L"Show local or in-game time",       false },
            { L"Compass",          L"Direction indicator",              false },
            { L"Speed Counter",    L"Show movement speed",              false },
            { L"Ping Counter",     L"Show server latency",              false },
            { L"Scoreboard",       L"Custom scoreboard display",        false },
            { L"Item Info",        L"Show held item durability",        false },
            { L"Better Hunger",    L"Hunger + saturation overlay",      false },
            { L"Break Progress",   L"Block break progress indicator",   false },
            { L"Potion Effects",   L"Show active potion effects",       false },
            { L"Paper Doll",       L"Show player model preview",        false },
        }},
        { L"Render", {
            { L"Motion Blur",      L"Smooth camera motion effect",      false },
            { L"No Hurt Cam",      L"Remove screen shake on damage",    false },
            { L"Fullbright",       L"Max brightness everywhere",        false },
            { L"Custom Sky",       L"Override sky color",               false },
            { L"Fog Modifier",     L"Change render fog distance",       false },
            { L"Anti Alias",       L"Smooth jagged edges",              false },
            { L"Chams",            L"Render entities through walls",    false },
            { L"ESP",              L"Entity bounding box overlay",      false },
            { L"Hitbox",           L"Show accurate entity hitboxes",    false },
            { L"Tracers",          L"Draw lines to entities",           false },
            { L"No Nausea",        L"Remove nausea screen effect",      false },
            { L"No Fire",          L"Remove fire overlay",              false },
        }},
        { L"Movement", {
            { L"Toggle Sprint",    L"Auto sprint without holding key",  false },
            { L"Toggle Sneak",     L"Auto sneak without holding key",   false },
            { L"Zoom",             L"Zoom in like optifine",            false },
            { L"No Slow",          L"Don't slow when using items",      false },
            { L"Speed",            L"Move faster than normal",          false },
            { L"Step",             L"Auto step up blocks",              false },
            { L"Fly",              L"Creative-like flying",             false },
            { L"Null Movement",    L"Register only latest move key",    false },
            { L"Raw Input",        L"Fix Minecraft input delay",        false },
            { L"Sens Multiplier",  L"Multiply mouse sensitivity",       false },
            { L"SnapLook",         L"Quickly look behind you",          false },
        }},
        { L"Utility", {
            { L"Auto GG",          L"Send gg at end of game",           false },
            { L"Anti AFK",         L"Prevent AFK kick",                 false },
            { L"Compact Chat",     L"Stack duplicate messages",         false },
            { L"Clear Chat",       L"Clear the chat history",           false },
            { L"Clear Scoreboard", L"Hide scoreboard from screen",      false },
            { L"Spam",             L"Send repeated chat messages",      false },
            { L"Inventory Sort",   L"Auto sort inventory",              false },
            { L"Auto Tool",        L"Switch to best tool automatically",false },
            { L"Reach Display",    L"Show attack reach distance",       false },
            { L"Direction HUD",    L"Compass rose direction display",   false },
        }},
        { L"Misc", {
            { L"Animations",       L"Custom item swing animations",     false },
            { L"Hit Color",        L"Change entity hit flash color",    false },
            { L"Block Outline",    L"Custom block selection outline",   false },
            { L"Crosshair",        L"Custom crosshair style",           false },
            { L"FOV Changer",      L"Change FOV beyond game limits",    false },
            { L"Freelook",         L"Move camera independently",        false },
            { L"Java Bobbing",     L"Java Edition view bobbing",        false },
            { L"No Fall Flash",    L"Remove fall damage screen flash",  false },
            { L"Arrow Counter",    L"Show arrows shot/remaining",       false },
            { L"Auto Perspective", L"Change perspective on events",     false },
        }},
    };
}

// ── State ─────────────────────────────────────────────────
static bool  g_MenuOpen  = false;
static int   g_Keybind   = 'R';
static float g_Alpha     = 0.0f;
static int   g_SelCat    = 0;       // selected category tab
static float g_ScrollY   = 0.0f;   // scroll offset in card area
static float g_ScrollVel = 0.0f;
static float g_MouseX    = 0, g_MouseY = 0;
static bool  g_Clicked   = false;
static float g_WheelDelta = 0.0f;

// Exported state
static ModuleState g_ModState = { true, true, false, 0.35f };

// ── Layout ────────────────────────────────────────────────
static const float kW       = 580.0f;
static const float kH       = 400.0f;
static const float kHeaderH = 44.0f;
static const float kTabH    = 34.0f;
static const float kCardW   = 160.0f;
static const float kCardH   = 72.0f;
static const float kGap     = 8.0f;
static const float kPad     = 14.0f;
static const float kBtnH    = 22.0f;
static const float kR       = 3.0f;
static const int   kCols    = 3;

// ── Helpers ───────────────────────────────────────────────

static D2D1_RECT_F Rc(float x, float y, float w, float h) {
    return D2D1::RectF(x, y, x+w, y+h);
}
static void FRR(ID2D1RenderTarget* rt, ID2D1Brush* br,
                float x, float y, float w, float h, float r = kR) {
    D2D1_ROUNDED_RECT rr = { Rc(x,y,w,h), r, r };
    rt->FillRoundedRectangle(rr, br);
}
static void Txt(ID2D1RenderTarget* rt, IDWriteTextFormat* fmt,
                ID2D1Brush* br, const wchar_t* t,
                float x, float y, float w, float h) {
    rt->DrawText(t, (UINT32)wcslen(t), fmt, Rc(x,y,w,h), br);
}
static bool Hit(float mx, float my, float x, float y, float w, float h) {
    return mx>=x && mx<=x+w && my>=y && my<=y+h;
}

// ── Icon drawing (keyboard, bars, blur, generic gear) ────
static void DrawIconKeyboard(ID2D1RenderTarget* rt, ID2D1Brush* br, float cx, float cy, float sz) {
    float s = sz/12.0f;
    rt->SetTransform(D2D1::Matrix3x2F::Scale(s,s) * D2D1::Matrix3x2F::Translation(cx-12*s, cy-12*s));
    D2D1_ROUNDED_RECT shell = { Rc(2,5,20,14), 1.5f, 1.5f };
    rt->DrawRoundedRectangle(shell, br, 1.2f);
    for (int i=0;i<5;i++) rt->FillRectangle(Rc(4.f+i*3.4f, 8.f, 2.4f, 2.4f), br);
    for (int i=0;i<4;i++) rt->FillRectangle(Rc(5.f+i*3.4f, 11.5f, 2.4f, 2.4f), br);
    rt->FillRectangle(Rc(7.f, 15.f, 10.f, 2.f), br);
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
}
static void DrawIconBars(ID2D1RenderTarget* rt, ID2D1Brush* br, float cx, float cy, float sz) {
    float s = sz/12.0f;
    rt->SetTransform(D2D1::Matrix3x2F::Scale(s,s) * D2D1::Matrix3x2F::Translation(cx-12*s, cy-12*s));
    rt->FillRectangle(Rc(3,13,4,8),  br);
    rt->FillRectangle(Rc(10,8,4,13), br);
    rt->FillRectangle(Rc(17,4,4,17), br);
    br->SetOpacity(br->GetOpacity()*0.5f);
    rt->FillRectangle(Rc(2,21.5f,20,1), br);
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
}
static void DrawIconGear(ID2D1RenderTarget* rt, ID2D1Brush* br, float cx, float cy, float sz) {
    float s = sz/12.0f;
    rt->SetTransform(D2D1::Matrix3x2F::Scale(s,s) * D2D1::Matrix3x2F::Translation(cx-12*s, cy-12*s));
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(12,12), 4.5f, 4.5f), br, 1.4f);
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(12,12), 8.0f, 8.0f), br, 1.1f);
    // 8 tick marks
    for (int i=0;i<8;i++) {
        float ang = i * 3.14159f / 4.0f;
        float ix = 12+cosf(ang)*7.5f, iy = 12+sinf(ang)*7.5f;
        float ox = 12+cosf(ang)*9.5f, oy = 12+sinf(ang)*9.5f;
        rt->DrawLine(D2D1::Point2F(ix,iy), D2D1::Point2F(ox,oy), br, 1.5f);
    }
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
}

// Pick icon by category
static void DrawCatIcon(ID2D1RenderTarget* rt, ID2D1Brush* br, int cat, float cx, float cy, float sz) {
    br->SetOpacity(0.5f);
    switch(cat) {
        case 0: DrawIconKeyboard(rt, br, cx, cy, sz); break;
        case 1: DrawIconBars(rt, br, cx, cy, sz); break;
        default: DrawIconGear(rt, br, cx, cy, sz); break;
    }
    br->SetOpacity(1.0f);
}

// ── Init ─────────────────────────────────────────────────
void Menu_Init(ID2D1RenderTarget* rt, IDWriteFactory3* dwrite) {
    InitModules();

    auto mk = [&](const D2D1_COLOR_F& c, ComPtr<ID2D1SolidColorBrush>& br) {
        rt->CreateSolidColorBrush(c, &br);
    };
    mk(kBg,        g_BrBg);
    mk(kBgHeader,  g_BrHeader);
    mk(kBgCard,    g_BrCard);
    mk(kBgCardHov, g_BrCardHov);
    mk(kBgOn,      g_BrOn);
    mk(kBgOff,     g_BrOff);
    mk(kBgTab,     g_BrTab);
    mk(kBgTabSel,  g_BrTabSel);
    mk(kText,      g_BrText);
    mk(kTextSub,   g_BrTextSub);
    mk(kTextMuted, g_BrTextMuted);
    mk(kAccent,    g_BrAccent);
    mk(kAccentBt,  g_BrAccentBt);
    mk(kIconClr,   g_BrIcon);
    rt->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0.5f), &g_BrScrim);

    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD,      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        15.0f, L"en-US", &g_FmtTitle);

    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.0f, L"en-US", &g_FmtTab);
    if (g_FmtTab) g_FmtTab->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    if (g_FmtTab) g_FmtTab->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.5f, L"en-US", &g_FmtCard);

    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD,      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        10.0f, L"en-US", &g_FmtBtn);
    if (g_FmtBtn) g_FmtBtn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    if (g_FmtBtn) g_FmtBtn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        9.5f,  L"en-US", &g_FmtSub);
}

// ── Menu_Draw ────────────────────────────────────────────
void Menu_Draw(ID2D1RenderTarget* rt, float sw, float sh) {
    float target = g_MenuOpen ? 1.0f : 0.0f;
    g_Alpha += (target - g_Alpha) * 0.20f;
    if (g_Alpha < 0.01f) { g_Clicked = false; return; }

    float a  = g_Alpha;
    float px = (sw - kW) * 0.5f;
    float py = (sh - kH) * 0.5f;

    // Scrim
    g_BrScrim->SetOpacity(a * 0.6f);
    rt->FillRectangle(D2D1::RectF(0,0,sw,sh), g_BrScrim.Get());

    // Panel
    g_BrBg->SetOpacity(a);
    FRR(rt, g_BrBg.Get(), px, py, kW, kH, 4.0f);

    // ── Header ────────────────────────────────────────────
    g_BrHeader->SetOpacity(a);
    FRR(rt, g_BrHeader.Get(), px, py, kW, kHeaderH, 4.0f);
    rt->FillRectangle(Rc(px, py+kHeaderH-4, kW, 4), g_BrHeader.Get());

    // Logo/title
    g_BrAccent->SetOpacity(a);
    Txt(rt, g_FmtTitle.Get(), g_BrAccent.Get(), L"Rust", px+kPad, py+13, 60, 20);

    // Hint
    g_BrTextMuted->SetOpacity(a);
    wchar_t hint[32];
    swprintf_s(hint, L"[%c] close", (wchar_t)g_Keybind);
    Txt(rt, g_FmtSub.Get(), g_BrTextMuted.Get(), hint, px+kW-90, py+16, 82, 14);

    // ── Category tabs ─────────────────────────────────────
    float tabY  = py + kHeaderH;
    float tabW  = kW / (float)g_Cats.size();

    for (int i = 0; i < (int)g_Cats.size(); i++) {
        float tx = px + i * tabW;
        bool sel = (i == g_SelCat);
        bool hov = Hit(g_MouseX, g_MouseY, tx, tabY, tabW, kTabH);

        auto* tbr = sel ? g_BrTabSel.Get() : (hov ? g_BrCardHov.Get() : g_BrTab.Get());
        tbr->SetOpacity(a);
        rt->FillRectangle(Rc(tx, tabY, tabW, kTabH), tbr);

        // Accent line under selected tab
        if (sel) {
            g_BrAccent->SetOpacity(a);
            rt->FillRectangle(Rc(tx+4, tabY+kTabH-2, tabW-8, 2), g_BrAccent.Get());
        }

        auto* tbr2 = sel ? g_BrAccentBt.Get() : (hov ? g_BrText.Get() : g_BrTextSub.Get());
        tbr2->SetOpacity(a);
        Txt(rt, g_FmtTab.Get(), tbr2, g_Cats[i].name, tx, tabY, tabW, kTabH);

        if (g_Clicked && hov) {
            g_SelCat = i;
            g_ScrollY = 0;
        }
    }

    // ── Card grid (clipped to card area) ─────────────────
    float areaY = tabY + kTabH;
    float areaH = kH - kHeaderH - kTabH;
    float areaX = px;

    // Clip
    D2D1_RECT_F clip = Rc(areaX, areaY, kW, areaH);
    rt->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Scroll velocity
    g_ScrollVel += g_WheelDelta * 18.0f;
    g_ScrollY   -= g_ScrollVel;
    g_ScrollVel *= 0.78f;
    g_WheelDelta = 0.0f;

    // Clamp scroll
    auto& mods = g_Cats[g_SelCat].modules;
    int   rows = ((int)mods.size() + kCols - 1) / kCols;
    float contentH = rows * (kCardH + kGap) + kGap;
    float maxScroll = contentH - areaH;
    if (maxScroll < 0) maxScroll = 0;
    if (g_ScrollY < 0) g_ScrollY = 0;
    if (g_ScrollY > maxScroll) g_ScrollY = maxScroll;

    float startX = areaX + kPad;
    float startY = areaY + kPad - g_ScrollY;

    for (int i = 0; i < (int)mods.size(); i++) {
        int col = i % kCols;
        int row = i / kCols;
        float cx = startX + col * (kCardW + kGap);
        float cy = startY + row * (kCardH + kGap);

        // Skip if fully outside clip
        if (cy + kCardH < areaY || cy > areaY + areaH) continue;

        bool hov = Hit(g_MouseX, g_MouseY, cx, cy, kCardW, kCardH)
                   && g_MouseY > areaY && g_MouseY < areaY + areaH;

        auto* cbr = hov ? g_BrCardHov.Get() : g_BrCard.Get();
        cbr->SetOpacity(a);
        FRR(rt, cbr, cx, cy, kCardW, kCardH);

        // Enabled dot indicator top-right
        if (mods[i].enabled) {
            g_BrAccent->SetOpacity(a);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx+kCardW-10, cy+10), 4, 4), g_BrAccent.Get());
        }

        // Module name
        g_BrText->SetOpacity(a);
        Txt(rt, g_FmtCard.Get(), g_BrText.Get(),
            mods[i].name, cx+8, cy+8, kCardW-16, 16);

        // Desc
        g_BrTextSub->SetOpacity(a*0.8f);
        Txt(rt, g_FmtSub.Get(), g_BrTextSub.Get(),
            mods[i].desc, cx+8, cy+26, kCardW-16, 22);

        // Enable/disable button
        float bx = cx+8, by = cy+kCardH-kBtnH-6, bw = kCardW-16;
        bool  bon = mods[i].enabled;

        auto* bbr = bon ? g_BrOn.Get() : g_BrOff.Get();
        bbr->SetOpacity(a);
        FRR(rt, bbr, bx, by, bw, kBtnH);

        auto* lbr = bon ? g_BrAccentBt.Get() : g_BrTextMuted.Get();
        lbr->SetOpacity(a);
        Txt(rt, g_FmtBtn.Get(), lbr,
            bon ? L"Enabled" : L"Disabled",
            bx, by, bw, kBtnH);

        // Click toggle
        if (g_Clicked && hov &&
            Hit(g_MouseX, g_MouseY, bx, by, bw, kBtnH)) {
            mods[i].enabled = !mods[i].enabled;
        }
    }

    rt->PopAxisAlignedClip();

    // Scrollbar
    if (contentH > areaH) {
        float sbW  = 3.0f;
        float sbX  = px + kW - sbW - 2;
        float ratio = areaH / contentH;
        float sbH  = areaH * ratio;
        float sbY  = areaY + (g_ScrollY / contentH) * areaH;

        g_BrOn->SetOpacity(a * 0.5f);
        FRR(rt, g_BrOn.Get(), sbX, sbY, sbW, sbH, 2.0f);
    }

    // Sync exported state from module list
    for (auto& m : g_Cats[0].modules) {
        if (wcscmp(m.name, L"Keystrokes") == 0) g_ModState.keystrokesEnabled = m.enabled;
        if (wcscmp(m.name, L"FPS Counter") == 0) g_ModState.fpsEnabled = m.enabled;
    }
    for (auto& m : g_Cats[1].modules) {
        if (wcscmp(m.name, L"Motion Blur") == 0) g_ModState.motionBlurEnabled = m.enabled;
    }

    g_Clicked    = false;
}

// ── Input ─────────────────────────────────────────────────
void Menu_HandleKey(int vk) {
    if (vk == g_Keybind) g_MenuOpen = !g_MenuOpen;
}

void Menu_HandleMouse(float x, float y, bool clicked) {
    g_MouseX  = x;
    g_MouseY  = y;
    g_Clicked = clicked;
}

void Menu_HandleWheel(float delta) {
    g_WheelDelta += delta;
}

bool        Menu_IsOpen()    { return g_MenuOpen || g_Alpha > 0.01f; }
ModuleState Menu_GetState()  { return g_ModState; }
void        Menu_SetKeybind(int vk) { g_Keybind = vk; }
