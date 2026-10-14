#pragma once
#include <cstdint>

// Call once per frame (from hooked Present)
void FPS_Tick();

// Returns the current smoothed FPS
float FPS_Get();
