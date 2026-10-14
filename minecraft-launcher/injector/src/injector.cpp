/*
 * Rust Client — DLL Injector
 * Injects RustDLL.dll into Minecraft.Windows.exe
 *
 * Usage: RustInjector.exe [path_to_dll]
 * Called by the Electron launcher after Minecraft launches.
 */

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <tlhelp32.h>
#include <shlwapi.h>
#include <stdio.h>
#include <string>
#include <chrono>
#include <thread>

#pragma comment(lib, "Shlwapi.lib")

// ── Helpers ───────────────────────────────────────────────

static void Log(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list args;
    va_start(args, fmt);
    vswprintf_s(buf, fmt, args);
    va_end(args);
    wprintf(L"[RustInjector] %s\n", buf);
    fflush(stdout);
}

// Find a process by executable name, retry for up to `timeoutMs`
static DWORD FindProcess(const wchar_t* exeName, int timeoutMs = 30000) {
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);

        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                    CloseHandle(snap);
                    return pe.th32ProcessID;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);

        Log(L"Waiting for %s…", exeName);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    return 0;
}

// Check if our DLL is already loaded in the target process
static bool IsDllLoaded(DWORD pid, const wchar_t* dllName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool found = false;

    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, dllName) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

// Core injection via LoadLibraryW remote thread
static bool Inject(DWORD pid, const std::wstring& dllPath) {
    // Open target process
    HANDLE hProc = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);

    if (!hProc) {
        Log(L"OpenProcess failed: %lu", GetLastError());
        return false;
    }

    // Allocate memory in target for the DLL path string
    size_t pathBytes = (dllPath.size() + 1) * sizeof(wchar_t);
    LPVOID remMem = VirtualAllocEx(hProc, nullptr, pathBytes,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remMem) {
        Log(L"VirtualAllocEx failed: %lu", GetLastError());
        CloseHandle(hProc);
        return false;
    }

    // Write the DLL path
    if (!WriteProcessMemory(hProc, remMem, dllPath.c_str(), pathBytes, nullptr)) {
        Log(L"WriteProcessMemory failed: %lu", GetLastError());
        VirtualFreeEx(hProc, remMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    // Get LoadLibraryW address (same in all processes on same OS)
    LPTHREAD_START_ROUTINE loadLib = (LPTHREAD_START_ROUTINE)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");

    if (!loadLib) {
        Log(L"Could not find LoadLibraryW");
        VirtualFreeEx(hProc, remMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    // Create remote thread to call LoadLibraryW(dllPath)
    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0,
                                        loadLib, remMem, 0, nullptr);
    if (!hThread) {
        Log(L"CreateRemoteThread failed: %lu", GetLastError());
        VirtualFreeEx(hProc, remMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    // Wait for LoadLibrary to complete (max 10s)
    WaitForSingleObject(hThread, 10000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProc, remMem, 0, MEM_RELEASE);
    CloseHandle(hProc);

    if (exitCode == 0) {
        Log(L"LoadLibrary returned NULL — injection may have failed");
        return false;
    }

    return true;
}

// ── Entry point ───────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Resolve DLL path
    std::wstring dllPath;

    if (argc >= 2) {
        // Convert narrow to wide
        int len = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
        dllPath.resize(len);
        MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &dllPath[0], len);
    } else {
        // Default: same folder as injector
        wchar_t selfPath[MAX_PATH];
        GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
        PathRemoveFileSpecW(selfPath);
        dllPath = std::wstring(selfPath) + L"\\RustDLL.dll";
    }

    // Verify DLL exists
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log(L"DLL not found: %s", dllPath.c_str());
        return 1;
    }

    Log(L"DLL path: %s", dllPath.c_str());
    Log(L"Waiting for Minecraft.Windows.exe…");

    // Wait for Minecraft to start (up to 60s)
    DWORD pid = FindProcess(L"Minecraft.Windows.exe", 60000);
    if (pid == 0) {
        Log(L"Minecraft.Windows.exe not found after 60s");
        return 1;
    }

    Log(L"Found Minecraft PID: %lu", pid);

    // Small delay to let the game initialize its DX12 device
    Log(L"Waiting for game to initialize…");
    std::this_thread::sleep_for(std::chrono::milliseconds(4000));

    // Check if already injected
    if (IsDllLoaded(pid, L"RustDLL.dll")) {
        Log(L"RustDLL.dll already loaded");
        return 0;
    }

    Log(L"Injecting…");
    if (Inject(pid, dllPath)) {
        Log(L"Injection successful");
        // Output a machine-readable marker for the Electron launcher
        wprintf(L"INJECT_SUCCESS\n");
        fflush(stdout);
        return 0;
    } else {
        Log(L"Injection failed");
        wprintf(L"INJECT_FAIL\n");
        fflush(stdout);
        return 1;
    }
}
