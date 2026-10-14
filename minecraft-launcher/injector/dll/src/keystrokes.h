#pragma once
#include <windows.h>

struct KeyState {
    bool w, a, s, d, space, lmb, rmb;
};

// Install low-level keyboard/mouse hooks (call once)
void Keys_Init();
void Keys_Shutdown();

// Returns current key state snapshot
KeyState Keys_Get();
