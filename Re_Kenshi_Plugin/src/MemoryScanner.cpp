#include "MemoryScanner.h"
#include <sstream>
#include <iomanip>
#include <psapi.h>

namespace ReKenshi {
namespace Memory {

MemoryScanner::Pattern::Pattern(const char* signature) {
    *this = MemoryScanner::ParsePattern(signature);
}

MemoryScanner::Pattern MemoryScanner::ParsePattern(const char* signature) {
    Pattern pattern;
    std::istringstream iss(signature);
    std::string byte;

    while (iss >> byte) {
        if (byte == "??" || byte == "?") {
            pattern.bytes.push_back(0x00);
            pattern.mask.push_back(false);  // Wildcard
        } else {
            pattern.bytes.push_back(static_cast<uint8_t>(std::stoul(byte, nullptr, 16)));
            pattern.mask.push_back(true);   // Must match
        }
    }

    return pattern;
}

uintptr_t MemoryScanner::FindPattern(const char* moduleName, const Pattern& pattern) {
    uintptr_t base = GetModuleBase(moduleName);
    if (!base) {
        return 0;
    }

    size_t size = GetModuleSize(moduleName);
    return FindPattern(base, size, pattern);
}

uintptr_t MemoryScanner::FindPattern(uintptr_t startAddress, size_t size, const Pattern& pattern) {
    if (pattern.bytes.empty() || !startAddress) {
        return 0;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(startAddress);
    const size_t patternSize = pattern.bytes.size();

    for (size_t i = 0; i <= size - patternSize; ++i) {
        bool found = true;

        for (size_t j = 0; j < patternSize; ++j) {
            if (pattern.mask[j] && data[i + j] != pattern.bytes[j]) {
                found = false;
                break;
            }
        }

        if (found) {
            return startAddress + i;
        }
    }

    return 0;
}

uintptr_t MemoryScanner::GetModuleBase(const char* moduleName) {
    HMODULE hModule = nullptr;

    if (moduleName) {
        hModule = GetModuleHandleA(moduleName);
    } else {
        hModule = GetModuleHandleA(nullptr);  // Main executable
    }

    return reinterpret_cast<uintptr_t>(hModule);
}

size_t MemoryScanner::GetModuleSize(const char* moduleName) {
    HMODULE hModule = nullptr;

    if (moduleName) {
        hModule = GetModuleHandleA(moduleName);
    } else {
        hModule = GetModuleHandleA(nullptr);
    }

    if (!hModule) {
        return 0;
    }

    MODULEINFO moduleInfo;
    if (GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo))) {
        return moduleInfo.SizeOfImage;
    }

    return 0;
}

uintptr_t MemoryScanner::ResolveRelativeAddress(uintptr_t address, int instructionSize) {
    if (!address) {
        return 0;
    }

    int32_t relativeOffset = 0;
    if (!ReadMemory(address + 1, relativeOffset)) {
        return 0;
    }

    return address + instructionSize + relativeOffset;
}

uintptr_t MemoryScanner::FollowPointerChain(uintptr_t baseAddress, const std::vector<uintptr_t>& offsets) {
    uintptr_t address = baseAddress;

    for (size_t i = 0; i < offsets.size(); ++i) {
        if (!ReadMemory(address, address)) {
            return 0;
        }

        if (address == 0) {
            return 0;
        }

        address += offsets[i];
    }

    return address;
}

} // namespace Memory
} // namespace ReKenshi
