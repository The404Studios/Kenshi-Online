#pragma once

#include "KenshiStructures.h"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <windows.h>

namespace ReKenshi {
namespace Utils {

/**
 * String utilities
 */
class StringUtils {
public:
    // Convert wide string to UTF-8
    static std::string WideToUtf8(const std::wstring& wstr);

    // Convert UTF-8 to wide string
    static std::wstring Utf8ToWide(const std::string& str);

    // Convert to lowercase
    static std::string ToLower(const std::string& str);

    // Convert to uppercase
    static std::string ToUpper(const std::string& str);

    // Trim whitespace
    static std::string Trim(const std::string& str);
    static std::string TrimLeft(const std::string& str);
    static std::string TrimRight(const std::string& str);

    // Split string
    static std::vector<std::string> Split(const std::string& str, char delimiter);

    // Join strings
    static std::string Join(const std::vector<std::string>& strings, const std::string& delimiter);

    // Replace all occurrences
    static std::string ReplaceAll(const std::string& str, const std::string& from, const std::string& to);

    // Check if string starts/ends with
    static bool StartsWith(const std::string& str, const std::string& prefix);
    static bool EndsWith(const std::string& str, const std::string& suffix);

    // Format helpers
    static std::string FormatBytes(size_t bytes);
    static std::string FormatDuration(double milliseconds);
};

/**
 * Math utilities
 */
class MathUtils {
public:
    // Distance calculations
    static float Distance2D(const Kenshi::Vector3& a, const Kenshi::Vector3& b);
    static float Distance3D(const Kenshi::Vector3& a, const Kenshi::Vector3& b);

    // Vector operations
    static Kenshi::Vector3 Add(const Kenshi::Vector3& a, const Kenshi::Vector3& b);
    static Kenshi::Vector3 Subtract(const Kenshi::Vector3& a, const Kenshi::Vector3& b);
    static Kenshi::Vector3 Multiply(const Kenshi::Vector3& v, float scalar);
    static float DotProduct(const Kenshi::Vector3& a, const Kenshi::Vector3& b);
    static Kenshi::Vector3 CrossProduct(const Kenshi::Vector3& a, const Kenshi::Vector3& b);
    static float Magnitude(const Kenshi::Vector3& v);
    static Kenshi::Vector3 Normalize(const Kenshi::Vector3& v);

    // Interpolation
    static float Lerp(float a, float b, float t);
    static Kenshi::Vector3 LerpVector(const Kenshi::Vector3& a, const Kenshi::Vector3& b, float t);

    // Angle calculations
    static float AngleBetween(const Kenshi::Vector3& a, const Kenshi::Vector3& b);
    static Kenshi::Vector3 RotateAroundY(const Kenshi::Vector3& v, float radians);

    // Clamping
    static float Clamp(float value, float min, float max);
    static int Clamp(int value, int min, int max);

    // Rounding
    static float Round(float value, int decimals);
};

/**
 * Time utilities
 */
class TimeUtils {
public:
    // Get current timestamp in milliseconds
    static uint64_t GetCurrentTimestampMs();

    // Get current timestamp in microseconds
    static uint64_t GetCurrentTimestampUs();

    // Sleep
    static void SleepMs(uint32_t milliseconds);

    // Format timestamp
    static std::string FormatTimestamp(uint64_t timestampMs);

    // Elapsed time
    static double GetElapsedMs(uint64_t startTimestamp);
};

/**
 * Memory utilities
 */
class MemoryUtils {
public:
    // Safe string copy from memory
    static std::string ReadString(uintptr_t address, size_t maxLength = 256);

    // Safe memory comparison
    static bool CompareMemory(uintptr_t address, const std::vector<uint8_t>& pattern);

    // Memory region info
    static bool IsValidMemoryRegion(uintptr_t address);
    static size_t GetMemoryRegionSize(uintptr_t address);

    // Module helpers
    static uintptr_t GetModuleBase(const char* moduleName);
    static size_t GetModuleSize(const char* moduleName);

    // Pointer validation
    static bool IsValidPointer(uintptr_t address);
    static bool IsReadableMemory(uintptr_t address, size_t size);
};

/**
 * File utilities
 */
class FileUtils {
public:
    // Check if file exists
    static bool FileExists(const std::string& path);

    // Get file size
    static size_t GetFileSize(const std::string& path);

    // Read entire file
    static std::string ReadFile(const std::string& path);

    // Write entire file
    static bool WriteFile(const std::string& path, const std::string& content);

    // Get directory from path
    static std::string GetDirectory(const std::string& path);

    // Get filename from path
    static std::string GetFilename(const std::string& path);

    // Get file extension
    static std::string GetExtension(const std::string& path);

    // Create directory
    static bool CreateDirectory(const std::string& path);

    // List files in directory
    static std::vector<std::string> ListFiles(const std::string& directory, const std::string& extension = "");
};

/**
 * Hash utilities
 */
class HashUtils {
public:
    // FNV-1a hash
    static uint32_t FNV1a_32(const std::string& str);
    static uint64_t FNV1a_64(const std::string& str);

    // Simple hash for patterns
    static uint32_t HashPattern(const std::vector<uint8_t>& pattern);

    // CRC32
    static uint32_t CRC32(const std::string& data);
};

/**
 * System utilities
 */
class SystemUtils {
public:
    // Get executable path
    static std::string GetExecutablePath();

    // Get executable directory
    static std::string GetExecutableDirectory();

    // Get working directory
    static std::string GetWorkingDirectory();

    // System info
    static uint32_t GetProcessId();
    static uint32_t GetThreadId();
    static std::string GetComputerName();
    static std::string GetUsername();

    // Performance counters
    static uint64_t GetCPUCycles();
    static double GetCPUFrequency();

    // Memory info
    static size_t GetTotalPhysicalMemory();
    static size_t GetAvailablePhysicalMemory();
};

/**
 * Random utilities
 */
class RandomUtils {
public:
    // Initialize random seed
    static void Initialize();

    // Random integers
    static int RandomInt(int min, int max);

    // Random floats
    static float RandomFloat(float min = 0.0f, float max = 1.0f);

    // Random boolean
    static bool RandomBool();

    // Random element from vector
    template<typename T>
    static T RandomElement(const std::vector<T>& vec) {
        if (vec.empty()) {
            throw std::runtime_error("Cannot get random element from empty vector");
        }
        return vec[RandomInt(0, static_cast<int>(vec.size()) - 1)];
    }
};

/**
 * Debugging utilities
 */
class DebugUtils {
public:
    // Hexdump memory
    static std::string HexDump(uintptr_t address, size_t length);

    // Print memory
    static void PrintMemory(uintptr_t address, size_t length);

    // Stack trace (simplified)
    static std::string GetStackTrace();

    // Breakpoint
    static void TriggerBreakpoint();

    // Check if debugger is attached
    static bool IsDebuggerAttached();
};

/**
 * JSON utilities (simple)
 */
class JsonUtils {
public:
    // Escape JSON string
    static std::string EscapeJsonString(const std::string& str);

    // Build simple JSON object
    static std::string BuildJsonObject(const std::vector<std::pair<std::string, std::string>>& fields);

    // Build JSON array
    static std::string BuildJsonArray(const std::vector<std::string>& elements);
};

} // namespace Utils
} // namespace ReKenshi
