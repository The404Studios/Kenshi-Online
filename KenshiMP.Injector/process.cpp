#include "process.h"
#include <Windows.h>
#include <Shlwapi.h>
#include <TlHelp32.h>
#include <string>

#pragma comment(lib, "shlwapi.lib")

namespace kmp {

std::wstring FindKenshiPath() {
    // Try Steam registry
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\WOW6432Node\\Valve\\Steam",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t steamPath[MAX_PATH] = {};
        DWORD size = sizeof(steamPath);
        if (RegQueryValueExW(hKey, L"InstallPath", nullptr, nullptr,
                            reinterpret_cast<LPBYTE>(steamPath), &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);

            // Check common Kenshi location
            std::wstring kenshiPath = std::wstring(steamPath) +
                L"\\steamapps\\common\\Kenshi";
            if (PathFileExistsW((kenshiPath + L"\\kenshi_x64.exe").c_str())) {
                return kenshiPath;
            }
        }
        RegCloseKey(hKey);
    }

    // Check Program Files (x86) directly
    std::wstring defaultPath = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Kenshi";
    if (PathFileExistsW((defaultPath + L"\\kenshi_x64.exe").c_str())) {
        return defaultPath;
    }

    return L"";
}

bool LaunchKenshi(const std::wstring& gamePath) {
    // Method 1: Launch via Steam protocol (preferred - handles Steam DRM)
    HINSTANCE result = ShellExecuteW(nullptr, L"open",
        L"steam://rungameid/233860", nullptr, nullptr, SW_SHOWNORMAL);

    if (reinterpret_cast<intptr_t>(result) > 32) {
        return true; // Steam launch successful
    }

    // Method 2: Direct launch (fallback)
    std::wstring exePath = gamePath + L"\\kenshi_x64.exe";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    if (CreateProcessW(exePath.c_str(), nullptr, nullptr, nullptr, FALSE,
                       0, nullptr, gamePath.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    return false;
}

} // namespace kmp
