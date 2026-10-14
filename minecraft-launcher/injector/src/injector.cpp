/*
 * Rust Client — DLL Injector
 * Injects RustDLL.dll into Minecraft.Windows.exe
 *
 * Usage: RustInjector.exe [path_to_dll]
 * Called by the Electron launcher after Minecraft launches.
 *
 * Does NOT require administrator privileges.
 * Works by granting ALL_APPLICATION_PACKAGES read/execute access to the DLL
 * before injection so the UWP sandbox can load it.
 */

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <tlhelp32.h>
#include <shlwapi.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdio.h>
#include <string>
#include <chrono>
#include <thread>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Advapi32.lib")

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

// ── UWP DLL permission fix ────────────────────────────────
//
// UWP processes run inside an AppContainer. When LoadLibrary is called from
// within the AppContainer, the loader checks that the DLL file has an ACE
// granting read+execute to the ALL_APPLICATION_PACKAGES SID (S-1-15-2-1).
// Without this ACE the load silently fails (returns NULL) even if injection
// itself succeeds.  We add it here at runtime — no admin needed because we
// own the file.

static bool GrantAppContainerAccess(const std::wstring& filePath) {
    // Build the well-known SID for ALL_APPLICATION_PACKAGES
    PSID pSid = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-15-2-1", &pSid)) {
        Log(L"ConvertStringSidToSid failed: %lu", GetLastError());
        return false;
    }

    // Read the existing DACL from the file
    PACL pOldDacl = nullptr;
    PSECURITY_DESCRIPTOR pSD = nullptr;
    DWORD err = GetNamedSecurityInfoW(
        filePath.c_str(), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr, nullptr, &pOldDacl, nullptr, &pSD);

    if (err != ERROR_SUCCESS) {
        Log(L"GetNamedSecurityInfo failed: %lu", err);
        LocalFree(pSid);
        return false;
    }

    // Check if the ACE already exists so we don't add duplicates
    if (pOldDacl) {
        ACL_SIZE_INFORMATION aclInfo{};
        GetAclInformation(pOldDacl, &aclInfo, sizeof(aclInfo), AclSizeInformation);
        for (DWORD i = 0; i < aclInfo.AceCount; ++i) {
            LPVOID ace = nullptr;
            if (GetAce(pOldDacl, i, &ace)) {
                auto* accessAce = reinterpret_cast<ACCESS_ALLOWED_ACE*>(ace);
                PSID aceSid = reinterpret_cast<PSID>(&accessAce->SidStart);
                if (EqualSid(aceSid, pSid)) {
                    // Already has the ACE — we're good
                    LocalFree(pSid);
                    LocalFree(pSD);
                    return true;
                }
            }
        }
    }

    // Build the new explicit access entry
    EXPLICIT_ACCESS_W ea{};
    ea.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
    ea.grfAccessMode        = GRANT_ACCESS;
    ea.grfInheritance       = NO_INHERITANCE;
    ea.Trustee.TrusteeForm  = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType  = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea.Trustee.ptstrName    = reinterpret_cast<LPWSTR>(pSid);

    PACL pNewDacl = nullptr;
    err = SetEntriesInAclW(1, &ea, pOldDacl, &pNewDacl);
    if (err != ERROR_SUCCESS) {
        Log(L"SetEntriesInAcl failed: %lu", err);
        LocalFree(pSid);
        LocalFree(pSD);
        return false;
    }

    // Write the updated DACL back — succeeds without admin if we own the file
    err = SetNamedSecurityInfoW(
        const_cast<LPWSTR>(filePath.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr, nullptr, pNewDacl, nullptr);

    LocalFree(pNewDacl);
    LocalFree(pSid);
    LocalFree(pSD);

    if (err != ERROR_SUCCESS) {
        Log(L"SetNamedSecurityInfo failed: %lu", err);
        return false;
    }

    Log(L"Granted ALL_APPLICATION_PACKAGES access to DLL");
    return true;
}

// ── Process helpers ───────────────────────────────────────

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

// ── Core injection ────────────────────────────────────────
//
// UWP (AppContainer) processes run at Low integrity but are NOT protected
// processes.  A standard user token has PROCESS_CREATE_THREAD /
// PROCESS_VM_WRITE access to lower-integrity targets without elevation,
// provided the DLL file itself has the ALL_APPLICATION_PACKAGES ACE (handled
// above).  We request only the minimum access flags needed.

static bool Inject(DWORD pid, const std::wstring& dllPath) {
    HANDLE hProc = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);

    if (!hProc) {
        Log(L"OpenProcess failed (err %lu) — launcher may need to run once as admin to set up permissions",
            GetLastError());
        return false;
    }

    size_t pathBytes = (dllPath.size() + 1) * sizeof(wchar_t);
    LPVOID remMem = VirtualAllocEx(hProc, nullptr, pathBytes,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remMem) {
        Log(L"VirtualAllocEx failed: %lu", GetLastError());
        CloseHandle(hProc);
        return false;
    }

    if (!WriteProcessMemory(hProc, remMem, dllPath.c_str(), pathBytes, nullptr)) {
        Log(L"WriteProcessMemory failed: %lu", GetLastError());
        VirtualFreeEx(hProc, remMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    LPTHREAD_START_ROUTINE loadLib = (LPTHREAD_START_ROUTINE)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");

    if (!loadLib) {
        Log(L"Could not find LoadLibraryW");
        VirtualFreeEx(hProc, remMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0,
                                        loadLib, remMem, 0, nullptr);
    if (!hThread) {
        Log(L"CreateRemoteThread failed: %lu", GetLastError());
        VirtualFreeEx(hProc, remMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    WaitForSingleObject(hThread, 10000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProc, remMem, 0, MEM_RELEASE);
    CloseHandle(hProc);

    if (exitCode == 0) {
        Log(L"LoadLibrary returned NULL — DLL may be missing ALL_APPLICATION_PACKAGES permission");
        return false;
    }

    return true;
}

// ── Entry point ───────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::wstring dllPath;

    if (argc >= 2) {
        int len = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
        dllPath.resize(len);
        MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &dllPath[0], len);
    } else {
        wchar_t selfPath[MAX_PATH];
        GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
        PathRemoveFileSpecW(selfPath);
        dllPath = std::wstring(selfPath) + L"\\RustDLL.dll";
    }

    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log(L"DLL not found: %s", dllPath.c_str());
        return 1;
    }

    Log(L"DLL path: %s", dllPath.c_str());

    // Grant ALL_APPLICATION_PACKAGES access so UWP sandbox can load the DLL
    if (!GrantAppContainerAccess(dllPath)) {
        Log(L"Warning: could not set DLL permissions — injection may fail");
    }

    Log(L"Waiting for Minecraft.Windows.exe…");
    DWORD pid = FindProcess(L"Minecraft.Windows.exe", 60000);
    if (pid == 0) {
        Log(L"Minecraft.Windows.exe not found after 60s");
        return 1;
    }

    Log(L"Found Minecraft PID: %lu", pid);
    Log(L"Waiting for game to initialize…");
    std::this_thread::sleep_for(std::chrono::milliseconds(4000));

    if (IsDllLoaded(pid, L"RustDLL.dll")) {
        Log(L"RustDLL.dll already loaded");
        return 0;
    }

    Log(L"Injecting…");
    if (Inject(pid, dllPath)) {
        Log(L"Injection successful");
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
