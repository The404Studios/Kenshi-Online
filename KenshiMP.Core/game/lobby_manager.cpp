#include "lobby_manager.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <cstring>
#include <algorithm>

namespace kmp {

void LobbyManager::OnFactionAssigned(const std::string& factionString, int playerSlot) {
    m_factionString = factionString;
    m_playerSlot = playerSlot;
    m_hasAssignment = true;
    spdlog::info("LobbyManager: Assigned faction '{}' slot {}", factionString, playerSlot);
}

// SEH helpers — MSVC forbids __try in functions with C++ objects (spdlog, std::string).
// These thin wrappers contain ONLY POD types and the SEH-protected code.
static bool SEH_CheckFactionCandidate(uintptr_t addr, char* outBuf) {
    __try {
        memcpy(outBuf, reinterpret_cast<void*>(addr), 17);
        outBuf[17] = '\0';
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SEH_MemcmpAt(uintptr_t addr, const char* searchStr, size_t len) {
    __try {
        return memcmp(reinterpret_cast<void*>(addr), searchStr, len) == 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t LobbyManager::FindFactionStringAddress() {
    uintptr_t moduleBase = Memory::GetModuleBase();

    // Strategy 1: Try known offsets (Steam v1.0.68, GOG v1.0.68)
    uintptr_t candidates[] = {
        moduleBase + 0x16C4258,  // Steam v1.0.68
        moduleBase + 0x16C2F68,  // GOG v1.0.68
    };

    for (auto addr : candidates) {
        char buf[20] = {};
        if (SEH_CheckFactionCandidate(addr, buf)) {
            bool hasDash = false, hasDot = false;
            for (int i = 0; i < 17; i++) {
                if (buf[i] == '-') hasDash = true;
                if (buf[i] == '.') hasDot = true;
            }
            if (hasDash && hasDot) {
                spdlog::info("LobbyManager: Found faction string at 0x{:X}: '{}'",
                             addr, std::string(buf, 17));
                return addr;
            }
        }
    }

    // Strategy 2: Search .rdata for "204-gamedata.base" (default faction string)
    const char* searchStr = "204-gamedata.base";
    size_t searchLen = 17;

    auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(moduleBase);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(moduleBase + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return 0;
    auto* sections = IMAGE_FIRST_SECTION(ntHeaders);

    for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
        if (strncmp(reinterpret_cast<const char*>(sections[i].Name), ".rdata", 6) == 0) {
            uintptr_t start = moduleBase + sections[i].VirtualAddress;
            uintptr_t end = start + sections[i].Misc.VirtualSize;

            for (uintptr_t addr = start; addr < end - searchLen; addr++) {
                if (SEH_MemcmpAt(addr, searchStr, searchLen)) {
                    spdlog::info("LobbyManager: Found faction string via search at 0x{:X}", addr);
                    return addr;
                }
            }
        }
    }

    spdlog::error("LobbyManager: Could not find faction string in memory");
    return 0;
}

bool LobbyManager::ApplyFactionPatch() {
    if (!m_hasAssignment || m_factionString.empty()) {
        spdlog::warn("LobbyManager: No faction assigned, cannot patch");
        return false;
    }

    uintptr_t addr = FindFactionStringAddress();
    if (addr == 0) return false;

    // The original string "204-gamedata.base" is 17 chars + null = 18 bytes.
    // Mod faction strings like "10-kenshi-online.mod" are up to 24 chars.
    // We write the full string + null, overwriting a few bytes past the original.
    // This is safe because .rdata strings are packed with other string literals
    // and the adjacent bytes are not critical (research mod also overwrites 1+ bytes).
    static constexpr size_t MAX_FACTION_WRITE = 24;
    size_t writeLen = std::min(m_factionString.size(), MAX_FACTION_WRITE);
    size_t protectLen = writeLen + 1; // +1 for null terminator

    DWORD oldProtect;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), protectLen,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) {
        spdlog::error("LobbyManager: VirtualProtect failed at 0x{:X}", addr);
        return false;
    }

    // Write faction string with null terminator
    char factionBuf[MAX_FACTION_WRITE + 1] = {};
    memcpy(factionBuf, m_factionString.c_str(), writeLen);
    factionBuf[writeLen] = '\0';
    memcpy(reinterpret_cast<void*>(addr), factionBuf, writeLen + 1);

    VirtualProtect(reinterpret_cast<void*>(addr), protectLen, oldProtect, &oldProtect);

    spdlog::info("LobbyManager: Patched faction string at 0x{:X} to '{}' ({} bytes)",
                 addr, m_factionString, writeLen);
    return true;
}

} // namespace kmp
