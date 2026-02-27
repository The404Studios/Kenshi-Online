#include "kmp/patterns.h"
#include "kmp/scanner.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <Windows.h>

namespace kmp {

// ── Runtime String Scanner ──
// Fallback for when static patterns are nullptr or fail to match.
// Scans the loaded kenshi_x64.exe in memory for known strings,
// follows xrefs to find function addresses (same logic as re_scanner.py).
class RuntimeStringScanner {
public:
    RuntimeStringScanner(uintptr_t moduleBase, size_t moduleSize)
        : m_base(moduleBase), m_size(moduleSize) {
        FindSections();
    }

    // Find a function that references the given string.
    // Returns the function start address, or 0 on failure.
    uintptr_t FindFunctionByString(const char* searchStr, int searchLen) const {
        if (!m_textBase || !m_rdataBase) return 0;

        // Step 1: Find the string in .rdata (or any readable section)
        uintptr_t strAddr = FindStringInMemory(searchStr, searchLen);
        if (!strAddr) return 0;

        // Step 2: Find code that references this string via RIP-relative LEA
        uintptr_t xref = FindStringXref(strAddr);
        if (!xref) return 0;

        // Step 3: Walk backwards to find function prologue
        uintptr_t funcStart = FindFunctionStart(xref);
        return funcStart;
    }

    // Find a global .data pointer that is loaded near code referencing a string.
    // Scans the function containing the string xref for MOV reg, [RIP+disp32]
    // instructions that point into the .data section. Returns the address of
    // the global (not its value).
    uintptr_t FindGlobalNearString(const char* searchStr, int searchLen, int nth = 0) const {
        if (!m_textBase || !m_rdataBase || !m_dataBase) return 0;

        uintptr_t strAddr = FindStringInMemory(searchStr, searchLen);
        if (!strAddr) return 0;

        uintptr_t xref = FindStringXref(strAddr);
        if (!xref) return 0;

        uintptr_t funcStart = FindFunctionStart(xref);
        if (!funcStart) return 0;

        // Scan a window around the string xref for MOV reg, [RIP+disp32]
        // These load global pointers from .data section
        uintptr_t scanStart = (funcStart > xref - 512) ? funcStart : xref - 512;
        uintptr_t scanEnd = xref + 512;
        if (scanEnd > m_textBase + m_textSize) scanEnd = m_textBase + m_textSize;

        int found = 0;
        return ScanForGlobalLoad(scanStart, scanEnd, nth);
    }

    // Getters for section info
    uintptr_t GetDataBase() const { return m_dataBase; }
    size_t GetDataSize() const { return m_dataSize; }

private:
    uintptr_t m_base = 0;
    size_t    m_size = 0;
    uintptr_t m_textBase = 0;
    size_t    m_textSize = 0;
    uintptr_t m_rdataBase = 0;
    size_t    m_rdataSize = 0;
    uintptr_t m_dataBase = 0;
    size_t    m_dataSize = 0;

    void FindSections() {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(m_base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;

        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(m_base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return;

        auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
            char name[9] = {};
            std::memcpy(name, section->Name, 8);
            if (std::strcmp(name, ".text") == 0) {
                m_textBase = m_base + section->VirtualAddress;
                m_textSize = section->Misc.VirtualSize;
            } else if (std::strcmp(name, ".rdata") == 0) {
                m_rdataBase = m_base + section->VirtualAddress;
                m_rdataSize = section->Misc.VirtualSize;
            } else if (std::strcmp(name, ".data") == 0) {
                m_dataBase = m_base + section->VirtualAddress;
                m_dataSize = section->Misc.VirtualSize;
            }
        }
    }

    uintptr_t FindStringInMemory(const char* searchStr, int len) const {
        // Search .rdata first, then full module
        uintptr_t sections[] = { m_rdataBase, m_base };
        size_t sizes[] = { m_rdataSize, m_size };

        for (int s = 0; s < 2; s++) {
            if (!sections[s] || !sizes[s]) continue;

            __try {
                auto* start = reinterpret_cast<const uint8_t*>(sections[s]);
                auto* end = start + sizes[s] - len;
                for (auto* p = start; p < end; p++) {
                    if (std::memcmp(p, searchStr, len) == 0) {
                        return reinterpret_cast<uintptr_t>(p);
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
        }
        return 0;
    }

    uintptr_t FindStringXref(uintptr_t stringAddr) const {
        if (!m_textBase || !m_textSize) return 0;

        __try {
            auto* text = reinterpret_cast<const uint8_t*>(m_textBase);
            size_t textLen = m_textSize;

            for (size_t i = 0; i + 7 < textLen; i++) {
                // REX.W LEA reg, [RIP+disp32]: 48 8D xx (mod=0, rm=5)
                // REX.WR LEA: 4C 8D xx
                if ((text[i] == 0x48 || text[i] == 0x4C) && text[i + 1] == 0x8D) {
                    uint8_t modrm = text[i + 2];
                    uint8_t mod = (modrm >> 6) & 3;
                    uint8_t rm = modrm & 7;
                    if (mod == 0 && rm == 5) {
                        int32_t disp;
                        std::memcpy(&disp, &text[i + 3], 4);
                        uintptr_t instrAddr = m_textBase + i;
                        uintptr_t target = instrAddr + 7 + disp;
                        if (target == stringAddr) {
                            return instrAddr;
                        }
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
        return 0;
    }

    uintptr_t FindFunctionStart(uintptr_t codeAddr) const {
        // Walk backwards looking for function prologue patterns
        __try {
            for (uintptr_t addr = codeAddr - 1; addr > codeAddr - 2048 && addr > m_textBase; addr--) {
                uint8_t b = *reinterpret_cast<const uint8_t*>(addr);

                // Look for CC/C3 padding (end of previous function)
                if (b == 0xCC || b == 0xC3) {
                    uintptr_t candidate = addr + 1;
                    // Skip CC padding
                    while (*reinterpret_cast<const uint8_t*>(candidate) == 0xCC) {
                        candidate++;
                    }
                    if (candidate < codeAddr && IsPrologue(candidate)) {
                        return candidate;
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
        return 0;
    }

    uintptr_t ScanForGlobalLoad(uintptr_t start, uintptr_t end, int nth) const {
        // Look for MOV reg, [RIP+disp32] (REX.W prefix: 48 8B xx)
        // or LEA reg, [RIP+disp32] (48 8D xx) pointing to .data section
        __try {
            int found = 0;
            auto* code = reinterpret_cast<const uint8_t*>(start);
            size_t len = end - start;

            for (size_t i = 0; i + 7 < len; i++) {
                // 48 8B xx where mod=0, rm=5 (RIP-relative)
                bool isMovRIP = (code[i] == 0x48 && code[i + 1] == 0x8B);
                // 4C 8B xx (REX.WR MOV)
                bool isMovRIP2 = (code[i] == 0x4C && code[i + 1] == 0x8B);

                if (isMovRIP || isMovRIP2) {
                    uint8_t modrm = code[i + 2];
                    uint8_t mod = (modrm >> 6) & 3;
                    uint8_t rm = modrm & 7;
                    if (mod == 0 && rm == 5) {
                        int32_t disp;
                        std::memcpy(&disp, &code[i + 3], 4);
                        uintptr_t instrAddr = start + i;
                        uintptr_t target = instrAddr + 7 + disp;

                        // Check if target is in .data section
                        if (target >= m_dataBase && target < m_dataBase + m_dataSize) {
                            if (found == nth) return target;
                            found++;
                        }
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
        return 0;
    }

    bool IsPrologue(uintptr_t addr) const {
        __try {
            auto* p = reinterpret_cast<const uint8_t*>(addr);
            // mov [rsp+xx], rbx: 48 89 5C 24
            if (p[0] == 0x48 && p[1] == 0x89 && p[2] == 0x5C && p[3] == 0x24) return true;
            // mov [rsp+xx], rsi: 48 89 74 24
            if (p[0] == 0x48 && p[1] == 0x89 && p[2] == 0x74 && p[3] == 0x24) return true;
            // mov [rsp+xx], rcx: 48 89 4C 24
            if (p[0] == 0x48 && p[1] == 0x89 && p[2] == 0x4C && p[3] == 0x24) return true;
            // mov [rsp+xx], rdx: 48 89 54 24
            if (p[0] == 0x48 && p[1] == 0x89 && p[2] == 0x54 && p[3] == 0x24) return true;
            // mov [rsp+xx], rbp: 48 89 6C 24
            if (p[0] == 0x48 && p[1] == 0x89 && p[2] == 0x6C && p[3] == 0x24) return true;
            // mov [rsp+xx], r8: 4C 89 44 24
            if (p[0] == 0x4C && p[1] == 0x89 && p[2] == 0x44 && p[3] == 0x24) return true;
            // push rbx: 40 53
            if (p[0] == 0x40 && p[1] == 0x53) return true;
            // push rbp: 40 55
            if (p[0] == 0x40 && p[1] == 0x55) return true;
            // push rsi: 40 56
            if (p[0] == 0x40 && p[1] == 0x56) return true;
            // push rdi: 40 57
            if (p[0] == 0x40 && p[1] == 0x57) return true;
            // sub rsp, imm8: 48 83 EC
            if (p[0] == 0x48 && p[1] == 0x83 && p[2] == 0xEC) return true;
            // sub rsp, imm32: 48 81 EC
            if (p[0] == 0x48 && p[1] == 0x81 && p[2] == 0xEC) return true;
            // push rbp; REX: 55 48
            if (p[0] == 0x55 && (p[1] == 0x48 || p[1] == 0x8B)) return true;
            // push rbx; REX: 53 48
            if (p[0] == 0x53 && p[1] == 0x48) return true;
            // push r12/r13/r14/r15: 41 5x
            if (p[0] == 0x41 && (p[1] >= 0x54 && p[1] <= 0x57)) return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return false;
    }
};

// ── Resolve Game Functions ──

bool ResolveGameFunctions(const PatternScanner& scanner, GameFunctions& funcs) {
    uintptr_t base = scanner.GetBase();
    size_t moduleSize = scanner.GetSize();
    int resolved = 0;
    int total = 0;

    auto tryPattern = [&](const char* name, const char* pattern, void*& target) {
        total++;
        if (!pattern) {
            spdlog::debug("ResolveGameFunctions: '{}' has no pattern yet", name);
            return;
        }
        auto result = scanner.Find(pattern);
        if (result) {
            target = reinterpret_cast<void*>(result.address);
            resolved++;
            spdlog::info("ResolveGameFunctions: '{}' = 0x{:X} (pattern)", name, result.address);
        } else {
            spdlog::warn("ResolveGameFunctions: '{}' pattern not found", name);
        }
    };

    // Try pattern-based resolution first
    tryPattern("CharacterSpawn",       patterns::CHARACTER_SPAWN,        funcs.CharacterSpawn);
    tryPattern("CharacterDestroy",     patterns::CHARACTER_DESTROY,      funcs.CharacterDestroy);
    tryPattern("CharacterSetPosition", patterns::CHARACTER_SET_POSITION, funcs.CharacterSetPosition);
    tryPattern("CharacterMoveTo",      patterns::CHARACTER_MOVE_TO,      funcs.CharacterMoveTo);
    tryPattern("ApplyDamage",          patterns::APPLY_DAMAGE,           funcs.ApplyDamage);
    tryPattern("StartAttack",          patterns::START_ATTACK,           funcs.StartAttack);
    tryPattern("CharacterDeath",       patterns::CHARACTER_DEATH,        funcs.CharacterDeath);
    tryPattern("CharacterKO",          patterns::CHARACTER_KO,           funcs.CharacterKO);
    tryPattern("ZoneLoad",             patterns::ZONE_LOAD,              funcs.ZoneLoad);
    tryPattern("ZoneUnload",           patterns::ZONE_UNLOAD,            funcs.ZoneUnload);
    tryPattern("BuildingPlace",        patterns::BUILDING_PLACE,         funcs.BuildingPlace);
    tryPattern("BuildingDestroyed",    patterns::BUILDING_DESTROYED,     funcs.BuildingDestroyed);
    tryPattern("GameFrameUpdate",      patterns::GAME_FRAME_UPDATE,      funcs.GameFrameUpdate);
    tryPattern("TimeUpdate",           patterns::TIME_UPDATE,            funcs.TimeUpdate);
    tryPattern("SaveGame",             patterns::SAVE_GAME,              funcs.SaveGame);
    tryPattern("LoadGame",             patterns::LOAD_GAME,              funcs.LoadGame);
    tryPattern("CharacterSerialise",   patterns::CHARACTER_SERIALISE,    funcs.CharacterSerialise);
    tryPattern("InputKeyPressed",      patterns::INPUT_KEY_PRESSED,      funcs.InputKeyPressed);
    tryPattern("InputMouseMoved",      patterns::INPUT_MOUSE_MOVED,      funcs.InputMouseMoved);
    tryPattern("SquadCreate",          patterns::SQUAD_CREATE,           funcs.SquadCreate);
    tryPattern("SquadAddMember",       patterns::SQUAD_ADD_MEMBER,       funcs.SquadAddMember);
    tryPattern("ItemPickup",           patterns::ITEM_PICKUP,            funcs.ItemPickup);
    tryPattern("ItemDrop",             patterns::ITEM_DROP,              funcs.ItemDrop);
    tryPattern("HealthUpdate",         patterns::HEALTH_UPDATE,          funcs.HealthUpdate);
    tryPattern("CharacterStats",       patterns::CHARACTER_STATS,        funcs.CharacterStats);
    tryPattern("CutDamageMod",         patterns::CUT_DAMAGE_MOD,        funcs.CutDamageMod);
    tryPattern("Navmesh",              patterns::NAVMESH,                funcs.Navmesh);

    // ── Runtime String Scanner Fallback ──
    // If patterns failed, try runtime string-xref scanning
    int fallbackResolved = 0;
    RuntimeStringScanner rss(base, moduleSize);

    struct FallbackEntry {
        const char* label;
        const char* searchStr;
        int         searchLen;
        void**      target;
    };

    // Fallback strings verified to exist in kenshi_x64.exe v1.0.68
    FallbackEntry fallbacks[] = {
        {"CharacterSpawn",       "[RootObjectFactory::process] Character",           38, &funcs.CharacterSpawn},
        {"CharacterDestroy",     "NodeList::destroyNodesByBuilding",                 32, &funcs.CharacterDestroy},
        {"CharacterSetPosition", "HavokCharacter::setPosition moved someone off the world", 55, &funcs.CharacterSetPosition},
        {"CharacterMoveTo",      "pathfind",                                          8, &funcs.CharacterMoveTo},
        {"ApplyDamage",          "Attack damage effect",                             20, &funcs.ApplyDamage},
        {"StartAttack",          "Cutting damage",                                   14, &funcs.StartAttack},
        {"CharacterDeath",       "{1} has died from blood loss.",                     28, &funcs.CharacterDeath},
        {"CharacterKO",          "knockout",                                          8, &funcs.CharacterKO},
        {"ZoneLoad",             "zone.%d.%d.zone",                                  15, &funcs.ZoneLoad},
        {"ZoneUnload",           "destroyed navmesh",                                17, &funcs.ZoneUnload},
        {"BuildingPlace",        "[RootObjectFactory::createBuilding] Building",     45, &funcs.BuildingPlace},
        {"BuildingDestroyed",    "Building::setDestroyed",                           22, &funcs.BuildingDestroyed},
        {"GameFrameUpdate",      "Kenshi 1.0.",                                      11, &funcs.GameFrameUpdate},
        {"TimeUpdate",           "dayTime",                                           7, &funcs.TimeUpdate},
        {"SaveGame",             "quicksave",                                         9, &funcs.SaveGame},
        {"LoadGame",             "[SaveManager::loadGame] No towns loaded.",          40, &funcs.LoadGame},
        {"CharacterSerialise",   "[Character::serialise] Character '",               33, &funcs.CharacterSerialise},
        {"HealthUpdate",         "damage resistance max",                            21, &funcs.HealthUpdate},
        {"CharacterStats",       "CharacterStats_Attributes",                        25, &funcs.CharacterStats},
    };

    for (auto& fb : fallbacks) {
        if (*fb.target != nullptr) continue; // Already resolved by pattern

        uintptr_t addr = rss.FindFunctionByString(fb.searchStr, fb.searchLen);
        if (addr) {
            *fb.target = reinterpret_cast<void*>(addr);
            fallbackResolved++;
            spdlog::info("ResolveGameFunctions: '{}' = 0x{:X} (string fallback)", fb.label, addr);
        }
    }

    // ── Auto-discover global pointers ──
    // Instead of hardcoding version-specific offsets, we find globals by
    // scanning for .data section references near known strings.

    // PlayerBase: Find the global pointer that the squad/player code loads.
    // The CharacterStats_Attributes function accesses the player interface,
    // and RootObjectFactory::process accesses the factory singleton.
    // Try multiple anchors for PlayerBase discovery.
    if (funcs.PlayerBase == 0) {
        // Try hardcoded offset first (fast, works if version matches)
        uintptr_t hardcoded = base + 0x01AC8A90;
        uintptr_t testRead = 0;
        if (Memory::Read(hardcoded, testRead) && testRead != 0 && testRead > base) {
            funcs.PlayerBase = hardcoded;
            spdlog::info("ResolveGameFunctions: 'PlayerBase' = 0x{:X} (hardcoded, -> 0x{:X})",
                         hardcoded, testRead);
        } else {
            // Hardcoded offset failed — try runtime discovery.
            // The player squad list is accessed from many functions. We search
            // for a function referencing "CharacterStats_Attributes" and look
            // for .data global loads nearby.
            spdlog::info("ResolveGameFunctions: Hardcoded PlayerBase failed, trying runtime discovery...");

            // Try each string anchor's function for nearby globals
            const char* playerAnchors[] = {
                "CharacterStats_Attributes",
                "Reset squad positions",
                "[Character::serialise] Character '",
            };
            int playerAnchorLens[] = { 25, 21, 33 };

            for (int a = 0; a < 3 && funcs.PlayerBase == 0; a++) {
                // Try each of the first few .data globals near the anchor
                for (int n = 0; n < 5 && funcs.PlayerBase == 0; n++) {
                    uintptr_t globalAddr = rss.FindGlobalNearString(
                        playerAnchors[a], playerAnchorLens[a], n);
                    if (globalAddr) {
                        uintptr_t val = 0;
                        if (Memory::Read(globalAddr, val) && val != 0 && val > base) {
                            funcs.PlayerBase = globalAddr;
                            spdlog::info("ResolveGameFunctions: 'PlayerBase' = 0x{:X} (discovered via '{}', nth={}, -> 0x{:X})",
                                         globalAddr, playerAnchors[a], n, val);
                        }
                    }
                }
            }
        }
    }

    // GameWorld singleton: referenced by time/speed/world management functions.
    // The "dayTime" string is used in the time update code, which loads GameWorld.
    if (funcs.GameWorldSingleton == 0) {
        // Try hardcoded GOG offset first
        uintptr_t hardcoded = base + 0x2133040;
        uintptr_t testRead = 0;
        if (Memory::Read(hardcoded, testRead) && testRead != 0 && testRead > base) {
            funcs.GameWorldSingleton = hardcoded;
            spdlog::info("ResolveGameFunctions: 'GameWorldSingleton' = 0x{:X} (hardcoded, -> 0x{:X})",
                         hardcoded, testRead);
        } else {
            spdlog::info("ResolveGameFunctions: Hardcoded GameWorld failed, trying runtime discovery...");
            const char* worldAnchors[] = { "dayTime", "zone.%d.%d.zone" };
            int worldAnchorLens[] = { 7, 15 };

            for (int a = 0; a < 2 && funcs.GameWorldSingleton == 0; a++) {
                for (int n = 0; n < 5 && funcs.GameWorldSingleton == 0; n++) {
                    uintptr_t globalAddr = rss.FindGlobalNearString(
                        worldAnchors[a], worldAnchorLens[a], n);
                    if (globalAddr) {
                        uintptr_t val = 0;
                        if (Memory::Read(globalAddr, val) && val != 0 && val > base) {
                            funcs.GameWorldSingleton = globalAddr;
                            spdlog::info("ResolveGameFunctions: 'GameWorldSingleton' = 0x{:X} (discovered via '{}', nth={}, -> 0x{:X})",
                                         globalAddr, worldAnchors[a], n, val);
                        }
                    }
                }
            }
        }
    }

    int totalResolved = resolved + fallbackResolved;
    spdlog::info("ResolveGameFunctions: Resolved {} pattern + {} fallback = {} total, PlayerBase=0x{:X}",
                 resolved, fallbackResolved, totalResolved, funcs.PlayerBase);

    return funcs.IsMinimallyResolved();
}

} // namespace kmp
