#pragma once
// ANSI (system codepage) <-> UTF-8 conversion utilities.
// Uses Win32 MultiByteToWideChar / WideCharToMultiByte.

#ifdef _WIN32
#include <Windows.h>
#include <string>

namespace kmp {

// Convert system ANSI codepage bytes to UTF-8.
// Returns UTF-8 string, or original bytes on failure.
inline std::string AnsiToUtf8(const char* ansi, int len) {
    if (len <= 0) return {};
    // ANSI -> wide
    int wlen = MultiByteToWideChar(CP_ACP, 0, ansi, len, nullptr, 0);
    if (wlen <= 0) return std::string(ansi, len);
    std::wstring wide(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, ansi, len, wide.data(), wlen);
    // wide -> UTF-8
    int u8len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return std::string(ansi, len);
    std::string utf8(u8len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, utf8.data(), u8len, nullptr, nullptr);
    return utf8;
}

// Convert UTF-8 bytes to system ANSI codepage.
// Returns ANSI string, or original bytes on failure.
inline std::string Utf8ToAnsi(const char* utf8, int len) {
    if (len <= 0) return {};
    // UTF-8 -> wide
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, len, nullptr, 0);
    if (wlen <= 0) return std::string(utf8, len);
    std::wstring wide(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, len, wide.data(), wlen);
    // wide -> ANSI
    int alen = WideCharToMultiByte(CP_ACP, 0, wide.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (alen <= 0) return std::string(utf8, len);
    std::string ansi(alen, '\0');
    WideCharToMultiByte(CP_ACP, 0, wide.data(), wlen, ansi.data(), alen, nullptr, nullptr);
    return ansi;
}

// In-place ANSI->UTF-8 conversion in a fixed buffer.
// Used inside __try blocks where std::string is forbidden.
// Returns new length, or original len on failure. buf must be at least bufSize bytes.
inline int AnsiToUtf8InPlace(char* buf, int len, int bufSize) {
    if (len <= 0 || len >= bufSize - 1) return len;
    // Check if already pure ASCII — no conversion needed
    bool allAscii = true;
    for (int i = 0; i < len; i++) {
        if (static_cast<unsigned char>(buf[i]) > 127) { allAscii = false; break; }
    }
    if (allAscii) return len;
    // ANSI -> wide (stack buffers for SEH safety)
    wchar_t wide[256];
    int wlen = MultiByteToWideChar(CP_ACP, 0, buf, len, wide, 256);
    if (wlen <= 0) return len;
    // wide -> UTF-8
    char utf8[256];
    int u8len = WideCharToMultiByte(CP_UTF8, 0, wide, wlen, utf8, 256, nullptr, nullptr);
    if (u8len <= 0 || u8len >= bufSize) return len;
    memcpy(buf, utf8, u8len);
    buf[u8len] = '\0';
    return u8len;
}

} // namespace kmp
#endif
