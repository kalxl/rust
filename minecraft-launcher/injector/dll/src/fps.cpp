/*
 * Rust Client — FPS Counter
 * Uses QueryPerformanceCounter to measure real frame times.
 * Smoothed over a 1-second sliding window.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "fps.h"

static LARGE_INTEGER g_Freq      = {};
static LARGE_INTEGER g_LastTime  = {};
static float         g_FPS       = 0.0f;
static int           g_FrameCount = 0;
static double        g_Elapsed    = 0.0;

void FPS_Tick() {
    if (g_Freq.QuadPart == 0) {
        QueryPerformanceFrequency(&g_Freq);
        QueryPerformanceCounter(&g_LastTime);
        return;
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    double delta = static_cast<double>(now.QuadPart - g_LastTime.QuadPart)
                 / static_cast<double>(g_Freq.QuadPart);
    g_LastTime = now;

    g_FrameCount++;
    g_Elapsed += delta;

    // Update FPS every second
    if (g_Elapsed >= 1.0) {
        g_FPS       = static_cast<float>(g_FrameCount / g_Elapsed);
        g_FrameCount = 0;
        g_Elapsed    = 0.0;
    }
}

float FPS_Get() {
    return g_FPS;
}
