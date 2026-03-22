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
    uintptr_t FindGlobalNearString(const char* searchStr, int searchLen,
                                    int nth = 0, bool includeReadOnly = false) const {
        if (!m_textBase || !m_rdataBase) return 0;

        uintptr_t strAddr = FindStringInMemory(searchStr, searchLen);
        if (!strAddr) {
            spdlog::debug("FindGlobalNearString('{}', nth={}): string not found", searchStr, nth);
            return 0;
        }

        uintptr_t xref = FindStringXref(strAddr);
        if (!xref) {
            spdlog::debug("FindGlobalNearString('{}', nth={}): no xref for str@0x{:X}", searchStr, nth, strAddr);
            return 0;
        }

        uintptr_t funcStart = FindFunctionStart(xref);
        if (!funcStart) {
            spdlog::debug("FindGlobalNearString('{}', nth={}): no prologue found near xref@0x{:X}", searchStr, nth, xref);
            return 0;
        }

        // Scan the entire function (from func start through 512 past the xref)
        // for MOV/LEA reg, [RIP+disp32] pointing to data sections
        uintptr_t scanStart = funcStart;
        uintptr_t scanEnd = xref + 512;
        if (scanEnd > m_textBase + m_textSize) scanEnd = m_textBase + m_textSize;

        uintptr_t result = ScanForGlobalLoadImpl(scanStart, scanEnd, nth, includeReadOnly);
        spdlog::debug("FindGlobalNearString('{}', nth={}, ro={}): str@0x{:X} xref@0x{:X} func@0x{:X} scan[0x{:X}..0x{:X}] => 0x{:X}",
                      searchStr, nth, includeReadOnly, strAddr, xref, funcStart, scanStart, scanEnd, result);
        return result;
    }

    // Getters for section info
    uintptr_t GetDataBase() const { return m_dataBase; }
    size_t GetDataSize() const { return m_dataSize; }

    // Public wrapper: find string address (used by internal methods + external diagnostics)
    uintptr_t FindStringInMemory(const char* searchStr, int len) const {
        return FindStringInMemoryImpl(searchStr, len);
    }

    // Diagnostic: find code xref to string (public for logging)
    uintptr_t FindStringXref(uintptr_t stringAddr) const {
        return FindStringXrefImpl(stringAddr);
    }

    // Public: scan a code range for RIP-relative loads from data sections
    uintptr_t ScanForGlobalLoad(uintptr_t start, uintptr_t end, int nth,
                                bool includeReadOnly = false) const {
        return ScanForGlobalLoadImpl(start, end, nth, includeReadOnly);
    }

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

    // Check if an address is in a writable module section (not .text, not .rdata)
    bool IsInWritableSection(uintptr_t addr) const {
        if (addr < m_base || addr >= m_base + m_size) return false;
        // Exclude .text (code) and .rdata (read-only data)
        if (addr >= m_textBase && addr < m_textBase + m_textSize) return false;
        if (addr >= m_rdataBase && addr < m_rdataBase + m_rdataSize) return false;
        // Everything else in the module is potentially writable (.data, .bss, etc.)
        return true;
    }

    // Check if an address is in ANY data section (including .rdata)
    // Used for finding globals like PlayerBase which MSVC can place in .rdata
    // (static const pointers initialized at startup, read-only after that)
    bool IsInDataSection(uintptr_t addr) const {
        if (addr < m_base || addr >= m_base + m_size) return false;
        // Exclude .text (code) only
        if (addr >= m_textBase && addr < m_textBase + m_textSize) return false;
        return true;
    }

    uintptr_t FindStringInMemoryImpl(const char* searchStr, int len) const {
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

    uintptr_t FindStringXrefImpl(uintptr_t stringAddr) const {
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
        // Method 1: Use .pdata (RtlLookupFunctionEntry) — authoritative and works
        // for large functions (ApplyDamage=6925B, StartAttack=9253B) where the
        // string xref can be thousands of bytes from the prologue.
        __try {
            DWORD64 imageBase = 0;
            auto* rtFunc = RtlLookupFunctionEntry(
                static_cast<DWORD64>(codeAddr), &imageBase, nullptr);
            if (rtFunc) {
                uintptr_t funcStart = static_cast<uintptr_t>(imageBase) + rtFunc->BeginAddress;
                if (funcStart < codeAddr && funcStart > 0) {
                    return funcStart;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        // Method 2: Fallback — walk backwards looking for function prologue patterns.
        // Increased from 2048 to 16384 to handle large Kenshi functions.
        __try {
            for (uintptr_t addr = codeAddr - 1; addr > codeAddr - 16384 && addr > m_textBase; addr--) {
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

    uintptr_t ScanForGlobalLoadImpl(uintptr_t start, uintptr_t end, int nth,
                                    bool includeReadOnly = false) const {
        // Look for MOV reg, [RIP+disp32] (REX.W prefix: 48 8B/4C 8B)
        // and LEA reg, [RIP+disp32] (48 8D/4C 8D) pointing to data sections.
        // includeReadOnly=true also accepts .rdata targets (for const globals like PlayerBase)
        __try {
            int found = 0;
            auto* code = reinterpret_cast<const uint8_t*>(start);
            size_t len = end - start;

            for (size_t i = 0; i + 7 < len; i++) {
                // REX.W (48) or REX.WR (4C) prefix
                bool hasRex = (code[i] == 0x48 || code[i] == 0x4C);
                // MOV (8B) or LEA (8D) opcode
                bool isMemOp = (code[i + 1] == 0x8B || code[i + 1] == 0x8D);

                if (hasRex && isMemOp) {
                    uint8_t modrm = code[i + 2];
                    uint8_t mod = (modrm >> 6) & 3;
                    uint8_t rm = modrm & 7;
                    if (mod == 0 && rm == 5) {
                        int32_t disp;
                        std::memcpy(&disp, &code[i + 3], 4);
                        uintptr_t instrAddr = start + i;
                        uintptr_t target = instrAddr + 7 + disp;

                        bool accepted = includeReadOnly
                            ? IsInDataSection(target)
                            : IsInWritableSection(target);
                        if (accepted) {
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
            // mov rax, rsp: 48 8B C4 (common MSVC prologue in Kenshi)
            if (p[0] == 0x48 && p[1] == 0x8B && p[2] == 0xC4) return true;
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
        if (target != nullptr) {
            // Already resolved (e.g. by orchestrator vtable discovery) — don't overwrite
            resolved++;
            spdlog::debug("ResolveGameFunctions: '{}' already resolved at 0x{:X}, skipping pattern scan",
                          name, reinterpret_cast<uintptr_t>(target));
            return;
        }
        if (!pattern) {
            spdlog::debug("ResolveGameFunctions: '{}' has no pattern yet", name);
            return;
        }
        auto result = scanner.Find(pattern);
        if (result) {
            uintptr_t addr = result.address;

            // Validate pattern match is a real function entry using .pdata
            // Some patterns match inside a DIFFERENT function on Steam.
            // Auto-correcting to that function's start would hook the WRONG function → crash.
            // Only accept corrections within 16 bytes (alignment/prefix instructions).
            DWORD64 imageBase = 0;
            auto* rtFunc = RtlLookupFunctionEntry(
                static_cast<DWORD64>(addr), &imageBase, nullptr);
            uintptr_t funcStart = 0;
            if (rtFunc) {
                funcStart = static_cast<uintptr_t>(imageBase) + rtFunc->BeginAddress;
                if (funcStart != addr) {
                    uintptr_t offset = addr - funcStart;
                    if (offset <= 0x10) {
                        // Small offset — likely alignment padding or prefix instruction
                        spdlog::warn("ResolveGameFunctions: '{}' pattern at 0x{:X} (offset +0x{:X}). "
                                     "Auto-correcting to function start 0x{:X}",
                                     name, addr, offset, funcStart);
                        addr = funcStart;
                    } else {
                        // Large offset — pattern matched inside a WRONG function
                        spdlog::error("ResolveGameFunctions: '{}' pattern at 0x{:X} is +0x{:X} bytes into "
                                      "function 0x{:X} — WRONG FUNCTION, nulling",
                                      name, addr, offset, funcStart);
                        target = nullptr;
                        return;
                    }
                }
            }

            // Alignment check: MSVC usually aligns functions to 16 bytes, but NOT always.
            // .pdata is the authoritative source for function boundaries.
            // SquadAddMember at 0x928423 is a valid .pdata function entry despite not being
            // 16-byte aligned. Only warn, don't reject — .pdata already confirmed it above.
            if ((addr & 0xF) != 0) {
                if (rtFunc && funcStart == addr) {
                    // .pdata confirms this IS a function start despite odd alignment — accept it
                    spdlog::warn("ResolveGameFunctions: '{}' at 0x{:X} NOT 16-byte aligned "
                                 "(low nibble 0x{:X}) but .pdata confirms function start — accepting",
                                 name, addr, addr & 0xF);
                } else {
                    spdlog::error("ResolveGameFunctions: '{}' at 0x{:X} NOT 16-byte aligned "
                                  "(low nibble 0x{:X}) and no .pdata confirmation — nulling",
                                  name, addr, addr & 0xF);
                    target = nullptr;
                    return;
                }
            }

            target = reinterpret_cast<void*>(addr);
            resolved++;
            spdlog::info("ResolveGameFunctions: '{}' = 0x{:X} (pattern)", name, addr);
        } else {
            spdlog::warn("ResolveGameFunctions: '{}' pattern not found", name);
        }
    };

    // Try pattern-based resolution first

    // Entity lifecycle
    tryPattern("CharacterSpawn",       patterns::CHARACTER_SPAWN,        funcs.CharacterSpawn);
    tryPattern("CharacterDestroy",     patterns::CHARACTER_DESTROY,      funcs.CharacterDestroy);
    tryPattern("CreateRandomSquad",    patterns::CREATE_RANDOM_SQUAD,    funcs.CreateRandomSquad);
    tryPattern("CharacterSerialise",   patterns::CHARACTER_SERIALISE,    funcs.CharacterSerialise);
    tryPattern("CharacterKO",          patterns::CHARACTER_KO,           funcs.CharacterKO);

    // Movement
    tryPattern("CharacterSetPosition", patterns::CHARACTER_SET_POSITION, funcs.CharacterSetPosition);
    tryPattern("CharacterMoveTo",      patterns::CHARACTER_MOVE_TO,      funcs.CharacterMoveTo);

    // Combat
    tryPattern("ApplyDamage",          patterns::APPLY_DAMAGE,           funcs.ApplyDamage);
    tryPattern("StartAttack",          patterns::START_ATTACK,           funcs.StartAttack);
    tryPattern("CharacterDeath",       patterns::CHARACTER_DEATH,        funcs.CharacterDeath);
    tryPattern("HealthUpdate",         patterns::HEALTH_UPDATE,          funcs.HealthUpdate);
    tryPattern("CutDamageMod",         patterns::CUT_DAMAGE_MOD,        funcs.CutDamageMod);
    tryPattern("UnarmedDamage",        patterns::UNARMED_DAMAGE,        funcs.UnarmedDamage);
    tryPattern("MartialArtsCombat",    patterns::MARTIAL_ARTS_COMBAT,    funcs.MartialArtsCombat);

    // World / Zones
    tryPattern("ZoneLoad",             patterns::ZONE_LOAD,              funcs.ZoneLoad);
    tryPattern("ZoneUnload",           patterns::ZONE_UNLOAD,            funcs.ZoneUnload);
    tryPattern("BuildingPlace",        patterns::BUILDING_PLACE,         funcs.BuildingPlace);
    tryPattern("BuildingDestroyed",    patterns::BUILDING_DESTROYED,     funcs.BuildingDestroyed);
    tryPattern("Navmesh",              patterns::NAVMESH,                funcs.Navmesh);
    tryPattern("SpawnCheck",           patterns::SPAWN_CHECK,            funcs.SpawnCheck);

    // Game loop / Time
    tryPattern("GameFrameUpdate",      patterns::GAME_FRAME_UPDATE,      funcs.GameFrameUpdate);
    tryPattern("TimeUpdate",           patterns::TIME_UPDATE,            funcs.TimeUpdate);

    // Save/Load
    tryPattern("SaveGame",             patterns::SAVE_GAME,              funcs.SaveGame);
    tryPattern("LoadGame",             patterns::LOAD_GAME,              funcs.LoadGame);
    tryPattern("ImportGame",           patterns::IMPORT_GAME,            funcs.ImportGame);
    tryPattern("CharacterStats",       patterns::CHARACTER_STATS,        funcs.CharacterStats);

    // Input (optional - WndProc handles input)
    tryPattern("InputKeyPressed",      patterns::INPUT_KEY_PRESSED,      funcs.InputKeyPressed);
    tryPattern("InputMouseMoved",      patterns::INPUT_MOUSE_MOVED,      funcs.InputMouseMoved);

    // Squad / Platoon
    tryPattern("SquadCreate",          patterns::SQUAD_CREATE,           funcs.SquadCreate);
    tryPattern("SquadAddMember",       patterns::SQUAD_ADD_MEMBER,       funcs.SquadAddMember);

    // Inventory / Items
    tryPattern("ItemPickup",           patterns::ITEM_PICKUP,            funcs.ItemPickup);
    tryPattern("ItemDrop",             patterns::ITEM_DROP,              funcs.ItemDrop);
    tryPattern("BuyItem",              patterns::BUY_ITEM,               funcs.BuyItem);

    // Faction / Diplomacy
    tryPattern("FactionRelation",      patterns::FACTION_RELATION,       funcs.FactionRelation);

    // AI System
    tryPattern("AICreate",             patterns::AI_CREATE,              funcs.AICreate);
    tryPattern("AIPackages",           patterns::AI_PACKAGES,            funcs.AIPackages);

    // Turret / Ranged
    tryPattern("GunTurret",            patterns::GUN_TURRET,             funcs.GunTurret);
    tryPattern("GunTurretFire",        patterns::GUN_TURRET_FIRE,        funcs.GunTurretFire);

    // Building Management
    tryPattern("BuildingDismantle",    patterns::BUILDING_DISMANTLE,     funcs.BuildingDismantle);
    tryPattern("BuildingConstruct",    patterns::BUILDING_CONSTRUCT,     funcs.BuildingConstruct);
    tryPattern("BuildingRepair",       patterns::BUILDING_REPAIR,        funcs.BuildingRepair);

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
        // Entity lifecycle
        {"CharacterSpawn",       "[RootObjectFactory::process] Character",              38, &funcs.CharacterSpawn},
        {"CharacterDestroy",     "NodeList::destroyNodesByBuilding",                    32, &funcs.CharacterDestroy},
        {"CreateRandomSquad",    "[RootObjectFactory::createRandomSquad] Missing squad leader", 59, &funcs.CreateRandomSquad},
        {"CharacterSerialise",   "[Character::serialise] Character '",                  34, &funcs.CharacterSerialise},
        {"CharacterKO",          "knockout",                                             8, &funcs.CharacterKO},
        // Movement
        {"CharacterSetPosition", "HavokCharacter::setPosition moved someone off the world", 55, &funcs.CharacterSetPosition},
        // CharacterMoveTo: "pathfind" is too generic — finds wrong function on Steam.
        // Resolved via vtable discovery or remains null (position polling handles sync).
        // {"CharacterMoveTo",   "pathfind",                                             8, &funcs.CharacterMoveTo},
        // Combat
        {"ApplyDamage",          "Attack damage effect",                                20, &funcs.ApplyDamage},
        {"StartAttack",          "Cutting damage",                                      14, &funcs.StartAttack},
        {"CharacterDeath",       "{1} has died from blood loss.",                        29, &funcs.CharacterDeath},
        {"HealthUpdate",         "block chance",                                         12, &funcs.HealthUpdate},
        {"MartialArtsCombat",    "Martial Arts",                                        12, &funcs.MartialArtsCombat},
        // World / Zones
        {"ZoneLoad",             "zone.%d.%d.zone",                                     15, &funcs.ZoneLoad},
        {"ZoneUnload",           "destroyed navmesh",                                   17, &funcs.ZoneUnload},
        {"BuildingPlace",        "[RootObjectFactory::createBuilding] Building",         44, &funcs.BuildingPlace},
        {"BuildingDestroyed",    "Building::setDestroyed",                              22, &funcs.BuildingDestroyed},
        // Game loop / Time
        {"GameFrameUpdate",      "Kenshi 1.0.",                                         11, &funcs.GameFrameUpdate},
        {"TimeUpdate",           "timeScale",                                            9, &funcs.TimeUpdate},
        // Save/Load
        {"SaveGame",             "quicksave",                                             9, &funcs.SaveGame},
        {"LoadGame",             "[SaveManager::loadGame] No towns loaded.",             40, &funcs.LoadGame},
        {"ImportGame",           "[SaveManager::importGame] No towns loaded.",           42, &funcs.ImportGame},
        {"CharacterStats",       "CharacterStats_Attributes",                            25, &funcs.CharacterStats},
        // Squad / Platoon
        {"SquadCreate",          "Reset squad positions",                                21, &funcs.SquadCreate},
        // SquadAddMember: "delayedSpawningChecks" finds wrong function on Steam.
        // Resolved via vtable discovery (Squad vtable+0x10) in core.cpp instead.
        // {"SquadAddMember",    "delayedSpawningChecks",                               21, &funcs.SquadAddMember},
        // Inventory / Items
        {"ItemPickup",           "addItem",                                               7, &funcs.ItemPickup},
        {"ItemDrop",             "removeItem",                                           10, &funcs.ItemDrop},
        {"BuyItem",              "buyItem",                                               7, &funcs.BuyItem},
        // Faction / Diplomacy
        {"FactionRelation",      "faction relation",                                     16, &funcs.FactionRelation},
        // AI System
        {"AICreate",             "[AI::create] No faction for",                          27, &funcs.AICreate},
        {"AIPackages",           "AI packages",                                          11, &funcs.AIPackages},
        // Turret
        {"GunTurret",            "gun turret",                                           10, &funcs.GunTurret},
        // Building management
        {"BuildingDismantle",    "dismantle",                                             9, &funcs.BuildingDismantle},
        {"BuildingConstruct",    "construction progress",                                21, &funcs.BuildingConstruct},
    };

    for (auto& fb : fallbacks) {
        if (*fb.target != nullptr) continue; // Already resolved by pattern or orchestrator

        uintptr_t addr = rss.FindFunctionByString(fb.searchStr, fb.searchLen);
        if (addr) {
            // Cross-check with .pdata to ensure we found the real function entry
            DWORD64 imageBase = 0;
            auto* rtFunc = RtlLookupFunctionEntry(
                static_cast<DWORD64>(addr), &imageBase, nullptr);
            if (rtFunc) {
                uintptr_t funcStart = static_cast<uintptr_t>(imageBase) + rtFunc->BeginAddress;
                if (funcStart != addr) {
                    uintptr_t offset = addr - funcStart;
                    if (offset <= 0x10) {
                        spdlog::warn("ResolveGameFunctions: '{}' string fallback at 0x{:X} (offset +0x{:X}). "
                                     "Correcting to 0x{:X}", fb.label, addr, offset, funcStart);
                        addr = funcStart;
                    } else {
                        spdlog::error("ResolveGameFunctions: '{}' string fallback at 0x{:X} is +0x{:X} into "
                                      "function 0x{:X} — wrong function, skipping",
                                      fb.label, addr, offset, funcStart);
                        continue;
                    }
                }
            }
            *fb.target = reinterpret_cast<void*>(addr);
            fallbackResolved++;
            spdlog::info("ResolveGameFunctions: '{}' = 0x{:X} (string fallback)", fb.label, addr);
        } else {
            // Diagnose WHY the string fallback failed
            uintptr_t strAddr = rss.FindStringInMemory(fb.searchStr, fb.searchLen);
            if (strAddr) {
                uintptr_t xref = rss.FindStringXref(strAddr);
                if (xref) {
                    spdlog::warn("ResolveGameFunctions: '{}' — string at 0x{:X}, xref at 0x{:X}, "
                                 "but FindFunctionStart failed (no prologue found)",
                                 fb.label, strAddr, xref);
                } else {
                    spdlog::warn("ResolveGameFunctions: '{}' — string at 0x{:X} but NO code xref "
                                 "(no LEA instruction references this string)", fb.label, strAddr);
                }
            } else {
                spdlog::warn("ResolveGameFunctions: '{}' — string '{}' NOT FOUND in binary",
                             fb.label, std::string(fb.searchStr, fb.searchLen));
            }
        }
    }

    // ── Auto-discover global pointers ──
    // Instead of hardcoding version-specific offsets, we find globals by
    // scanning for .data section references near known strings.

    // Helper: validate that a pointer value looks like a real user-mode address.
    // On x64 Windows, user-mode addresses are below 0x00007FFFFFFFFFFF.
    // We also exclude very low addresses (< 0x10000) and uninitialized sentinels.
    auto isValidUserPtr = [](uintptr_t val) -> bool {
        return val > 0x10000 && val < 0x00007FFFFFFFFFFF &&
               val != 0xFFFFFFFFFFFFFFFF &&
               val != 0xCCCCCCCCCCCCCCCC &&
               val != 0xCDCDCDCDCDCDCDCD;
    };

    // ── Function-disassembly global discovery ──
    // The pattern scanner already resolved exact function addresses. We can scan
    // their code for MOV/LEA reg,[RIP+disp32] pointing into .data to find globals
    // like PlayerBase and GameWorld without hardcoded RVAs. This is the most
    // reliable approach since it works identically on GOG and Steam.
    auto findGlobalInFunction = [&](uintptr_t funcAddr, int nth) -> uintptr_t {
        if (!funcAddr) return 0;
        // Use .pdata to determine function end (accurate), fallback to 4KB scan.
        // 1KB was too small — many Kenshi functions are 2-8KB and load globals late.
        uintptr_t scanEnd = funcAddr + 4096;
        DWORD64 imageBase = 0;
        auto* rtFunc = RtlLookupFunctionEntry(
            static_cast<DWORD64>(funcAddr), &imageBase, nullptr);
        if (rtFunc) {
            uintptr_t pdataEnd = static_cast<uintptr_t>(imageBase) + rtFunc->EndAddress;
            if (pdataEnd > funcAddr && pdataEnd < funcAddr + 65536) {
                scanEnd = pdataEnd;
            }
        }
        return rss.ScanForGlobalLoad(funcAddr, scanEnd, nth, true);
    };

    // ── Semantic validation: does this look like a real PlayerBase? ──
    // PlayerBase is a pointer-to-pointer: *PlayerBase -> object with vtable.
    // After game loads, the object should have a valid faction list at known offsets.
    // CRITICAL: The object must be HEAP-allocated (outside module image range).
    // .data addresses containing .text pointers (function ptrs, vtables) are NOT PlayerBase.
    auto validatePlayerBase = [&](uintptr_t candidateAddr) -> bool {
        if (candidateAddr == 0) return false;
        uintptr_t val = 0;
        if (!Memory::Read(candidateAddr, val)) return false;
        // Value=0 means game not loaded yet — NOT validated, but may be correct.
        // Don't accept zero as valid — wait for re-check after game loads.
        if (val == 0) return false;
        if (!isValidUserPtr(val)) return false;
        // MUST be outside module image — real game objects are heap-allocated.
        // .data globals containing .text/.rdata pointers are false positives.
        if (val >= base && val < base + moduleSize) return false;
        // Double-deref: object should have a readable vtable in .text range
        uintptr_t vtable = 0;
        if (!Memory::Read(val, vtable)) return false;
        // vtable should point into the module's .text section
        uintptr_t textStart = base + 0x1000; // .text is typically at base+0x1000
        uintptr_t textEnd = base + moduleSize;
        if (vtable < textStart || vtable >= textEnd) return false;
        return true;
    };

    // PlayerBase: Find the global pointer that the squad/player code loads.
    // Priority: 1) function disassembly, 2) string-xref, 3) hardcoded (GOG only)
    //
    // STEAM FIX: During early init, the game hasn't loaded, so all globals are 0.
    // validatePlayerBase rejects val==0. We do a TWO-PASS approach:
    //   Pass 1: Strict (val must be valid heap pointer) — works on GOG and post-load Steam
    //   Pass 2: Tentative (accept val==0 if address is in .data section) — works on Steam pre-load
    // Pass 2 candidates are re-validated by RetryGlobalDiscovery after game loads.
    auto tentativeGlobalValidation = [&](uintptr_t candidateAddr) -> bool {
        if (candidateAddr == 0) return false;
        uintptr_t val = 0;
        if (!Memory::Read(candidateAddr, val)) return false;
        // Accept null (game not loaded yet) IF the address is in .data
        if (val == 0) {
            bool inData = rss.GetDataBase() != 0 &&
                          candidateAddr >= rss.GetDataBase() &&
                          candidateAddr < rss.GetDataBase() + rss.GetDataSize();
            return inData;
        }
        // Non-null: apply full validation
        if (!isValidUserPtr(val)) return false;
        if (val >= base && val < base + moduleSize) return false;
        uintptr_t vtable = 0;
        if (!Memory::Read(val, vtable)) return false;
        if (vtable < base + 0x1000 || vtable >= base + moduleSize) return false;
        return true;
    };

    if (funcs.PlayerBase == 0) {
        // Method 1: Scan resolved functions for .data globals (most reliable)
        // CharacterSpawn (RootObjectFactory::process) loads the factory singleton
        // which is often near or is PlayerBase.
        uintptr_t funcCandidates[] = {
            reinterpret_cast<uintptr_t>(funcs.CharacterSpawn),
            reinterpret_cast<uintptr_t>(funcs.CharacterStats),
            reinterpret_cast<uintptr_t>(funcs.SaveGame),
            reinterpret_cast<uintptr_t>(funcs.LoadGame),
        };
        const char* funcNames[] = { "CharacterSpawn", "CharacterStats", "SaveGame", "LoadGame" };

        // Pass 1: Strict validation (non-null heap pointer with vtable)
        for (int fi = 0; fi < 4 && funcs.PlayerBase == 0; fi++) {
            if (!funcCandidates[fi]) continue;
            for (int nth = 0; nth < 16 && funcs.PlayerBase == 0; nth++) {
                uintptr_t candidate = findGlobalInFunction(funcCandidates[fi], nth);
                if (!candidate) continue;
                uintptr_t val = 0;
                Memory::Read(candidate, val);
                if (validatePlayerBase(candidate)) {
                    funcs.PlayerBase = candidate;
                    spdlog::info("ResolveGameFunctions: 'PlayerBase' = 0x{:X} (func-disasm via '{}', nth={}, -> 0x{:X})",
                                 candidate, funcNames[fi], nth, val);
                } else {
                    spdlog::debug("ResolveGameFunctions: PlayerBase candidate 0x{:X} rejected "
                                  "(func='{}', nth={}, val=0x{:X}, {})",
                                  candidate, funcNames[fi], nth, val,
                                  (val == 0) ? "zero" :
                                  (val >= base && val < base + moduleSize) ? "in-module" : "vtable-fail");
                }
            }
        }

        // Method 2: Runtime string-xref discovery (works on any version)
        if (funcs.PlayerBase == 0) {
            const char* playerAnchors[] = {
                "CharacterStats_Attributes",
                "Reset squad positions",
                "[Character::serialise] Character '",
                "[RootObjectFactory::process] Character",
            };
            int playerAnchorLens[] = { 25, 21, 33, 38 };

            for (int a = 0; a < 4 && funcs.PlayerBase == 0; a++) {
                for (int n = 0; n < 12 && funcs.PlayerBase == 0; n++) {
                    uintptr_t globalAddr = rss.FindGlobalNearString(
                        playerAnchors[a], playerAnchorLens[a], n, true);
                    if (globalAddr && validatePlayerBase(globalAddr)) {
                        uintptr_t val = 0;
                        Memory::Read(globalAddr, val);
                        funcs.PlayerBase = globalAddr;
                        spdlog::info("ResolveGameFunctions: 'PlayerBase' = 0x{:X} (string-xref via '{}', nth={}, -> 0x{:X})",
                                     globalAddr, playerAnchors[a], n, val);
                    }
                }
            }
        }

        // Method 3: Hardcoded GOG offset (last resort)
        if (funcs.PlayerBase == 0) {
            uintptr_t hardcoded = base + 0x01AC8A90;
            if (validatePlayerBase(hardcoded)) {
                uintptr_t val = 0;
                Memory::Read(hardcoded, val);
                funcs.PlayerBase = hardcoded;
                spdlog::info("ResolveGameFunctions: 'PlayerBase' = 0x{:X} (hardcoded GOG, -> 0x{:X})",
                             hardcoded, val);
            }
        }

        // Pass 2 (Steam pre-load): Accept null-valued .data globals tentatively.
        // These will be re-validated by RetryGlobalDiscovery after the game loads.
        if (funcs.PlayerBase == 0) {
            spdlog::info("ResolveGameFunctions: PlayerBase not found (strict) — trying tentative (null-allowed)...");
            for (int fi = 0; fi < 4 && funcs.PlayerBase == 0; fi++) {
                if (!funcCandidates[fi]) continue;
                for (int nth = 0; nth < 16 && funcs.PlayerBase == 0; nth++) {
                    uintptr_t candidate = findGlobalInFunction(funcCandidates[fi], nth);
                    if (!candidate) continue;
                    if (tentativeGlobalValidation(candidate)) {
                        uintptr_t val = 0;
                        Memory::Read(candidate, val);
                        funcs.PlayerBase = candidate;
                        spdlog::info("ResolveGameFunctions: 'PlayerBase' = 0x{:X} (TENTATIVE, func='{}', nth={}, val=0x{:X}) — needs re-validation after game load",
                                     candidate, funcNames[fi], nth, val);
                    }
                }
            }

            // Tentative string-xref pass
            if (funcs.PlayerBase == 0) {
                const char* playerAnchors[] = {
                    "CharacterStats_Attributes",
                    "Reset squad positions",
                    "[Character::serialise] Character '",
                    "[RootObjectFactory::process] Character",
                };
                int playerAnchorLens[] = { 25, 21, 33, 38 };

                for (int a = 0; a < 4 && funcs.PlayerBase == 0; a++) {
                    for (int n = 0; n < 12 && funcs.PlayerBase == 0; n++) {
                        uintptr_t globalAddr = rss.FindGlobalNearString(
                            playerAnchors[a], playerAnchorLens[a], n, true);
                        if (globalAddr && tentativeGlobalValidation(globalAddr)) {
                            uintptr_t val = 0;
                            Memory::Read(globalAddr, val);
                            funcs.PlayerBase = globalAddr;
                            spdlog::info("ResolveGameFunctions: 'PlayerBase' = 0x{:X} (TENTATIVE string-xref via '{}', nth={}, val=0x{:X})",
                                         globalAddr, playerAnchors[a], n, val);
                        }
                    }
                }
            }

            if (funcs.PlayerBase == 0) {
                spdlog::warn("ResolveGameFunctions: PlayerBase not found — will retry after game loads");
            }
        }
    }

    // GameWorld singleton: referenced by time/speed/world management functions.
    // Priority: 1) function disassembly, 2) string-xref, 3) hardcoded (GOG only)
    auto validateGameWorld = [&](uintptr_t candidateAddr) -> bool {
        if (candidateAddr == 0) return false;
        uintptr_t val = 0;
        if (!Memory::Read(candidateAddr, val)) return false;
        if (val == 0) return false; // Game not loaded — don't accept yet
        if (!isValidUserPtr(val)) return false;
        // MUST be outside module image — real game objects are heap-allocated
        if (val >= base && val < base + moduleSize) return false;
        uintptr_t vtable = 0;
        if (!Memory::Read(val, vtable)) return false;
        uintptr_t textStart = base + 0x1000;
        uintptr_t textEnd = base + moduleSize;
        if (vtable < textStart || vtable >= textEnd) return false;
        return true;
    };

    if (funcs.GameWorldSingleton == 0) {
        // Method 1: Scan resolved functions for .data globals
        uintptr_t gwFuncCandidates[] = {
            reinterpret_cast<uintptr_t>(funcs.TimeUpdate),
            reinterpret_cast<uintptr_t>(funcs.ZoneLoad),
            reinterpret_cast<uintptr_t>(funcs.GameFrameUpdate),
            reinterpret_cast<uintptr_t>(funcs.SaveGame),
        };
        const char* gwFuncNames[] = { "TimeUpdate", "ZoneLoad", "GameFrameUpdate", "SaveGame" };

        for (int fi = 0; fi < 4 && funcs.GameWorldSingleton == 0; fi++) {
            if (!gwFuncCandidates[fi]) continue;
            for (int nth = 0; nth < 16 && funcs.GameWorldSingleton == 0; nth++) {
                uintptr_t candidate = findGlobalInFunction(gwFuncCandidates[fi], nth);
                if (!candidate) continue;
                uintptr_t val = 0;
                Memory::Read(candidate, val);
                if (validateGameWorld(candidate)) {
                    funcs.GameWorldSingleton = candidate;
                    spdlog::info("ResolveGameFunctions: 'GameWorldSingleton' = 0x{:X} (func-disasm via '{}', nth={}, -> 0x{:X})",
                                 candidate, gwFuncNames[fi], nth, val);
                } else {
                    spdlog::debug("ResolveGameFunctions: GameWorld candidate 0x{:X} rejected "
                                  "(func='{}', nth={}, val=0x{:X}, {})",
                                  candidate, gwFuncNames[fi], nth, val,
                                  (val == 0) ? "zero" :
                                  (val >= base && val < base + moduleSize) ? "in-module" : "vtable-fail");
                }
            }
        }

        // Method 2: Runtime string-xref discovery
        if (funcs.GameWorldSingleton == 0) {
            const char* worldAnchors[] = { "dayTime", "zone.%d.%d.zone", "Kenshi 1.0." };
            int worldAnchorLens[] = { 7, 15, 11 };

            for (int a = 0; a < 3 && funcs.GameWorldSingleton == 0; a++) {
                for (int n = 0; n < 12 && funcs.GameWorldSingleton == 0; n++) {
                    uintptr_t globalAddr = rss.FindGlobalNearString(
                        worldAnchors[a], worldAnchorLens[a], n, true);
                    if (globalAddr && validateGameWorld(globalAddr)) {
                        uintptr_t val = 0;
                        Memory::Read(globalAddr, val);
                        funcs.GameWorldSingleton = globalAddr;
                        spdlog::info("ResolveGameFunctions: 'GameWorldSingleton' = 0x{:X} (string-xref via '{}', nth={}, -> 0x{:X})",
                                     globalAddr, worldAnchors[a], n, val);
                    }
                }
            }
        }

        // Method 3: Hardcoded GOG offset (last resort)
        if (funcs.GameWorldSingleton == 0) {
            uintptr_t hardcoded = base + 0x2133040;
            if (validateGameWorld(hardcoded)) {
                uintptr_t val = 0;
                Memory::Read(hardcoded, val);
                funcs.GameWorldSingleton = hardcoded;
                spdlog::info("ResolveGameFunctions: 'GameWorldSingleton' = 0x{:X} (hardcoded GOG, -> 0x{:X})",
                             hardcoded, val);
            }
        }

        // Pass 2 (Steam pre-load): Accept null-valued .data globals tentatively.
        if (funcs.GameWorldSingleton == 0) {
            spdlog::info("ResolveGameFunctions: GameWorld not found (strict) — trying tentative (null-allowed)...");
            uintptr_t gwFuncCandidates2[] = {
                reinterpret_cast<uintptr_t>(funcs.TimeUpdate),
                reinterpret_cast<uintptr_t>(funcs.ZoneLoad),
                reinterpret_cast<uintptr_t>(funcs.GameFrameUpdate),
                reinterpret_cast<uintptr_t>(funcs.SaveGame),
            };
            const char* gwFuncNames2[] = { "TimeUpdate", "ZoneLoad", "GameFrameUpdate", "SaveGame" };

            for (int fi = 0; fi < 4 && funcs.GameWorldSingleton == 0; fi++) {
                if (!gwFuncCandidates2[fi]) continue;
                for (int nth = 0; nth < 16 && funcs.GameWorldSingleton == 0; nth++) {
                    uintptr_t candidate = findGlobalInFunction(gwFuncCandidates2[fi], nth);
                    if (!candidate) continue;
                    if (tentativeGlobalValidation(candidate)) {
                        uintptr_t val = 0;
                        Memory::Read(candidate, val);
                        funcs.GameWorldSingleton = candidate;
                        spdlog::info("ResolveGameFunctions: 'GameWorldSingleton' = 0x{:X} (TENTATIVE, func='{}', nth={}, val=0x{:X})",
                                     candidate, gwFuncNames2[fi], nth, val);
                    }
                }
            }

            // Tentative string-xref pass
            if (funcs.GameWorldSingleton == 0) {
                const char* worldAnchors2[] = { "dayTime", "zone.%d.%d.zone", "Kenshi 1.0." };
                int worldAnchorLens2[] = { 7, 15, 11 };

                for (int a = 0; a < 3 && funcs.GameWorldSingleton == 0; a++) {
                    for (int n = 0; n < 12 && funcs.GameWorldSingleton == 0; n++) {
                        uintptr_t globalAddr = rss.FindGlobalNearString(
                            worldAnchors2[a], worldAnchorLens2[a], n, true);
                        if (globalAddr && tentativeGlobalValidation(globalAddr)) {
                            uintptr_t val = 0;
                            Memory::Read(globalAddr, val);
                            funcs.GameWorldSingleton = globalAddr;
                            spdlog::info("ResolveGameFunctions: 'GameWorldSingleton' = 0x{:X} (TENTATIVE string-xref via '{}', nth={}, val=0x{:X})",
                                         globalAddr, worldAnchors2[a], n, val);
                        }
                    }
                }
            }

            if (funcs.GameWorldSingleton == 0) {
                spdlog::warn("ResolveGameFunctions: GameWorld not found — will retry after game loads");
            }
        }
    }

    int totalResolved = resolved + fallbackResolved;
    spdlog::info("ResolveGameFunctions: Resolved {} pattern + {} fallback = {} total, PlayerBase=0x{:X}",
                 resolved, fallbackResolved, totalResolved, funcs.PlayerBase);

    return funcs.IsMinimallyResolved();
}

bool RetryGlobalDiscovery(const PatternScanner& scanner, GameFunctions& funcs) {
    uintptr_t base = scanner.GetBase();
    size_t moduleSize = scanner.GetSize();
    RuntimeStringScanner rss(base, moduleSize);
    bool found = false;

    auto isValidUserPtr = [](uintptr_t val) -> bool {
        return val > 0x10000 && val < 0x00007FFFFFFFFFFF &&
               val != 0xFFFFFFFFFFFFFFFF &&
               val != 0xCCCCCCCCCCCCCCCC &&
               val != 0xCDCDCDCDCDCDCDCD;
    };

    // Semantic validation: heap-allocated object with vtable in .text
    auto validateGlobal = [&](uintptr_t candidateAddr) -> bool {
        if (candidateAddr == 0) return false;
        uintptr_t val = 0;
        if (!Memory::Read(candidateAddr, val)) return false;
        if (!isValidUserPtr(val)) return false;
        // MUST be outside module image — real game objects are heap-allocated
        if (val >= base && val < base + moduleSize) return false;
        uintptr_t vtable = 0;
        if (!Memory::Read(val, vtable)) return false;
        uintptr_t textStart = base + 0x1000;
        uintptr_t textEnd = base + moduleSize;
        return (vtable >= textStart && vtable < textEnd);
    };

    auto findGlobalInFunction = [&](uintptr_t funcAddr, int nth) -> uintptr_t {
        if (!funcAddr) return 0;
        // Use .pdata to determine function end (accurate), fallback to 4KB scan.
        // 1KB was too small — many Kenshi functions are 2-8KB and load globals late.
        uintptr_t scanEnd = funcAddr + 4096;
        DWORD64 imageBase = 0;
        auto* rtFunc = RtlLookupFunctionEntry(
            static_cast<DWORD64>(funcAddr), &imageBase, nullptr);
        if (rtFunc) {
            uintptr_t pdataEnd = static_cast<uintptr_t>(imageBase) + rtFunc->EndAddress;
            if (pdataEnd > funcAddr && pdataEnd < funcAddr + 65536) {
                scanEnd = pdataEnd;
            }
        }
        return rss.ScanForGlobalLoad(funcAddr, scanEnd, nth, true);
    };

    // ── PlayerBase retry ──
    if (funcs.PlayerBase != 0) {
        if (validateGlobal(funcs.PlayerBase)) {
            uintptr_t val = 0;
            Memory::Read(funcs.PlayerBase, val);
            spdlog::info("RetryGlobalDiscovery: PlayerBase 0x{:X} now valid -> 0x{:X}", funcs.PlayerBase, val);
        } else {
            spdlog::info("RetryGlobalDiscovery: PlayerBase 0x{:X} still invalid — re-scanning...", funcs.PlayerBase);
            funcs.PlayerBase = 0;
        }
    }

    if (funcs.PlayerBase == 0) {
        // Method 1: Function disassembly (most reliable after game load)
        uintptr_t funcCandidates[] = {
            reinterpret_cast<uintptr_t>(funcs.CharacterSpawn),
            reinterpret_cast<uintptr_t>(funcs.CharacterStats),
            reinterpret_cast<uintptr_t>(funcs.SaveGame),
            reinterpret_cast<uintptr_t>(funcs.LoadGame),
        };
        const char* funcNames[] = { "CharacterSpawn", "CharacterStats", "SaveGame", "LoadGame" };

        for (int fi = 0; fi < 4 && funcs.PlayerBase == 0; fi++) {
            if (!funcCandidates[fi]) continue;
            for (int nth = 0; nth < 16 && funcs.PlayerBase == 0; nth++) {
                uintptr_t candidate = findGlobalInFunction(funcCandidates[fi], nth);
                if (candidate && validateGlobal(candidate)) {
                    uintptr_t val = 0;
                    Memory::Read(candidate, val);
                    funcs.PlayerBase = candidate;
                    found = true;
                    spdlog::info("RetryGlobalDiscovery: 'PlayerBase' = 0x{:X} (func-disasm via '{}', nth={}, -> 0x{:X})",
                                 candidate, funcNames[fi], nth, val);
                }
            }
        }

        // Method 2: String-xref fallback
        if (funcs.PlayerBase == 0) {
            const char* playerAnchors[] = {
                "CharacterStats_Attributes",
                "Reset squad positions",
                "[Character::serialise] Character '",
                "[RootObjectFactory::process] Character",
                "quicksave",
            };
            int playerAnchorLens[] = { 25, 21, 33, 38, 9 };

            for (int a = 0; a < 5 && funcs.PlayerBase == 0; a++) {
                for (int n = 0; n < 12 && funcs.PlayerBase == 0; n++) {
                    uintptr_t globalAddr = rss.FindGlobalNearString(
                        playerAnchors[a], playerAnchorLens[a], n, true);
                    if (globalAddr && validateGlobal(globalAddr)) {
                        uintptr_t val = 0;
                        Memory::Read(globalAddr, val);
                        funcs.PlayerBase = globalAddr;
                        found = true;
                        spdlog::info("RetryGlobalDiscovery: 'PlayerBase' = 0x{:X} (string-xref via '{}', nth={}, -> 0x{:X})",
                                     globalAddr, playerAnchors[a], n, val);
                    }
                }
            }
        }
    }

    // ── GameWorld retry ──
    if (funcs.GameWorldSingleton != 0) {
        if (validateGlobal(funcs.GameWorldSingleton)) {
            // Already good
        } else {
            funcs.GameWorldSingleton = 0;
        }
    }

    if (funcs.GameWorldSingleton == 0) {
        // Method 1: Function disassembly
        uintptr_t gwFuncCandidates[] = {
            reinterpret_cast<uintptr_t>(funcs.TimeUpdate),
            reinterpret_cast<uintptr_t>(funcs.ZoneLoad),
            reinterpret_cast<uintptr_t>(funcs.GameFrameUpdate),
            reinterpret_cast<uintptr_t>(funcs.SaveGame),
        };
        const char* gwFuncNames[] = { "TimeUpdate", "ZoneLoad", "GameFrameUpdate", "SaveGame" };

        for (int fi = 0; fi < 4 && funcs.GameWorldSingleton == 0; fi++) {
            if (!gwFuncCandidates[fi]) continue;
            for (int nth = 0; nth < 16 && funcs.GameWorldSingleton == 0; nth++) {
                uintptr_t candidate = findGlobalInFunction(gwFuncCandidates[fi], nth);
                if (candidate && validateGlobal(candidate)) {
                    uintptr_t val = 0;
                    Memory::Read(candidate, val);
                    funcs.GameWorldSingleton = candidate;
                    spdlog::info("RetryGlobalDiscovery: 'GameWorldSingleton' = 0x{:X} (func-disasm via '{}', nth={}, -> 0x{:X})",
                                 candidate, gwFuncNames[fi], nth, val);
                }
            }
        }

        // Method 2: String-xref fallback
        if (funcs.GameWorldSingleton == 0) {
            const char* worldAnchors[] = { "dayTime", "zone.%d.%d.zone", "Kenshi 1.0." };
            int worldAnchorLens[] = { 7, 15, 11 };

            for (int a = 0; a < 3 && funcs.GameWorldSingleton == 0; a++) {
                for (int n = 0; n < 12 && funcs.GameWorldSingleton == 0; n++) {
                    uintptr_t globalAddr = rss.FindGlobalNearString(
                        worldAnchors[a], worldAnchorLens[a], n, true);
                    if (globalAddr && validateGlobal(globalAddr)) {
                        uintptr_t val = 0;
                        Memory::Read(globalAddr, val);
                        funcs.GameWorldSingleton = globalAddr;
                        spdlog::info("RetryGlobalDiscovery: 'GameWorldSingleton' = 0x{:X} (string-xref via '{}', nth={}, -> 0x{:X})",
                                     globalAddr, worldAnchors[a], n, val);
                    }
                }
            }
        }
    }

    return found || (funcs.PlayerBase != 0);
}

} // namespace kmp
