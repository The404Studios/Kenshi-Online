#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include <cstdint>

namespace ReKenshi {
namespace Memory {

/**
 * Memory scanning utilities for finding patterns in process memory
 */
class MemoryScanner {
public:
    /**
     * Pattern structure for signature scanning
     */
    struct Pattern {
        std::vector<uint8_t> bytes;
        std::vector<bool> mask;  // true = must match, false = wildcard

        Pattern() = default;
        Pattern(const char* signature);  // Parse "48 8B 05 ?? ?? ?? ?? 48 85 C0"
    };

    /**
     * Scan for pattern in module
     */
    static uintptr_t FindPattern(const char* moduleName, const Pattern& pattern);
    static uintptr_t FindPattern(uintptr_t startAddress, size_t size, const Pattern& pattern);

    /**
     * Helper to parse pattern strings
     * Format: "48 8B 05 ?? ?? ?? ?? 48 85 C0"
     * ?? = wildcard byte
     */
    static Pattern ParsePattern(const char* signature);

    /**
     * Read memory safely with exception handling
     */
    template<typename T>
    static bool ReadMemory(uintptr_t address, T& outValue) {
        __try {
            outValue = *reinterpret_cast<T*>(address);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    /**
     * Write memory safely
     */
    template<typename T>
    static bool WriteMemory(uintptr_t address, const T& value) {
        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(T), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }

        __try {
            *reinterpret_cast<T*>(address) = value;
            VirtualProtect(reinterpret_cast<void*>(address), sizeof(T), oldProtect, &oldProtect);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            VirtualProtect(reinterpret_cast<void*>(address), sizeof(T), oldProtect, &oldProtect);
            return false;
        }
    }

    /**
     * Get module base address
     */
    static uintptr_t GetModuleBase(const char* moduleName = nullptr);

    /**
     * Get module size
     */
    static size_t GetModuleSize(const char* moduleName = nullptr);

    /**
     * Resolve relative address (for RIP-relative addressing)
     * Example: E8 ?? ?? ?? ?? -> call instruction
     */
    static uintptr_t ResolveRelativeAddress(uintptr_t address, int instructionSize = 5);

    /**
     * Follow pointer chain
     * Example: [[base + 0x10] + 0x20] + 0x30
     */
    static uintptr_t FollowPointerChain(uintptr_t baseAddress, const std::vector<uintptr_t>& offsets);
};

/**
 * Common Kenshi patterns (from reverse engineering)
 */
namespace KenshiPatterns {
    // OGRE-related patterns
    constexpr const char* OGRE_ROOT = "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 01 FF 90 ?? ?? ?? ??";
    constexpr const char* OGRE_RENDER_WINDOW = "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8B 08 48 8B 01";
    constexpr const char* OGRE_SCENE_MANAGER = "48 8B 0D ?? ?? ?? ?? 48 85 C9 0F 84 ?? ?? ?? ??";

    // Direct3D patterns
    constexpr const char* D3D11_DEVICE = "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 ?? ?? ?? ?? 48 8B F8";
    constexpr const char* D3D11_CONTEXT = "48 8B 15 ?? ?? ?? ?? 48 8B CA E8 ?? ?? ?? ??";
    constexpr const char* DXGI_SWAPCHAIN = "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8B 10";

    // Game state patterns
    constexpr const char* GAME_WORLD = "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? E8 ?? ?? ?? ?? 84 C0";
    constexpr const char* PLAYER_CONTROLLER = "48 8B 1D ?? ?? ?? ?? 48 85 DB 74 ?? 48 8B CB";
    constexpr const char* CHARACTER_LIST = "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8B 48 ??";

    // Function patterns
    constexpr const char* SPAWN_CHARACTER = "40 53 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 48 85 C0";
    constexpr const char* UPDATE_WORLD = "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24";
}

} // namespace Memory
} // namespace ReKenshi
