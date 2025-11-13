#include "Utilities.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>
#include <chrono>
#include <fstream>
#include <psapi.h>

namespace ReKenshi {
namespace Utils {

//=============================================================================
// StringUtils Implementation
//=============================================================================

std::string StringUtils::WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();

    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

std::wstring StringUtils::Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();

    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

std::string StringUtils::ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::string StringUtils::ToUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

std::string StringUtils::Trim(const std::string& str) {
    return TrimRight(TrimLeft(str));
}

std::string StringUtils::TrimLeft(const std::string& str) {
    auto it = std::find_if(str.begin(), str.end(), [](char c) {
        return !std::isspace(c);
    });
    return std::string(it, str.end());
}

std::string StringUtils::TrimRight(const std::string& str) {
    auto it = std::find_if(str.rbegin(), str.rend(), [](char c) {
        return !std::isspace(c);
    });
    return std::string(str.begin(), it.base());
}

std::vector<std::string> StringUtils::Split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;

    while (std::getline(ss, item, delimiter)) {
        result.push_back(item);
    }

    return result;
}

std::string StringUtils::Join(const std::vector<std::string>& strings, const std::string& delimiter) {
    if (strings.empty()) return "";

    std::ostringstream oss;
    oss << strings[0];

    for (size_t i = 1; i < strings.size(); i++) {
        oss << delimiter << strings[i];
    }

    return oss.str();
}

std::string StringUtils::ReplaceAll(const std::string& str, const std::string& from, const std::string& to) {
    std::string result = str;
    size_t pos = 0;

    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }

    return result;
}

bool StringUtils::StartsWith(const std::string& str, const std::string& prefix) {
    return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
}

bool StringUtils::EndsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string StringUtils::FormatBytes(size_t bytes) {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int unit = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        unit++;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return oss.str();
}

std::string StringUtils::FormatDuration(double milliseconds) {
    if (milliseconds < 1.0) {
        return std::to_string(static_cast<int>(milliseconds * 1000.0)) + " μs";
    } else if (milliseconds < 1000.0) {
        return std::to_string(static_cast<int>(milliseconds)) + " ms";
    } else if (milliseconds < 60000.0) {
        return std::to_string(static_cast<int>(milliseconds / 1000.0)) + " s";
    } else {
        int minutes = static_cast<int>(milliseconds / 60000.0);
        int seconds = static_cast<int>((milliseconds - minutes * 60000.0) / 1000.0);
        return std::to_string(minutes) + " min " + std::to_string(seconds) + " s";
    }
}

//=============================================================================
// MathUtils Implementation
//=============================================================================

float MathUtils::Distance2D(const Kenshi::Vector3& a, const Kenshi::Vector3& b) {
    float dx = a.x - b.x;
    float dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

float MathUtils::Distance3D(const Kenshi::Vector3& a, const Kenshi::Vector3& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

Kenshi::Vector3 MathUtils::Add(const Kenshi::Vector3& a, const Kenshi::Vector3& b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

Kenshi::Vector3 MathUtils::Subtract(const Kenshi::Vector3& a, const Kenshi::Vector3& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

Kenshi::Vector3 MathUtils::Multiply(const Kenshi::Vector3& v, float scalar) {
    return { v.x * scalar, v.y * scalar, v.z * scalar };
}

float MathUtils::DotProduct(const Kenshi::Vector3& a, const Kenshi::Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Kenshi::Vector3 MathUtils::CrossProduct(const Kenshi::Vector3& a, const Kenshi::Vector3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float MathUtils::Magnitude(const Kenshi::Vector3& v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

Kenshi::Vector3 MathUtils::Normalize(const Kenshi::Vector3& v) {
    float mag = Magnitude(v);
    if (mag < 0.0001f) {
        return { 0.0f, 0.0f, 0.0f };
    }
    return Multiply(v, 1.0f / mag);
}

float MathUtils::Lerp(float a, float b, float t) {
    return a + (b - a) * Clamp(t, 0.0f, 1.0f);
}

Kenshi::Vector3 MathUtils::LerpVector(const Kenshi::Vector3& a, const Kenshi::Vector3& b, float t) {
    t = Clamp(t, 0.0f, 1.0f);
    return {
        Lerp(a.x, b.x, t),
        Lerp(a.y, b.y, t),
        Lerp(a.z, b.z, t)
    };
}

float MathUtils::AngleBetween(const Kenshi::Vector3& a, const Kenshi::Vector3& b) {
    float dot = DotProduct(a, b);
    float magProduct = Magnitude(a) * Magnitude(b);
    if (magProduct < 0.0001f) {
        return 0.0f;
    }
    return acosf(Clamp(dot / magProduct, -1.0f, 1.0f));
}

Kenshi::Vector3 MathUtils::RotateAroundY(const Kenshi::Vector3& v, float radians) {
    float c = cosf(radians);
    float s = sinf(radians);
    return {
        v.x * c - v.z * s,
        v.y,
        v.x * s + v.z * c
    };
}

float MathUtils::Clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

int MathUtils::Clamp(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

float MathUtils::Round(float value, int decimals) {
    float multiplier = powf(10.0f, static_cast<float>(decimals));
    return roundf(value * multiplier) / multiplier;
}

//=============================================================================
// TimeUtils Implementation
//=============================================================================

uint64_t TimeUtils::GetCurrentTimestampMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

uint64_t TimeUtils::GetCurrentTimestampUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

void TimeUtils::SleepMs(uint32_t milliseconds) {
    Sleep(milliseconds);
}

std::string TimeUtils::FormatTimestamp(uint64_t timestampMs) {
    time_t seconds = timestampMs / 1000;
    uint32_t milliseconds = timestampMs % 1000;

    std::tm tm;
    localtime_s(&tm, &seconds);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << milliseconds;

    return oss.str();
}

double TimeUtils::GetElapsedMs(uint64_t startTimestamp) {
    return static_cast<double>(GetCurrentTimestampMs() - startTimestamp);
}

//=============================================================================
// MemoryUtils Implementation
//=============================================================================

std::string MemoryUtils::ReadString(uintptr_t address, size_t maxLength) {
    if (!IsValidPointer(address)) {
        return "";
    }

    __try {
        const char* str = reinterpret_cast<const char*>(address);
        size_t len = strnlen(str, maxLength);
        return std::string(str, len);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return "";
    }
}

bool MemoryUtils::CompareMemory(uintptr_t address, const std::vector<uint8_t>& pattern) {
    if (!IsValidPointer(address)) {
        return false;
    }

    __try {
        const uint8_t* mem = reinterpret_cast<const uint8_t*>(address);
        for (size_t i = 0; i < pattern.size(); i++) {
            if (mem[i] != pattern[i]) {
                return false;
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MemoryUtils::IsValidMemoryRegion(uintptr_t address) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == 0) {
        return false;
    }

    return (mbi.State == MEM_COMMIT) &&
           (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE));
}

size_t MemoryUtils::GetMemoryRegionSize(uintptr_t address) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == 0) {
        return 0;
    }
    return mbi.RegionSize;
}

uintptr_t MemoryUtils::GetModuleBase(const char* moduleName) {
    HMODULE hModule = GetModuleHandleA(moduleName);
    return reinterpret_cast<uintptr_t>(hModule);
}

size_t MemoryUtils::GetModuleSize(const char* moduleName) {
    HMODULE hModule = GetModuleHandleA(moduleName);
    if (!hModule) {
        return 0;
    }

    MODULEINFO modInfo;
    if (GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo))) {
        return modInfo.SizeOfImage;
    }

    return 0;
}

bool MemoryUtils::IsValidPointer(uintptr_t address) {
    if (address == 0) {
        return false;
    }

    return IsValidMemoryRegion(address);
}

bool MemoryUtils::IsReadableMemory(uintptr_t address, size_t size) {
    if (!IsValidPointer(address)) {
        return false;
    }

    __try {
        volatile char test;
        for (size_t i = 0; i < size; i++) {
            test = *reinterpret_cast<char*>(address + i);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

//=============================================================================
// FileUtils Implementation
//=============================================================================

bool FileUtils::FileExists(const std::string& path) {
    DWORD attrib = GetFileAttributesA(path.c_str());
    return (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY));
}

size_t FileUtils::GetFileSize(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return 0;
    }
    return static_cast<size_t>(file.tellg());
}

std::string FileUtils::ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool FileUtils::WriteFile(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file << content;
    return true;
}

std::string FileUtils::GetDirectory(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) {
        return "";
    }
    return path.substr(0, pos);
}

std::string FileUtils::GetFilename(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

std::string FileUtils::GetExtension(const std::string& path) {
    size_t pos = path.find_last_of('.');
    if (pos == std::string::npos) {
        return "";
    }
    return path.substr(pos);
}

bool FileUtils::CreateDirectory(const std::string& path) {
    return CreateDirectoryA(path.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

std::vector<std::string> FileUtils::ListFiles(const std::string& directory, const std::string& extension) {
    std::vector<std::string> files;
    std::string searchPath = directory + "\\*" + extension;

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                files.push_back(findData.cFileName);
            }
        } while (FindNextFileA(hFind, &findData));

        FindClose(hFind);
    }

    return files;
}

//=============================================================================
// HashUtils Implementation
//=============================================================================

uint32_t HashUtils::FNV1a_32(const std::string& str) {
    constexpr uint32_t FNV_PRIME = 16777619u;
    constexpr uint32_t FNV_OFFSET = 2166136261u;

    uint32_t hash = FNV_OFFSET;
    for (char c : str) {
        hash ^= static_cast<uint32_t>(c);
        hash *= FNV_PRIME;
    }

    return hash;
}

uint64_t HashUtils::FNV1a_64(const std::string& str) {
    constexpr uint64_t FNV_PRIME = 1099511628211ull;
    constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;

    uint64_t hash = FNV_OFFSET;
    for (char c : str) {
        hash ^= static_cast<uint64_t>(c);
        hash *= FNV_PRIME;
    }

    return hash;
}

uint32_t HashUtils::HashPattern(const std::vector<uint8_t>& pattern) {
    constexpr uint32_t FNV_PRIME = 16777619u;
    constexpr uint32_t FNV_OFFSET = 2166136261u;

    uint32_t hash = FNV_OFFSET;
    for (uint8_t byte : pattern) {
        hash ^= byte;
        hash *= FNV_PRIME;
    }

    return hash;
}

uint32_t HashUtils::CRC32(const std::string& data) {
    static uint32_t crcTable[256] = { 0 };
    static bool tableInitialized = false;

    if (!tableInitialized) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t crc = i;
            for (int j = 0; j < 8; j++) {
                crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
            }
            crcTable[i] = crc;
        }
        tableInitialized = true;
    }

    uint32_t crc = 0xFFFFFFFF;
    for (char c : data) {
        crc = (crc >> 8) ^ crcTable[(crc ^ c) & 0xFF];
    }

    return crc ^ 0xFFFFFFFF;
}

//=============================================================================
// SystemUtils Implementation
//=============================================================================

std::string SystemUtils::GetExecutablePath() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    return std::string(path);
}

std::string SystemUtils::GetExecutableDirectory() {
    return FileUtils::GetDirectory(GetExecutablePath());
}

std::string SystemUtils::GetWorkingDirectory() {
    char path[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, path);
    return std::string(path);
}

uint32_t SystemUtils::GetProcessId() {
    return GetCurrentProcessId();
}

uint32_t SystemUtils::GetThreadId() {
    return GetCurrentThreadId();
}

std::string SystemUtils::GetComputerName() {
    char name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(name);
    if (::GetComputerNameA(name, &size)) {
        return std::string(name);
    }
    return "";
}

std::string SystemUtils::GetUsername() {
    char name[256];
    DWORD size = sizeof(name);
    if (GetUserNameA(name, &size)) {
        return std::string(name);
    }
    return "";
}

uint64_t SystemUtils::GetCPUCycles() {
    return __rdtsc();
}

double SystemUtils::GetCPUFrequency() {
    static double frequency = 0.0;
    if (frequency == 0.0) {
        uint64_t start = __rdtsc();
        Sleep(100);
        uint64_t end = __rdtsc();
        frequency = (end - start) / 100000.0;  // Cycles per millisecond
    }
    return frequency;
}

size_t SystemUtils::GetTotalPhysicalMemory() {
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    GlobalMemoryStatusEx(&memStatus);
    return static_cast<size_t>(memStatus.ullTotalPhys);
}

size_t SystemUtils::GetAvailablePhysicalMemory() {
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    GlobalMemoryStatusEx(&memStatus);
    return static_cast<size_t>(memStatus.ullAvailPhys);
}

//=============================================================================
// RandomUtils Implementation
//=============================================================================

static std::mt19937 s_randomEngine;
static bool s_randomInitialized = false;

void RandomUtils::Initialize() {
    if (!s_randomInitialized) {
        s_randomEngine.seed(static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count()));
        s_randomInitialized = true;
    }
}

int RandomUtils::RandomInt(int min, int max) {
    Initialize();
    std::uniform_int_distribution<int> dist(min, max);
    return dist(s_randomEngine);
}

float RandomUtils::RandomFloat(float min, float max) {
    Initialize();
    std::uniform_real_distribution<float> dist(min, max);
    return dist(s_randomEngine);
}

bool RandomUtils::RandomBool() {
    return RandomInt(0, 1) == 1;
}

//=============================================================================
// DebugUtils Implementation
//=============================================================================

std::string DebugUtils::HexDump(uintptr_t address, size_t length) {
    if (!MemoryUtils::IsValidPointer(address)) {
        return "[Invalid address]";
    }

    std::ostringstream oss;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(address);

    for (size_t i = 0; i < length; i += 16) {
        oss << std::hex << std::setfill('0') << std::setw(8) << (address + i) << "  ";

        // Hex bytes
        for (size_t j = 0; j < 16; j++) {
            if (i + j < length) {
                oss << std::setw(2) << static_cast<int>(data[i + j]) << " ";
            } else {
                oss << "   ";
            }

            if (j == 7) oss << " ";
        }

        oss << " |";

        // ASCII representation
        for (size_t j = 0; j < 16 && i + j < length; j++) {
            char c = data[i + j];
            oss << (isprint(c) ? c : '.');
        }

        oss << "|\n";
    }

    return oss.str();
}

void DebugUtils::PrintMemory(uintptr_t address, size_t length) {
    OutputDebugStringA(HexDump(address, length).c_str());
}

std::string DebugUtils::GetStackTrace() {
    // Simplified stack trace - full implementation would use DbgHelp
    return "[Stack trace not implemented]";
}

void DebugUtils::TriggerBreakpoint() {
    __debugbreak();
}

bool DebugUtils::IsDebuggerAttached() {
    return IsDebuggerPresent() != 0;
}

//=============================================================================
// JsonUtils Implementation
//=============================================================================

std::string JsonUtils::EscapeJsonString(const std::string& str) {
    std::ostringstream oss;

    for (char c : str) {
        switch (c) {
        case '"':  oss << "\\\""; break;
        case '\\': oss << "\\\\"; break;
        case '\b': oss << "\\b"; break;
        case '\f': oss << "\\f"; break;
        case '\n': oss << "\\n"; break;
        case '\r': oss << "\\r"; break;
        case '\t': oss << "\\t"; break;
        default:   oss << c; break;
        }
    }

    return oss.str();
}

std::string JsonUtils::BuildJsonObject(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::ostringstream oss;
    oss << "{";

    for (size_t i = 0; i < fields.size(); i++) {
        if (i > 0) oss << ",";
        oss << "\"" << EscapeJsonString(fields[i].first) << "\":\""
            << EscapeJsonString(fields[i].second) << "\"";
    }

    oss << "}";
    return oss.str();
}

std::string JsonUtils::BuildJsonArray(const std::vector<std::string>& elements) {
    std::ostringstream oss;
    oss << "[";

    for (size_t i = 0; i < elements.size(); i++) {
        if (i > 0) oss << ",";
        oss << "\"" << EscapeJsonString(elements[i]) << "\"";
    }

    oss << "]";
    return oss.str();
}

} // namespace Utils
} // namespace ReKenshi
