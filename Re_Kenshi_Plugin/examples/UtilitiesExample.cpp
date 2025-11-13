/**
 * Utilities Example for Re_Kenshi Plugin
 *
 * This file demonstrates all utility helper classes.
 */

#include "../include/Utilities.h"
#include "../include/KenshiStructures.h"
#include <iostream>

using namespace ReKenshi::Utils;
using namespace ReKenshi::Kenshi;

//=============================================================================
// Example 1: String Utilities
//=============================================================================

void Example_StringUtils() {
    std::cout << "=== String Utilities ===\n\n";

    // Case conversion
    std::string text = "Hello World";
    std::cout << "Original: " << text << "\n";
    std::cout << "Lowercase: " << StringUtils::ToLower(text) << "\n";
    std::cout << "Uppercase: " << StringUtils::ToUpper(text) << "\n\n";

    // Trimming
    std::string padded = "   trimmed   ";
    std::cout << "Original: '" << padded << "'\n";
    std::cout << "Trimmed: '" << StringUtils::Trim(padded) << "'\n\n";

    // Splitting and joining
    std::string csv = "apple,banana,cherry,date";
    auto parts = StringUtils::Split(csv, ',');
    std::cout << "Split '" << csv << "':\n";
    for (const auto& part : parts) {
        std::cout << "  - " << part << "\n";
    }
    std::string joined = StringUtils::Join(parts, " | ");
    std::cout << "Joined: " << joined << "\n\n";

    // Replace
    std::string original = "I like cats and cats like me";
    std::string replaced = StringUtils::ReplaceAll(original, "cats", "dogs");
    std::cout << "Original: " << original << "\n";
    std::cout << "Replaced: " << replaced << "\n\n";

    // Starts/ends with
    std::string filename = "config.json";
    std::cout << "File: " << filename << "\n";
    std::cout << "Starts with 'config': " << StringUtils::StartsWith(filename, "config") << "\n";
    std::cout << "Ends with '.json': " << StringUtils::EndsWith(filename, ".json") << "\n\n";

    // Formatting
    size_t bytes = 1536000;
    std::cout << "Bytes: " << bytes << " = " << StringUtils::FormatBytes(bytes) << "\n";

    double ms = 2500.5;
    std::cout << "Duration: " << ms << " ms = " << StringUtils::FormatDuration(ms) << "\n\n";
}

//=============================================================================
// Example 2: Math Utilities
//=============================================================================

void Example_MathUtils() {
    std::cout << "=== Math Utilities ===\n\n";

    Vector3 a = { 0.0f, 0.0f, 0.0f };
    Vector3 b = { 10.0f, 5.0f, 3.0f };

    // Distance
    float dist2D = MathUtils::Distance2D(a, b);
    float dist3D = MathUtils::Distance3D(a, b);
    std::cout << "Distance 2D: " << dist2D << "\n";
    std::cout << "Distance 3D: " << dist3D << "\n\n";

    // Vector operations
    Vector3 sum = MathUtils::Add(a, b);
    Vector3 diff = MathUtils::Subtract(b, a);
    Vector3 scaled = MathUtils::Multiply(b, 2.0f);

    std::cout << "a + b = (" << sum.x << ", " << sum.y << ", " << sum.z << ")\n";
    std::cout << "b - a = (" << diff.x << ", " << diff.y << ", " << diff.z << ")\n";
    std::cout << "b * 2 = (" << scaled.x << ", " << scaled.y << ", " << scaled.z << ")\n\n";

    // Dot and cross product
    Vector3 v1 = { 1.0f, 0.0f, 0.0f };
    Vector3 v2 = { 0.0f, 1.0f, 0.0f };
    float dot = MathUtils::DotProduct(v1, v2);
    Vector3 cross = MathUtils::CrossProduct(v1, v2);
    std::cout << "Dot product: " << dot << "\n";
    std::cout << "Cross product: (" << cross.x << ", " << cross.y << ", " << cross.z << ")\n\n";

    // Magnitude and normalization
    Vector3 vec = { 3.0f, 4.0f, 0.0f };
    float mag = MathUtils::Magnitude(vec);
    Vector3 normalized = MathUtils::Normalize(vec);
    std::cout << "Magnitude of (3, 4, 0): " << mag << "\n";
    std::cout << "Normalized: (" << normalized.x << ", " << normalized.y << ", " << normalized.z << ")\n\n";

    // Interpolation
    float lerp = MathUtils::Lerp(0.0f, 100.0f, 0.5f);
    Vector3 lerpVec = MathUtils::LerpVector(a, b, 0.5f);
    std::cout << "Lerp(0, 100, 0.5) = " << lerp << "\n";
    std::cout << "Lerp vector = (" << lerpVec.x << ", " << lerpVec.y << ", " << lerpVec.z << ")\n\n";

    // Clamping
    float clamped = MathUtils::Clamp(150.0f, 0.0f, 100.0f);
    std::cout << "Clamp(150, 0, 100) = " << clamped << "\n\n";
}

//=============================================================================
// Example 3: Time Utilities
//=============================================================================

void Example_TimeUtils() {
    std::cout << "=== Time Utilities ===\n\n";

    // Get current timestamp
    uint64_t timestampMs = TimeUtils::GetCurrentTimestampMs();
    uint64_t timestampUs = TimeUtils::GetCurrentTimestampUs();

    std::cout << "Current timestamp (ms): " << timestampMs << "\n";
    std::cout << "Current timestamp (μs): " << timestampUs << "\n\n";

    // Format timestamp
    std::string formatted = TimeUtils::FormatTimestamp(timestampMs);
    std::cout << "Formatted: " << formatted << "\n\n";

    // Measure elapsed time
    uint64_t start = TimeUtils::GetCurrentTimestampMs();
    TimeUtils::SleepMs(100);
    double elapsed = TimeUtils::GetElapsedMs(start);

    std::cout << "Elapsed time: " << elapsed << " ms\n\n";
}

//=============================================================================
// Example 4: Memory Utilities
//=============================================================================

void Example_MemoryUtils() {
    std::cout << "=== Memory Utilities ===\n\n";

    // Get module information
    uintptr_t moduleBase = MemoryUtils::GetModuleBase(nullptr);  // Current module
    size_t moduleSize = MemoryUtils::GetModuleSize(nullptr);

    std::cout << "Module base: 0x" << std::hex << moduleBase << std::dec << "\n";
    std::cout << "Module size: " << StringUtils::FormatBytes(moduleSize) << "\n\n";

    // Test string (on stack)
    const char* testStr = "Hello from memory!";
    uintptr_t strAddr = reinterpret_cast<uintptr_t>(testStr);

    if (MemoryUtils::IsValidPointer(strAddr)) {
        std::string readStr = MemoryUtils::ReadString(strAddr);
        std::cout << "Read string: " << readStr << "\n\n";
    }

    // Memory pattern comparison
    std::vector<uint8_t> pattern = { 0x48, 0x8B, 0x05 };
    std::cout << "Pattern comparison example (check specific memory)\n\n";
}

//=============================================================================
// Example 5: File Utilities
//=============================================================================

void Example_FileUtils() {
    std::cout << "=== File Utilities ===\n\n";

    // Path operations
    std::string fullPath = "C:\\Games\\Kenshi\\data\\config.json";
    std::string directory = FileUtils::GetDirectory(fullPath);
    std::string filename = FileUtils::GetFilename(fullPath);
    std::string extension = FileUtils::GetExtension(fullPath);

    std::cout << "Full path: " << fullPath << "\n";
    std::cout << "Directory: " << directory << "\n";
    std::cout << "Filename: " << filename << "\n";
    std::cout << "Extension: " << extension << "\n\n";

    // File operations
    std::string testFile = "test_file.txt";
    std::string content = "This is test content\nLine 2\nLine 3";

    if (FileUtils::WriteFile(testFile, content)) {
        std::cout << "File written successfully\n";

        if (FileUtils::FileExists(testFile)) {
            size_t fileSize = FileUtils::GetFileSize(testFile);
            std::cout << "File size: " << fileSize << " bytes\n";

            std::string readContent = FileUtils::ReadFile(testFile);
            std::cout << "File content:\n" << readContent << "\n\n";
        }
    }
}

//=============================================================================
// Example 6: Hash Utilities
//=============================================================================

void Example_HashUtils() {
    std::cout << "=== Hash Utilities ===\n\n";

    std::string text = "Hello, World!";

    // FNV-1a hashing
    uint32_t hash32 = HashUtils::FNV1a_32(text);
    uint64_t hash64 = HashUtils::FNV1a_64(text);

    std::cout << "Text: " << text << "\n";
    std::cout << "FNV-1a 32-bit: 0x" << std::hex << hash32 << std::dec << "\n";
    std::cout << "FNV-1a 64-bit: 0x" << std::hex << hash64 << std::dec << "\n\n";

    // CRC32
    uint32_t crc = HashUtils::CRC32(text);
    std::cout << "CRC32: 0x" << std::hex << crc << std::dec << "\n\n";

    // Pattern hashing
    std::vector<uint8_t> pattern = { 0x48, 0x8B, 0x05, 0xAA, 0xBB, 0xCC };
    uint32_t patternHash = HashUtils::HashPattern(pattern);
    std::cout << "Pattern hash: 0x" << std::hex << patternHash << std::dec << "\n\n";
}

//=============================================================================
// Example 7: System Utilities
//=============================================================================

void Example_SystemUtils() {
    std::cout << "=== System Utilities ===\n\n";

    // Paths
    std::string exePath = SystemUtils::GetExecutablePath();
    std::string exeDir = SystemUtils::GetExecutableDirectory();
    std::string workDir = SystemUtils::GetWorkingDirectory();

    std::cout << "Executable: " << exePath << "\n";
    std::cout << "Exe directory: " << exeDir << "\n";
    std::cout << "Working directory: " << workDir << "\n\n";

    // Process info
    uint32_t pid = SystemUtils::GetProcessId();
    uint32_t tid = SystemUtils::GetThreadId();
    std::string computerName = SystemUtils::GetComputerName();
    std::string username = SystemUtils::GetUsername();

    std::cout << "Process ID: " << pid << "\n";
    std::cout << "Thread ID: " << tid << "\n";
    std::cout << "Computer: " << computerName << "\n";
    std::cout << "Username: " << username << "\n\n";

    // Memory info
    size_t totalMem = SystemUtils::GetTotalPhysicalMemory();
    size_t availMem = SystemUtils::GetAvailablePhysicalMemory();

    std::cout << "Total memory: " << StringUtils::FormatBytes(totalMem) << "\n";
    std::cout << "Available memory: " << StringUtils::FormatBytes(availMem) << "\n\n";
}

//=============================================================================
// Example 8: Random Utilities
//=============================================================================

void Example_RandomUtils() {
    std::cout << "=== Random Utilities ===\n\n";

    RandomUtils::Initialize();

    // Random integers
    std::cout << "Random integers (1-10):\n";
    for (int i = 0; i < 5; i++) {
        std::cout << "  " << RandomUtils::RandomInt(1, 10) << "\n";
    }
    std::cout << "\n";

    // Random floats
    std::cout << "Random floats (0.0-1.0):\n";
    for (int i = 0; i < 5; i++) {
        std::cout << "  " << RandomUtils::RandomFloat() << "\n";
    }
    std::cout << "\n";

    // Random booleans
    std::cout << "Random booleans:\n";
    for (int i = 0; i < 5; i++) {
        std::cout << "  " << (RandomUtils::RandomBool() ? "true" : "false") << "\n";
    }
    std::cout << "\n";

    // Random element
    std::vector<std::string> names = { "Alice", "Bob", "Charlie", "Diana", "Eve" };
    std::string randomName = RandomUtils::RandomElement(names);
    std::cout << "Random name: " << randomName << "\n\n";
}

//=============================================================================
// Example 9: Debug Utilities
//=============================================================================

void Example_DebugUtils() {
    std::cout << "=== Debug Utilities ===\n\n";

    // Debugger detection
    bool debuggerAttached = DebugUtils::IsDebuggerAttached();
    std::cout << "Debugger attached: " << (debuggerAttached ? "Yes" : "No") << "\n\n";

    // Hex dump
    const char* data = "Test data for hex dump!";
    uintptr_t addr = reinterpret_cast<uintptr_t>(data);
    std::string hexDump = DebugUtils::HexDump(addr, 24);

    std::cout << "Hex dump:\n" << hexDump << "\n";
}

//=============================================================================
// Example 10: JSON Utilities
//=============================================================================

void Example_JsonUtils() {
    std::cout << "=== JSON Utilities ===\n\n";

    // Escape special characters
    std::string text = "Hello \"World\"\nNew line\tTab";
    std::string escaped = JsonUtils::EscapeJsonString(text);
    std::cout << "Original: " << text << "\n";
    std::cout << "Escaped: " << escaped << "\n\n";

    // Build JSON object
    std::vector<std::pair<std::string, std::string>> fields = {
        {"name", "John Doe"},
        {"age", "30"},
        {"city", "New York"}
    };
    std::string jsonObj = JsonUtils::BuildJsonObject(fields);
    std::cout << "JSON Object: " << jsonObj << "\n\n";

    // Build JSON array
    std::vector<std::string> items = { "apple", "banana", "cherry" };
    std::string jsonArray = JsonUtils::BuildJsonArray(items);
    std::cout << "JSON Array: " << jsonArray << "\n\n";
}

//=============================================================================
// Example 11: Practical Use Case - Game Character Info
//=============================================================================

void Example_CharacterInfo() {
    std::cout << "=== Character Info Use Case ===\n\n";

    // Simulated character data
    Vector3 position = { 1234.5f, 50.2f, 987.3f };
    Vector3 targetPos = { 1500.0f, 50.2f, 1200.0f };

    std::string characterName = "Wanderer";
    float health = 75.5f;
    float maxHealth = 100.0f;

    // Calculate distance to target
    float distance = MathUtils::Distance3D(position, targetPos);

    // Format report
    std::cout << "=== Character Report ===\n";
    std::cout << "Name: " << characterName << "\n";
    std::cout << "Health: " << MathUtils::Round(health, 1) << " / " << maxHealth;
    std::cout << " (" << MathUtils::Round((health / maxHealth) * 100.0f, 0) << "%)\n";
    std::cout << "Position: (" << position.x << ", " << position.y << ", " << position.z << ")\n";
    std::cout << "Distance to target: " << MathUtils::Round(distance, 1) << " units\n";

    // Generate unique ID
    std::string charId = characterName + "_" + std::to_string(HashUtils::FNV1a_32(characterName));
    std::cout << "Character ID: " << charId << "\n\n";
}

//=============================================================================
// Main Example Runner
//=============================================================================

int main() {
    std::cout << "========== Re_Kenshi Utilities Examples ==========\n\n";

    Example_StringUtils();
    Example_MathUtils();
    Example_TimeUtils();
    Example_MemoryUtils();
    Example_FileUtils();
    Example_HashUtils();
    Example_SystemUtils();
    Example_RandomUtils();
    Example_DebugUtils();
    Example_JsonUtils();
    Example_CharacterInfo();

    std::cout << "========================================\n";

    return 0;
}
