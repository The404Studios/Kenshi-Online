#include "game_offset_prober.h"
#include "game_types.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <fstream>
#include <string>
#include <cmath>
#include <cstdio>

using json = nlohmann::json;

namespace kmp::game {

// ═══════════════════════════════════════════════════════════════════════════
//  INTERNAL STATE
// ═══════════════════════════════════════════════════════════════════════════

static bool s_proberComplete = false;
static bool s_proberRanOnce  = false;  // Prevent re-running after first full pass

// Per-offset probed flags — each is attempted at most once per session.
static bool s_probedSceneNode        = false;
static bool s_probedIsPlayerCtrl     = false;
static bool s_probedAnimState        = false;
static bool s_probedAIPackage        = false;
static bool s_probedMoveSpeed        = false;

// Cache file path (next to the DLL / game exe)
static const char* CACHE_FILE = "KenshiOnline_offset_cache.json";

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════════════════

// SEH-protected single-value read. Returns false on access violation.
template<typename T>
static bool SafeRead(uintptr_t addr, T& out) {
    __try {
        out = *reinterpret_cast<const T*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Check if a pointer looks like a valid heap-allocated object (usermode, aligned, outside module).
static bool IsValidHeapPtr(uintptr_t ptr) {
    if (ptr < 0x10000 || ptr >= 0x00007FFFFFFFFFFF) return false;
    if ((ptr & 0x7) != 0) return false;
    // Reject if inside the game executable's image
    uintptr_t modBase = Memory::GetModuleBase();
    if (ptr >= modBase && ptr < modBase + 0x4000000) return false;
    return true;
}

// Check if a pointer's vtable lives inside a specific module's image range.
static bool HasVtableInModule(uintptr_t objPtr, uintptr_t moduleBase, uintptr_t moduleEnd) {
    if (moduleBase == 0 || moduleEnd == 0) return false;
    uintptr_t vtable = 0;
    if (!SafeRead(objPtr, vtable)) return false;
    return vtable >= moduleBase && vtable < moduleEnd;
}

// Get module base and end for a named DLL (cached after first call).
static bool GetModuleRange(const char* dllName, uintptr_t& base, uintptr_t& end) {
    HMODULE hMod = GetModuleHandleA(dllName);
    if (!hMod) return false;
    base = reinterpret_cast<uintptr_t>(hMod);
    __try {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
        end = base + nt->OptionalHeader.SizeOfImage;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// Compute a simple hash of the game executable for cache invalidation.
// Uses file size + PE timestamp as a fast fingerprint (no need for crypto hash).
static uint64_t ComputeExeFingerprint() {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    HANDLE hFile = CreateFileA(exePath, GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return 0;

    DWORD fileSize = GetFileSize(hFile, nullptr);
    CloseHandle(hFile);

    // Also read PE timestamp from the executable in memory
    uintptr_t modBase = Memory::GetModuleBase();
    DWORD peTimestamp = 0;
    __try {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(modBase);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(modBase + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE)
                peTimestamp = nt->FileHeader.TimeDateStamp;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    // Combine file size and PE timestamp into a single 64-bit fingerprint
    return (static_cast<uint64_t>(peTimestamp) << 32) | static_cast<uint64_t>(fileSize);
}

// Get the directory where the cache file should be written.
// Uses the Kenshi game directory (parent of the KenshiMP dir).
static std::string GetCachePath() {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string path(exePath);
    auto pos = path.find_last_of("\\/");
    if (pos != std::string::npos) {
        path = path.substr(0, pos + 1);
    }
    path += CACHE_FILE;
    return path;
}

// ═══════════════════════════════════════════════════════════════════════════
//  INDIVIDUAL PROBES
// ═══════════════════════════════════════════════════════════════════════════

// ── sceneNode ──
// Ogre::SceneNode* should be a pointer in the character struct whose vtable
// resides inside OgreMain_x64.dll. The sceneNode also contains the character's
// world transform, so we can cross-check the position.
static bool ProbeSceneNode(uintptr_t charPtr) {
    if (s_probedSceneNode) return false;
    s_probedSceneNode = true;

    auto& offsets = GetOffsets().character;
    if (offsets.sceneNode >= 0) return false; // Already known

    // Resolve OgreMain module range
    static uintptr_t s_ogreBase = 0, s_ogreEnd = 0;
    if (s_ogreBase == 0) {
        if (!GetModuleRange("OgreMain_x64.dll", s_ogreBase, s_ogreEnd)) {
            spdlog::debug("OffsetProber: OgreMain_x64.dll not loaded, skipping sceneNode probe");
            return false;
        }
    }

    // Read the character's cached position for cross-validation
    Vec3 cachedPos;
    if (offsets.position < 0) return false;
    Memory::ReadVec3(charPtr + offsets.position, cachedPos.x, cachedPos.y, cachedPos.z);
    if (cachedPos.x == 0.f && cachedPos.y == 0.f && cachedPos.z == 0.f) {
        s_probedSceneNode = false; // Let another character try later
        return false;
    }

    // Scan pointer-aligned fields in the character struct (0x60..0x300).
    // The sceneNode is typically in the first part of the struct, after
    // basic fields like vtable, squad, faction, name.
    for (int probe = 0x60; probe <= 0x300; probe += 8) {
        uintptr_t candidate = 0;
        if (!SafeRead(charPtr + probe, candidate)) continue;
        if (!IsValidHeapPtr(candidate)) continue;

        // Check 1: vtable must point into OgreMain_x64.dll
        if (!HasVtableInModule(candidate, s_ogreBase, s_ogreEnd)) continue;

        // Check 2: Ogre::SceneNode::_getDerivedPosition() stores the world
        // position. In Ogre 1.x, the derived position is typically at
        // SceneNode+0x50..0x5C (Vec3) or at Node+0xD0..0xDC depending on build.
        // Try several common Ogre::Node position offsets.
        static const int ogre_pos_offsets[] = {
            0x4C, 0x50, 0x54,    // Ogre 1.7-1.9 SceneNode internal position
            0xD0, 0xD4, 0xD8,    // Ogre 1.x Node derived position (some builds)
            0xA0, 0xA4, 0xA8,    // Alternative layout
            0x10, 0x14, 0x18,    // Inline after vtable
        };

        for (int i = 0; i < sizeof(ogre_pos_offsets) / sizeof(int); i += 3) {
            float nx = 0.f, ny = 0.f, nz = 0.f;
            if (!SafeRead(candidate + ogre_pos_offsets[i],     nx)) continue;
            if (!SafeRead(candidate + ogre_pos_offsets[i + 1], ny)) continue;
            if (!SafeRead(candidate + ogre_pos_offsets[i + 2], nz)) continue;

            // Position match within tolerance (Ogre transform may lag by a frame)
            float dx = std::abs(nx - cachedPos.x);
            float dy = std::abs(ny - cachedPos.y);
            float dz = std::abs(nz - cachedPos.z);

            if (dx < 5.0f && dy < 5.0f && dz < 5.0f) {
                offsets.sceneNode = probe;
                spdlog::info("OffsetProber: Discovered sceneNode offset = 0x{:X} "
                             "(Ogre vtable confirmed, pos match at node+0x{:X})",
                             probe, ogre_pos_offsets[i]);
                return true;
            }
        }
    }

    spdlog::debug("OffsetProber: sceneNode probe failed — no Ogre::SceneNode pointer found");
    return false;
}

// ── isPlayerControlled ──
// Differential probe: compare a known player character with a known NPC.
// The byte that is 1 on the player and 0 on the NPC is the flag.
// This wraps the existing ProbePlayerControlledOffset logic with SEH.
static bool ProbeIsPlayerControlled(uintptr_t playerPtr, uintptr_t npcPtr) {
    if (s_probedIsPlayerCtrl) return false;
    s_probedIsPlayerCtrl = true;

    auto& offsets = GetOffsets().character;
    if (offsets.isPlayerControlled >= 0) return false; // Already known

    if (playerPtr == 0 || npcPtr == 0) {
        s_probedIsPlayerCtrl = false; // Need both — retry later
        return false;
    }

    // Scan bytes in range 0x80..0x500 (past known pointer fields, before stats)
    // Use wider range than the original to catch edge cases.
    int candidateCount = 0;
    int bestCandidate = -1;

    for (int off = 0x80; off <= 0x500; off++) {
        uint8_t playerVal = 0, npcVal = 0;
        if (!SafeRead(playerPtr + off, playerVal)) continue;
        if (!SafeRead(npcPtr + off, npcVal)) continue;

        if (playerVal == 1 && npcVal == 0) {
            // Cross-validate: neighboring bytes should be 0 on both sides
            // (a standalone bool flag, not part of a multi-byte integer)
            uint8_t playerPrev = 0xFF, playerNext = 0xFF;
            uint8_t npcPrev = 0xFF, npcNext = 0xFF;
            SafeRead(playerPtr + off - 1, playerPrev);
            SafeRead(playerPtr + off + 1, playerNext);
            SafeRead(npcPtr + off - 1, npcPrev);
            SafeRead(npcPtr + off + 1, npcNext);

            if (playerPrev == 0 && playerNext == 0 && npcPrev == 0 && npcNext == 0) {
                candidateCount++;
                if (bestCandidate < 0) bestCandidate = off;
            }
        }
    }

    if (bestCandidate >= 0) {
        offsets.isPlayerControlled = bestCandidate;
        spdlog::info("OffsetProber: Discovered isPlayerControlled = 0x{:X} "
                     "({} total candidates, using first match)",
                     bestCandidate, candidateCount);
        return true;
    }

    spdlog::debug("OffsetProber: isPlayerControlled probe failed");
    return false;
}

// ── aiPackage ──
// The AI package pointer should be a heap-allocated object with a vtable in
// the game module (kenshi_x64.exe). It should be near the inventory/stats region.
// We look for a pointer in range 0x200..0x400 that has a game-module vtable
// and is NOT the inventory, stats, or other known pointers.
static bool ProbeAIPackage(uintptr_t charPtr) {
    if (s_probedAIPackage) return false;
    s_probedAIPackage = true;

    auto& offsets = GetOffsets().character;
    if (offsets.aiPackage >= 0) return false;

    uintptr_t modBase = Memory::GetModuleBase();
    uintptr_t modEnd  = modBase + 0x4000000; // Conservative 64MB estimate

    // Refine module end from PE headers (safe: PE headers are in our address space)
    {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(modBase);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(modBase + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE)
                modEnd = modBase + nt->OptionalHeader.SizeOfImage;
        }
    }

    // Known offsets to skip (these point to known objects, not AI packages)
    const int knownPtrOffsets[] = {
        offsets.faction,     // 0x10
        offsets.gameDataPtr, // 0x40
        offsets.inventory,   // 0x2E8
    };

    for (int probe = 0x200; probe <= 0x400; probe += 8) {
        // Skip known pointer offsets
        bool skip = false;
        for (int known : knownPtrOffsets) {
            if (known >= 0 && probe == known) { skip = true; break; }
        }
        if (skip) continue;

        uintptr_t candidate = 0;
        if (!SafeRead(charPtr + probe, candidate)) continue;
        if (!IsValidHeapPtr(candidate)) continue;

        // Vtable must be in the game module
        uintptr_t vtable = 0;
        if (!SafeRead(candidate, vtable)) continue;
        if (vtable < modBase || vtable >= modEnd) continue;

        // AI packages typically have a backpointer to the character or faction.
        // Check if any of the first 8 qwords in the AI object point back to
        // the character or its faction.
        uintptr_t factionPtr = 0;
        if (offsets.faction >= 0) SafeRead(charPtr + offsets.faction, factionPtr);

        bool hasBackRef = false;
        for (int i = 1; i <= 8; i++) {
            uintptr_t field = 0;
            if (!SafeRead(candidate + i * 8, field)) continue;
            if (field == charPtr || (factionPtr != 0 && field == factionPtr)) {
                hasBackRef = true;
                break;
            }
        }

        if (hasBackRef) {
            offsets.aiPackage = probe;
            spdlog::info("OffsetProber: Discovered aiPackage offset = 0x{:X} "
                         "(game vtable + backref confirmed)", probe);
            return true;
        }
    }

    spdlog::debug("OffsetProber: aiPackage probe failed");
    return false;
}

// ── moveSpeed ──
// Movement speed is a float that should be > 0 when the character is moving
// and ~0 when stationary. We look for a float in range [0, 30] in the
// character struct between offsets 0x200 and 0x450 that is NOT part of a
// known field. Since we can't observe motion in a single probe, we accept
// any reasonable-looking speed float.
static bool ProbeMoveSpeed(uintptr_t charPtr) {
    if (s_probedMoveSpeed) return false;
    s_probedMoveSpeed = true;

    auto& offsets = GetOffsets().character;
    if (offsets.moveSpeed >= 0) return false;

    // This probe has low confidence without observing change over time.
    // We'll scan for a float field that looks like a speed value (0..30 range,
    // not integer-valued which would suggest a counter or flag).
    // For now, skip this probe — it needs multi-frame observation.
    // The moveSpeed offset can be discovered later via a dedicated motion probe.
    spdlog::debug("OffsetProber: moveSpeed probe deferred — needs multi-frame observation");
    return false;
}

// ── animState ──
// Animation state is typically a small integer (0-255) or enum.
// Similar to moveSpeed, this needs observation over time to identify.
static bool ProbeAnimState(uintptr_t charPtr) {
    if (s_probedAnimState) return false;
    s_probedAnimState = true;

    auto& offsets = GetOffsets().character;
    if (offsets.animState >= 0) return false;

    // Deferred — needs multi-frame observation of animation transitions
    spdlog::debug("OffsetProber: animState probe deferred — needs multi-frame observation");
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
//  CACHE I/O
// ═══════════════════════════════════════════════════════════════════════════

bool LoadOffsetCache() {
    // NOTE: No __try/__except in this function — json objects have destructors,
    // and MSVC forbids SEH in functions that require C++ unwinding.
    // json::parse with allow_exceptions=false handles parse errors safely.

    std::string path = GetCachePath();
    std::ifstream file(path);
    if (!file.is_open()) {
        spdlog::debug("OffsetProber: No cache file at {}", path);
        return false;
    }

    json j = json::parse(file, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) {
        spdlog::warn("OffsetProber: Cache file is invalid JSON");
        return false;
    }

    // Validate fingerprint
    uint64_t cachedFingerprint = j.value("exe_fingerprint", uint64_t(0));
    uint64_t currentFingerprint = ComputeExeFingerprint();
    if (cachedFingerprint == 0 || cachedFingerprint != currentFingerprint) {
        spdlog::info("OffsetProber: Cache invalidated — exe fingerprint changed "
                     "(cached=0x{:X}, current=0x{:X})", cachedFingerprint, currentFingerprint);
        return false;
    }

    // Restore offsets from cache
    auto& offsets = GetOffsets().character;
    int restored = 0;

    auto restore = [&](const char* key, int& field) {
        if (j.contains(key) && j[key].is_number_integer()) {
            int val = j[key].get<int>();
            if (val >= 0) {
                field = val;
                restored++;
            }
        }
    };

    restore("sceneNode",          offsets.sceneNode);
    restore("isPlayerControlled", offsets.isPlayerControlled);
    restore("aiPackage",          offsets.aiPackage);
    restore("equipment",          offsets.equipment);
    restore("animClassOffset",    offsets.animClassOffset);
    restore("squad",              offsets.squad);
    restore("moveSpeed",          offsets.moveSpeed);
    restore("animState",          offsets.animState);

    if (restored > 0) {
        spdlog::info("OffsetProber: Restored {} offsets from cache ({})", restored, path);

        // Mark probes as complete for offsets that were restored
        if (offsets.sceneNode >= 0)          s_probedSceneNode = true;
        if (offsets.isPlayerControlled >= 0) s_probedIsPlayerCtrl = true;
        if (offsets.aiPackage >= 0)          s_probedAIPackage = true;
        if (offsets.moveSpeed >= 0)          s_probedMoveSpeed = true;
        if (offsets.animState >= 0)          s_probedAnimState = true;

        GetOffsets().discoveredByScanner = true;
        return true;
    }

    spdlog::debug("OffsetProber: Cache had no restorable offsets");
    return false;
}

void SaveOffsetCache() {
    auto& offsets = GetOffsets().character;

    json j;
    j["exe_fingerprint"] = ComputeExeFingerprint();
    j["version"]         = 1;

    // Save all runtime-discovered offsets (only save if >= 0, i.e., discovered)
    auto save = [&](const char* key, int val) {
        if (val >= 0) j[key] = val;
    };

    save("sceneNode",          offsets.sceneNode);
    save("isPlayerControlled", offsets.isPlayerControlled);
    save("aiPackage",          offsets.aiPackage);
    save("equipment",          offsets.equipment);
    save("animClassOffset",    offsets.animClassOffset);
    save("squad",              offsets.squad);
    save("moveSpeed",          offsets.moveSpeed);
    save("animState",          offsets.animState);

    std::string path = GetCachePath();
    std::ofstream file(path);
    if (!file.is_open()) {
        spdlog::warn("OffsetProber: Failed to write cache to {}", path);
        return;
    }

    file << j.dump(2);
    file.close();

    spdlog::info("OffsetProber: Saved offset cache to {} ({} entries)",
                 path, j.size() - 2); // -2 for fingerprint and version
}

// ═══════════════════════════════════════════════════════════════════════════
//  PUBLIC API
// ═══════════════════════════════════════════════════════════════════════════

// SEH-safe probe runner — NO C++ objects with destructors allowed in this function.
// MSVC forbids __try in functions that need C++ unwinding.
// Returns: number of newly discovered offsets. Sets flags in |results|.
struct ProbeResults {
    int  discovered;
    bool anyNew;
    bool sceneNodeCrashed;
    bool playerCtrlCrashed;
    bool aiPackageCrashed;
    bool moveSpeedCrashed;
    bool animStateCrashed;
};

static ProbeResults RunProbesSEH(uintptr_t charPtr, uintptr_t npcCharPtr) {
    ProbeResults r = {};

    __try {
        if (ProbeSceneNode(charPtr)) { r.discovered++; r.anyNew = true; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r.sceneNodeCrashed = true;
        s_probedSceneNode = true;
    }

    __try {
        if (ProbeIsPlayerControlled(charPtr, npcCharPtr)) { r.discovered++; r.anyNew = true; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r.playerCtrlCrashed = true;
        s_probedIsPlayerCtrl = true;
    }

    __try {
        if (ProbeAIPackage(charPtr)) { r.discovered++; r.anyNew = true; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r.aiPackageCrashed = true;
        s_probedAIPackage = true;
    }

    __try {
        if (ProbeMoveSpeed(charPtr)) { r.discovered++; r.anyNew = true; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r.moveSpeedCrashed = true;
        s_probedMoveSpeed = true;
    }

    __try {
        if (ProbeAnimState(charPtr)) { r.discovered++; r.anyNew = true; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r.animStateCrashed = true;
        s_probedAnimState = true;
    }

    return r;
}

bool RunOffsetProber(uintptr_t charPtr, uintptr_t npcCharPtr) {
    if (s_proberComplete) return false;
    if (charPtr == 0) return false;

    // Validate character pointer
    if (charPtr < 0x10000 || charPtr >= 0x00007FFFFFFFFFFF || (charPtr & 0x7) != 0)
        return false;

    // Verify we have a valid position (character must be fully initialized)
    auto& offsets = GetOffsets().character;
    Vec3 pos;
    if (offsets.position >= 0) {
        Memory::ReadVec3(charPtr + offsets.position, pos.x, pos.y, pos.z);
        if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) {
            return false; // Character not yet placed in world
        }
    }

    spdlog::info("OffsetProber: Running probe suite on char 0x{:X} (npc=0x{:X})",
                 charPtr, npcCharPtr);

    // Run all probes in a SEH-safe context (separate function, no C++ destructors)
    ProbeResults results = RunProbesSEH(charPtr, npcCharPtr);

    // Log crash warnings (safe to use spdlog here — we're outside __try)
    if (results.sceneNodeCrashed)
        spdlog::warn("OffsetProber: sceneNode probe crashed (SEH caught)");
    if (results.playerCtrlCrashed)
        spdlog::warn("OffsetProber: isPlayerControlled probe crashed (SEH caught)");
    if (results.aiPackageCrashed)
        spdlog::warn("OffsetProber: aiPackage probe crashed (SEH caught)");
    if (results.moveSpeedCrashed)
        spdlog::warn("OffsetProber: moveSpeed probe crashed (SEH caught)");
    if (results.animStateCrashed)
        spdlog::warn("OffsetProber: animState probe crashed (SEH caught)");

    // Log summary
    if (results.discovered > 0) {
        spdlog::info("OffsetProber: Discovered {} new offsets this run", results.discovered);
    }

    // Log final offset status — use spdlog's built-in hex formatting
    auto logOff = [](const char* name, int val) {
        if (val >= 0)
            spdlog::info("  {} = 0x{:X}", name, val);
        else
            spdlog::info("  {} = UNKNOWN", name);
    };
    spdlog::info("OffsetProber: Offset status after probe run:");
    logOff("sceneNode         ", offsets.sceneNode);
    logOff("isPlayerControlled", offsets.isPlayerControlled);
    logOff("aiPackage         ", offsets.aiPackage);
    logOff("equipment         ", offsets.equipment);
    logOff("animClassOffset   ", offsets.animClassOffset);
    logOff("squad             ", offsets.squad);
    logOff("moveSpeed         ", offsets.moveSpeed);
    logOff("animState         ", offsets.animState);

    // Check if all feasible probes have been attempted
    bool allProbed = s_probedSceneNode && s_probedIsPlayerCtrl &&
                     s_probedAIPackage && s_probedMoveSpeed && s_probedAnimState;

    if (allProbed) {
        s_proberComplete = true;
        spdlog::info("OffsetProber: All probes completed");

        // Save cache if anything was discovered
        if (results.anyNew || !s_proberRanOnce) {
            SaveOffsetCache();
        }
    }

    s_proberRanOnce = true;
    return results.anyNew;
}

void ResetOffsetProber() {
    s_proberComplete     = false;
    s_proberRanOnce      = false;
    s_probedSceneNode    = false;
    s_probedIsPlayerCtrl = false;
    s_probedAnimState    = false;
    s_probedAIPackage    = false;
    s_probedMoveSpeed    = false;
    spdlog::info("OffsetProber: Reset all probe state");
}

bool IsProberComplete() {
    return s_proberComplete;
}

} // namespace kmp::game
