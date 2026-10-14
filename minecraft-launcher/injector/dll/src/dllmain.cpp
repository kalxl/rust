/*
 * Rust Client — DLL Entry Point
 * Loaded into Minecraft.Windows.exe by the injector.
 * Spawns a worker thread to set up the DX12 hook.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "hook.h"

static DWORD WINAPI WorkerThread(LPVOID) {
    Hook_Init();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    }
    if (reason == DLL_PROCESS_DETACH) {
        Hook_Shutdown();
    }
    return TRUE;
}
