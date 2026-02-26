#pragma once
#include <cstdint>
#include <cstring>
#include <Windows.h>

namespace kmp {

class Memory {
public:
    // Safe read from game memory (handles access violations)
    template<typename T>
    static bool Read(uintptr_t address, T& out) {
        __try {
            out = *reinterpret_cast<T*>(address);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Safe write to game memory
    template<typename T>
    static bool Write(uintptr_t address, const T& value) {
        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(T),
                           PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }
        __try {
            *reinterpret_cast<T*>(address) = value;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            VirtualProtect(reinterpret_cast<void*>(address), sizeof(T), oldProtect, &oldProtect);
            return false;
        }
        VirtualProtect(reinterpret_cast<void*>(address), sizeof(T), oldProtect, &oldProtect);
        return true;
    }

    // Follow a pointer chain: base + offset1 -> +offset2 -> ... -> final value
    // Offsets array terminated by -1
    static uintptr_t FollowChain(uintptr_t base, const int* offsets) {
        uintptr_t addr = base;
        for (int i = 0; offsets[i] != -1; i++) {
            uintptr_t next;
            if (!Read(addr + offsets[i], next)) return 0;
            addr = next;
        }
        return addr;
    }

    // Read a float through a pointer chain
    static bool ReadChainFloat(uintptr_t base, const int* offsets, float& out) {
        uintptr_t addr = base;
        // Follow all offsets except the last
        int i = 0;
        while (offsets[i] != -1 && offsets[i + 1] != -1) {
            uintptr_t next;
            if (!Read(addr + offsets[i], next)) return false;
            addr = next;
            i++;
        }
        // Last offset: read the actual value
        if (offsets[i] != -1) {
            return Read(addr + offsets[i], out);
        }
        return Read(addr, out);
    }

    // Read a Vec3 (3 consecutive floats)
    static bool ReadVec3(uintptr_t address, float& x, float& y, float& z) {
        struct { float x, y, z; } v;
        if (!Read(address, v)) return false;
        x = v.x; y = v.y; z = v.z;
        return true;
    }

    // Patch bytes in memory (with protection change)
    static bool Patch(uintptr_t address, const uint8_t* bytes, size_t len) {
        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void*>(address), len,
                           PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }
        std::memcpy(reinterpret_cast<void*>(address), bytes, len);
        VirtualProtect(reinterpret_cast<void*>(address), len, oldProtect, &oldProtect);
        return true;
    }

    // NOP out instructions
    static bool Nop(uintptr_t address, size_t len) {
        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void*>(address), len,
                           PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }
        std::memset(reinterpret_cast<void*>(address), 0x90, len);
        VirtualProtect(reinterpret_cast<void*>(address), len, oldProtect, &oldProtect);
        return true;
    }

    // Get module base address
    static uintptr_t GetModuleBase(const char* moduleName = nullptr) {
        HMODULE hMod = moduleName ? GetModuleHandleA(moduleName) : GetModuleHandleA(nullptr);
        return reinterpret_cast<uintptr_t>(hMod);
    }
};

} // namespace kmp
