/*
 * Rust Client — Keystroke Tracker
 * Uses GetAsyncKeyState to poll key states each frame.
 * No hooks needed — avoids anti-cheat detection vectors.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "keystrokes.h"

void Keys_Init()    {}
void Keys_Shutdown() {}

static inline bool IsDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

KeyState Keys_Get() {
    KeyState s{};
    s.w     = IsDown('W');
    s.a     = IsDown('A');
    s.s     = IsDown('S');
    s.d     = IsDown('D');
    s.space = IsDown(VK_SPACE);
    s.lmb   = IsDown(VK_LBUTTON);
    s.rmb   = IsDown(VK_RBUTTON);
    return s;
}
