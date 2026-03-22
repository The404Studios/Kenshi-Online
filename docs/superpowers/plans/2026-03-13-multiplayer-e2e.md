# Kenshi-Online End-to-End Multiplayer Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Get two Kenshi clients to see each other's characters walking around with correct animations, with full combat and inventory sync, using the proven squad-bypass spawning approach from the research mod.

**Architecture:** Three new subsystems added to KenshiMP.Core: (1) Squad Spawn Bypass hooks that inject remote characters through the game's natural squad spawning pipeline instead of crashing FactoryCreate calls, (2) Character Tracker hooks that maintain a live registry of all characters via animation update callbacks, enabling name-based player identification, (3) Lobby Manager that assigns faction identity to each connecting player pre-load. Additionally, combat hooks are hardened with proper SEH protection to fix the attack-crash bug.

**Tech Stack:** C++17, MinHook/VirtualAlloc detours, ENet networking, Kenshi x64 (Steam v1.0.68), pattern scanning

---

## Chunk 1: Squad Spawn Bypass System

This is the highest priority — without reliable character spawning, nothing else matters. The research mod proved this approach works by hooking two points in the squad spawning pipeline.

### Task 1: Add Pattern Entries for Squad Spawn Functions

**Files:**
- Modify: `KenshiMP.Scanner/include/kmp/patterns.h` (GameFunctions struct)
- Modify: `KenshiMP.Scanner/src/orchestrator.cpp` (RegisterBuiltinPatterns)

The research mod uses two hooks in the squad spawning pipeline. Our `SpawnCheck` pattern (RVA 0x4FFAD0) with string "tried to spawn inside walls!" is already in this function area. We need to add entries for the specific hook sites.

- [ ] **Step 1: Add GameFunctions members for squad spawn hooks**

In `KenshiMP.Scanner/include/kmp/patterns.h`, add after the `SpawnCheck` entry (~line 405):

```cpp
    void*  SquadSpawnBypass    = nullptr;  // Squad spawn check bypass (research mod: GOG 0x4FF47C)
    void*  SquadSpawnCall      = nullptr;  // Squad spawn function call site (research mod: GOG 0x4FFA88)
    void*  CharAnimUpdate      = nullptr;  // Character animation update callback (research mod: GOG 0x65F6C7)
```

- [ ] **Step 2: Register patterns in orchestrator**

In `KenshiMP.Scanner/src/orchestrator.cpp` inside `RegisterBuiltinPatterns()`, add after the SpawnCheck registration:

```cpp
    // ── Squad Spawn Bypass (from research mod RE) ──
    // The squad spawning pipeline checks whether to spawn squads near the player.
    // Hooking this allows injecting remote player characters through the game's
    // natural spawn pipeline — fully initialized with faction, AI, squad, animations.
    reg("SquadSpawnBypass", "entity", "Squad spawn check bypass",
        "48 8D AC 24 30 FF FF FF FF 48 81 EC D0 01 00 00",
        " tried to spawn inside walls!", 29,
        0x004FF47C, &funcs.SquadSpawnBypass);

    // Character animation update — fires for EVERY character each frame.
    // Research mod uses this to track all characters by name in real time.
    // Pattern from GOG: mov rcx,[rbx+320]; mov [rbx+37C],sil
    reg("CharAnimUpdate", "entity", "Character animation update tick",
        "48 8B 8B 20 03 00 00 40 88 B3 7C 03 00 00",
        nullptr, 0,
        0x0065F6C7, &funcs.CharAnimUpdate);
```

- [ ] **Step 3: Build and verify pattern scan finds addresses**

Run: Build KenshiMP solution, launch game, check logs for:
```
PatternOrchestrator: 'SquadSpawnBypass' = 0xXXXXXX
PatternOrchestrator: 'CharAnimUpdate' = 0xXXXXXX
```

If patterns are not found (GOG→Steam offset shift), we fall back to the hardcoded RVAs which need Steam verification. Alternative: use the `SpawnCheck` string anchor "tried to spawn inside walls!" to find the function, then walk backwards to find the squad bypass entry point.

- [ ] **Step 4: Commit**

```bash
git add KenshiMP.Scanner/include/kmp/patterns.h KenshiMP.Scanner/src/orchestrator.cpp
git commit -m "feat: add pattern entries for squad spawn bypass and char animation hooks"
```

### Task 2: Create Squad Spawn Hooks Module

**Files:**
- Create: `KenshiMP.Core/hooks/squad_spawn_hooks.h`
- Create: `KenshiMP.Core/hooks/squad_spawn_hooks.cpp`
- Modify: `KenshiMP.Core/CMakeLists.txt` (add new source files)

This implements the research mod's approach: hook the squad spawn evaluation to force-spawn characters using our mod GameData templates.

- [ ] **Step 1: Create header file**

```cpp
// KenshiMP.Core/hooks/squad_spawn_hooks.h
#pragma once
#include "kmp/types.h"
#include <queue>
#include <mutex>
#include <atomic>

namespace kmp::squad_spawn_hooks {

// Install hooks for squad spawn bypass
bool Install();
void Uninstall();

// Queue a character spawn request. When the game next evaluates squad spawning,
// the hook will force-spawn this character through the natural pipeline.
void QueueSquadSpawn(void* gameData, const Vec3& position);

// Get number of pending squad spawns
int GetPendingCount();

// Get total successful squad bypass spawns
int GetSuccessCount();

} // namespace kmp::squad_spawn_hooks
```

- [ ] **Step 2: Create implementation file**

The key insight from the research mod: they hook TWO sites in the squad spawning function.
1. The ENTRY where the function checks whether to spawn (bypass the checks)
2. The CALL SITE where the factory function is called (inject our GameData + position)

Since exact assembly injection like the research mod requires knowing the exact instruction layout on Steam (which may differ from GOG), we take a slightly different approach: we hook the function entry with MinHook and modify the `activePlatoon` struct fields to force spawning, similar to `bypassSquadSpawningCheck` in the research mod.

```cpp
// KenshiMP.Core/hooks/squad_spawn_hooks.cpp
#pragma once
#include "squad_spawn_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "../game/spawn_manager.h"
#include "../hooks/entity_hooks.h"
#include "../hooks/ai_hooks.h"
#include "kmp/hook_manager.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <queue>
#include <mutex>
#include <atomic>

namespace kmp::squad_spawn_hooks {

// ── Spawn queue ──
struct SquadSpawnRequest {
    void* gameData;  // GameData* for the character template
    Vec3 position;
};

static std::mutex s_queueMutex;
static std::queue<SquadSpawnRequest> s_spawnQueue;
static std::atomic<int> s_successCount{0};
static std::atomic<int> s_bypassCount{0};

// ── activePlatoon struct offsets (from research mod RE) ──
// These are the three checks the game evaluates to decide whether to spawn:
static constexpr int OFFSET_SKIP_CHECK_1 = 0xF0;   // bool: if 1, skip spawning
static constexpr int OFFSET_SKIP_CHECK_2 = 0x58;   // bool: if >0, also skip
static constexpr int OFFSET_SKIP_CHECK_3 = 0x250;  // void*: must be 0 for bypass
static constexpr int OFFSET_SQUAD_PTR    = 0x78;   // platoon*
static constexpr int OFFSET_LEADER       = 0xA0;   // CharacterHuman* leader

// ── Hook function type ──
// The squad spawn check function — called for each activePlatoon when evaluating spawns.
// Research mod signature: (rsp prologue) lea rbp,[rsp-D0]; sub rsp,1D0
// Arguments passed via calling convention (this = activePlatoon*):
// RCX = unknown context (game engine), RDX = activePlatoon*
using SquadSpawnCheckFn = void(__fastcall*)(void* context, void* activePlatoon);
static SquadSpawnCheckFn s_origSquadSpawnCheck = nullptr;

// ── SEH-safe struct reads ──
static bool SEH_ReadBool(uintptr_t addr, bool& out) {
    __try {
        out = *reinterpret_cast<bool*>(addr);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool SEH_WriteBool(uintptr_t addr, bool val) {
    __try {
        *reinterpret_cast<bool*>(addr) = val;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool SEH_ReadPtr(uintptr_t addr, uintptr_t& out) {
    __try {
        out = *reinterpret_cast<uintptr_t*>(addr);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
}

static bool SEH_WritePtr(uintptr_t addr, uintptr_t val) {
    __try {
        *reinterpret_cast<uintptr_t*>(addr) = val;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Saved state for restoration after bypass
struct SavedChecks {
    bool check1;
    bool check2;
    uintptr_t platoonAddr;
    bool valid;
};
static SavedChecks s_savedChecks = {};

// ── Hook implementation ──
// This mimics the research mod's bypassSquadSpawningCheck:
// When we have a queued spawn AND the activePlatoon meets bypass conditions,
// we flip the spawn checks to force the game to spawn a character.
// The spawned character is then hijacked by entity_hooks (NPC Hijack path).
static void __fastcall Hook_SquadSpawnCheck(void* context, void* activePlatoon) {
    uintptr_t apAddr = reinterpret_cast<uintptr_t>(activePlatoon);

    // Check if we have pending spawns
    bool hasPending = false;
    {
        std::lock_guard lock(s_queueMutex);
        hasPending = !s_spawnQueue.empty();
    }

    if (hasPending && apAddr > 0x10000 && apAddr < 0x00007FFFFFFFFFFF) {
        // Read the three spawn checks from the activePlatoon struct
        bool check1 = false, check2 = false;
        uintptr_t check3 = 0;

        SEH_ReadBool(apAddr + OFFSET_SKIP_CHECK_1, check1);
        SEH_ReadBool(apAddr + OFFSET_SKIP_CHECK_2, check2);
        SEH_ReadPtr(apAddr + OFFSET_SKIP_CHECK_3, check3);

        // Research mod condition: check3 == 0 AND check1 == 1 (would normally skip)
        // We flip check1 to 0 to FORCE spawning
        if (check3 == 0 && check1 == true) {
            int bypassNum = s_bypassCount.fetch_add(1) + 1;
            spdlog::info("squad_spawn_hooks: BYPASSING spawn check #{} (platoon=0x{:X})",
                         bypassNum, apAddr);

            // Save original values for restoration
            s_savedChecks.check1 = check1;
            s_savedChecks.check2 = check2;
            s_savedChecks.platoonAddr = apAddr;
            s_savedChecks.valid = true;

            // Flip checks to force spawn
            SEH_WriteBool(apAddr + OFFSET_SKIP_CHECK_1, false);  // Allow spawning
            SEH_WriteBool(apAddr + OFFSET_SKIP_CHECK_2, false);  // Allow spawning

            // The spawn will happen inside the original function.
            // entity_hooks::Hook_CharacterCreate will fire and NPC Hijack
            // will grab the character for our spawn queue.

            // Pop the spawn request so entity_hooks knows to hijack
            {
                std::lock_guard lock(s_queueMutex);
                if (!s_spawnQueue.empty()) {
                    SquadSpawnRequest req = s_spawnQueue.front();
                    s_spawnQueue.pop();

                    // Queue this as a SpawnRequest for entity_hooks NPC hijack
                    SpawnRequest spawnReq;
                    spawnReq.netId = 0; // Will be filled by the caller
                    spawnReq.owner = 0;
                    spawnReq.type = EntityType::PlayerCharacter;
                    spawnReq.position = req.position;
                    // SpawnManager will handle the rest via its queue
                }
            }
        }
    }

    // Call original function — this evaluates the (possibly modified) checks
    // and spawns characters if conditions are met
    s_origSquadSpawnCheck(context, activePlatoon);

    // Restore original check values after the function returns
    if (s_savedChecks.valid && s_savedChecks.platoonAddr == apAddr) {
        SEH_WriteBool(apAddr + OFFSET_SKIP_CHECK_1, s_savedChecks.check1);
        SEH_WriteBool(apAddr + OFFSET_SKIP_CHECK_2, s_savedChecks.check2);
        s_savedChecks.valid = false;

        spdlog::info("squad_spawn_hooks: Restored original checks for platoon 0x{:X}", apAddr);
        s_successCount.fetch_add(1);
    }
}

// ── Public API ──

void QueueSquadSpawn(void* gameData, const Vec3& position) {
    std::lock_guard lock(s_queueMutex);
    s_spawnQueue.push({gameData, position});
    spdlog::info("squad_spawn_hooks: Queued spawn at ({:.0f},{:.0f},{:.0f}), pending={}",
                 position.x, position.y, position.z, s_spawnQueue.size());
}

int GetPendingCount() {
    std::lock_guard lock(s_queueMutex);
    return static_cast<int>(s_spawnQueue.size());
}

int GetSuccessCount() {
    return s_successCount.load();
}

bool Install() {
    auto& core = Core::Get();
    auto& hookMgr = HookManager::Get();
    auto& funcs = core.GetGameFunctions();

    if (!funcs.SquadSpawnBypass) {
        spdlog::warn("squad_spawn_hooks: SquadSpawnBypass address not resolved — hook not installed");
        return false;
    }

    uintptr_t targetAddr = reinterpret_cast<uintptr_t>(funcs.SquadSpawnBypass);
    spdlog::info("squad_spawn_hooks: Installing at 0x{:X}", targetAddr);

    if (!hookMgr.InstallAt("SquadSpawnBypass", targetAddr,
                            &Hook_SquadSpawnCheck, &s_origSquadSpawnCheck)) {
        spdlog::error("squad_spawn_hooks: Failed to install SquadSpawnBypass hook");
        return false;
    }

    spdlog::info("squad_spawn_hooks: Installed successfully");
    return true;
}

void Uninstall() {
    HookManager::Get().Remove("SquadSpawnBypass");
}

} // namespace kmp::squad_spawn_hooks
```

- [ ] **Step 3: Add to CMakeLists.txt**

Add `hooks/squad_spawn_hooks.cpp` and `hooks/squad_spawn_hooks.h` to the KenshiMP.Core source list.

- [ ] **Step 4: Build and verify compilation**

Run: Build solution. Expected: Clean compile with no errors.

- [ ] **Step 5: Commit**

```bash
git add KenshiMP.Core/hooks/squad_spawn_hooks.h KenshiMP.Core/hooks/squad_spawn_hooks.cpp KenshiMP.Core/CMakeLists.txt
git commit -m "feat: add squad spawn bypass hook module"
```

### Task 3: Create Character Tracker Hook Module

**Files:**
- Create: `KenshiMP.Core/hooks/char_tracker_hooks.h`
- Create: `KenshiMP.Core/hooks/char_tracker_hooks.cpp`
- Modify: `KenshiMP.Core/CMakeLists.txt`

This hooks the character animation update function to maintain a live map of ALL characters by name — independent of CharacterCreate. Enables finding "Player 1" / "Player 2" characters by name match.

- [ ] **Step 1: Create header**

```cpp
// KenshiMP.Core/hooks/char_tracker_hooks.h
#pragma once
#include "kmp/types.h"
#include <string>
#include <functional>

namespace kmp::char_tracker_hooks {

bool Install();
void Uninstall();

// Tracked character info
struct TrackedChar {
    void* animClassPtr;     // AnimationClassHuman*
    void* characterPtr;     // CharacterHuman* (at animClass+0x2D8)
    std::string name;
    Vec3 position;          // Last known position (from movement chain)
    uint64_t lastSeenTick;  // GetTickCount64() of last update
};

// Find a tracked character by name (exact match)
const TrackedChar* FindByName(const std::string& name);

// Find a tracked character by game pointer
const TrackedChar* FindByPtr(void* characterPtr);

// Get the current player's AnimationClassHuman* (matched by local player name)
void* GetLocalPlayerAnimClass();

// Get a remote player's AnimationClassHuman* (matched by name from server)
void* GetRemotePlayerAnimClass(const std::string& name);

// Set the callback for when a new character is first seen
void SetOnNewCharacter(std::function<void(const TrackedChar&)> callback);

// Get total tracked character count
int GetTrackedCount();

// Diagnostic: dump all tracked characters to log
void DumpTrackedChars();

} // namespace kmp::char_tracker_hooks
```

- [ ] **Step 2: Create implementation**

```cpp
// KenshiMP.Core/hooks/char_tracker_hooks.cpp
#include "char_tracker_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "../game/spawn_manager.h"
#include "kmp/hook_manager.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <unordered_map>
#include <mutex>

namespace kmp::char_tracker_hooks {

// ── Hook type ──
// The animation update function is called with the AnimationClassHuman* in a register.
// Research mod hook site: mov rcx,[rbx+320]; mov [rbx+37C],sil
// RBX = AnimationClassHuman* at this instruction
// We hook the FUNCTION that contains this instruction, getting AnimationClassHuman* as a parameter.
//
// The exact calling convention depends on where in the function we hook.
// If we hook the function ENTRY: RCX = this (some context), but we need to inspect
// stack/registers for the AnimationClassHuman*.
//
// Alternative approach: since MinHook hooks function entries, and the charUpdateHook
// address (0x65F6C7) is MID-FUNCTION (not a function start), we use VirtualAlloc
// inline hooking like the research mod does — replace the 14 bytes at that address
// with a JMP to our trampoline, call our function, then execute the original bytes.

// Storage
static std::mutex s_trackerMutex;
static std::unordered_map<void*, TrackedChar> s_trackedChars;  // Key: CharacterHuman*
static std::function<void(const TrackedChar&)> s_onNewChar;
static void* s_localPlayerAnimClass = nullptr;

// The callback invoked when the animation update fires
static void OnCharUpdate(void* animClassHuman) {
    if (!animClassHuman) return;
    uintptr_t animPtr = reinterpret_cast<uintptr_t>(animClassHuman);
    if (animPtr < 0x10000 || animPtr > 0x00007FFFFFFFFFFF) return;

    // Read CharacterHuman* at animClass+0x2D8 (research mod offset)
    uintptr_t charPtr = 0;
    if (!Memory::Read(animPtr + 0x2D8, charPtr) || charPtr == 0) return;
    if (charPtr < 0x10000 || charPtr > 0x00007FFFFFFFFFFF) return;

    void* charKey = reinterpret_cast<void*>(charPtr);
    uint64_t now = GetTickCount64();

    std::lock_guard lock(s_trackerMutex);
    auto it = s_trackedChars.find(charKey);
    if (it == s_trackedChars.end()) {
        // New character — read name
        game::CharacterAccessor accessor(charKey);
        std::string name = accessor.GetName();
        if (name.empty()) return;

        TrackedChar tc;
        tc.animClassPtr = animClassHuman;
        tc.characterPtr = charKey;
        tc.name = name;
        tc.position = accessor.GetPosition();
        tc.lastSeenTick = now;
        s_trackedChars[charKey] = tc;

        spdlog::info("char_tracker: NEW character '{}' at 0x{:X} (animClass=0x{:X})",
                     name, charPtr, animPtr);

        if (s_onNewChar) {
            s_onNewChar(tc);
        }
    } else {
        // Update existing
        it->second.animClassPtr = animClassHuman;
        it->second.lastSeenTick = now;

        // Update position periodically (every ~500ms to avoid spam)
        if (now - it->second.lastSeenTick > 500) {
            game::CharacterAccessor accessor(charKey);
            it->second.position = accessor.GetPosition();
        }
    }
}

// ── Inline Hook Implementation ──
// Since CharAnimUpdate (0x65F6C7) is mid-function, we can't use MinHook.
// Instead, we use the same VirtualAlloc code-cave approach as the research mod.

static uint8_t s_originalBytes[14] = {};  // Original bytes at hook site
static void* s_trampolineAlloc = nullptr;

// SEH-safe callback wrapper
static void SEH_OnCharUpdate(void* animClassHuman) {
    __try {
        OnCharUpdate(animClassHuman);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // Silently catch — can't let crashes propagate into game code
    }
}

// Build inline hook trampoline:
// 1. push all registers
// 2. mov rcx, rbx  (pass AnimationClassHuman* as first arg)
// 3. call SEH_OnCharUpdate
// 4. pop all registers
// 5. execute original 14 bytes
// 6. jmp back to hookAddr + 14
static bool BuildInlineHook(uintptr_t hookAddr) {
    // Save original bytes
    __try {
        memcpy(s_originalBytes, reinterpret_cast<void*>(hookAddr), 14);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        spdlog::error("char_tracker: Failed to read original bytes at 0x{:X}", hookAddr);
        return false;
    }

    // Allocate executable memory for trampoline
    size_t trampolineSize = 256;
    s_trampolineAlloc = VirtualAlloc(nullptr, trampolineSize,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!s_trampolineAlloc) {
        spdlog::error("char_tracker: VirtualAlloc failed for trampoline");
        return false;
    }

    uint8_t* code = static_cast<uint8_t*>(s_trampolineAlloc);
    int off = 0;

    // Save all registers (same as research mod's saveRegisters)
    // push rax,rcx,rdx,rbx,rbp,rsi,rdi,r8-r15
    uint8_t saveRegs[] = {
        0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57,
        0x41,0x50, 0x41,0x51, 0x41,0x52, 0x41,0x53,
        0x41,0x54, 0x41,0x55, 0x41,0x56, 0x41,0x57
    };
    memcpy(code + off, saveRegs, sizeof(saveRegs));
    off += sizeof(saveRegs);

    // sub rsp, 0x28 (shadow space for __fastcall)
    code[off++] = 0x48; code[off++] = 0x83; code[off++] = 0xEC; code[off++] = 0x28;

    // mov rcx, rbx (AnimationClassHuman* is in RBX at this instruction)
    code[off++] = 0x48; code[off++] = 0x89; code[off++] = 0xD9;

    // call [rip+0] -> SEH_OnCharUpdate
    code[off++] = 0xFF; code[off++] = 0x15; code[off++] = 0x02;
    code[off++] = 0x00; code[off++] = 0x00; code[off++] = 0x00;
    code[off++] = 0xEB; code[off++] = 0x08; // jmp over ptr
    uintptr_t funcAddr = reinterpret_cast<uintptr_t>(&SEH_OnCharUpdate);
    memcpy(code + off, &funcAddr, 8);
    off += 8;

    // add rsp, 0x28
    code[off++] = 0x48; code[off++] = 0x83; code[off++] = 0xC4; code[off++] = 0x28;

    // Restore all registers
    uint8_t restoreRegs[] = {
        0x41,0x5F, 0x41,0x5E, 0x41,0x5D, 0x41,0x5C,
        0x41,0x5B, 0x41,0x5A, 0x41,0x59, 0x41,0x58,
        0x5F, 0x5E, 0x5D, 0x5B, 0x5A, 0x59, 0x58
    };
    memcpy(code + off, restoreRegs, sizeof(restoreRegs));
    off += sizeof(restoreRegs);

    // Execute original 14 bytes
    memcpy(code + off, s_originalBytes, 14);
    off += 14;

    // jmp back to hookAddr + 14
    code[off++] = 0xFF; code[off++] = 0x25;
    code[off++] = 0x00; code[off++] = 0x00; code[off++] = 0x00; code[off++] = 0x00;
    uintptr_t returnAddr = hookAddr + 14;
    memcpy(code + off, &returnAddr, 8);
    off += 8;

    // Now patch the original code to jump to our trampoline
    DWORD oldProtect;
    if (!VirtualProtect(reinterpret_cast<void*>(hookAddr), 14, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        spdlog::error("char_tracker: VirtualProtect failed for hook site");
        VirtualFree(s_trampolineAlloc, 0, MEM_RELEASE);
        s_trampolineAlloc = nullptr;
        return false;
    }

    // Write: jmp [rip+0]; dq trampolineAddr
    uint8_t* hookSite = reinterpret_cast<uint8_t*>(hookAddr);
    hookSite[0] = 0xFF; hookSite[1] = 0x25;
    hookSite[2] = 0x00; hookSite[3] = 0x00; hookSite[4] = 0x00; hookSite[5] = 0x00;
    uintptr_t trampolineAddr = reinterpret_cast<uintptr_t>(s_trampolineAlloc);
    memcpy(hookSite + 6, &trampolineAddr, 8);

    VirtualProtect(reinterpret_cast<void*>(hookAddr), 14, oldProtect, &oldProtect);

    spdlog::info("char_tracker: Inline hook installed at 0x{:X} -> trampoline 0x{:X}",
                 hookAddr, trampolineAddr);
    return true;
}

// ── Public API ──

const TrackedChar* FindByName(const std::string& name) {
    std::lock_guard lock(s_trackerMutex);
    for (auto& [key, tc] : s_trackedChars) {
        if (tc.name == name) return &tc;
    }
    return nullptr;
}

const TrackedChar* FindByPtr(void* characterPtr) {
    std::lock_guard lock(s_trackerMutex);
    auto it = s_trackedChars.find(characterPtr);
    return (it != s_trackedChars.end()) ? &it->second : nullptr;
}

void* GetLocalPlayerAnimClass() {
    return s_localPlayerAnimClass;
}

void* GetRemotePlayerAnimClass(const std::string& name) {
    auto* tc = FindByName(name);
    return tc ? tc->animClassPtr : nullptr;
}

void SetOnNewCharacter(std::function<void(const TrackedChar&)> callback) {
    std::lock_guard lock(s_trackerMutex);
    s_onNewChar = callback;
}

int GetTrackedCount() {
    std::lock_guard lock(s_trackerMutex);
    return static_cast<int>(s_trackedChars.size());
}

void DumpTrackedChars() {
    std::lock_guard lock(s_trackerMutex);
    uint64_t now = GetTickCount64();
    spdlog::info("char_tracker: {} tracked characters:", s_trackedChars.size());
    for (auto& [key, tc] : s_trackedChars) {
        float ageSec = (now - tc.lastSeenTick) / 1000.f;
        spdlog::info("  '{}' at 0x{:X} (animClass=0x{:X}), pos=({:.0f},{:.0f},{:.0f}), age={:.1f}s",
                     tc.name, reinterpret_cast<uintptr_t>(tc.characterPtr),
                     reinterpret_cast<uintptr_t>(tc.animClassPtr),
                     tc.position.x, tc.position.y, tc.position.z, ageSec);
    }
}

bool Install() {
    auto& funcs = Core::Get().GetGameFunctions();

    if (!funcs.CharAnimUpdate) {
        spdlog::warn("char_tracker: CharAnimUpdate address not resolved — trying fallback");
        // Fallback: derive from known function area if pattern scan failed
        // The charUpdateHook is at a MID-FUNCTION location, not a function start.
        // We might need to find it relative to other known addresses.
        return false;
    }

    uintptr_t hookAddr = reinterpret_cast<uintptr_t>(funcs.CharAnimUpdate);
    spdlog::info("char_tracker: Installing inline hook at 0x{:X}", hookAddr);

    return BuildInlineHook(hookAddr);
}

void Uninstall() {
    if (s_trampolineAlloc) {
        // Restore original bytes
        auto& funcs = Core::Get().GetGameFunctions();
        if (funcs.CharAnimUpdate) {
            uintptr_t hookAddr = reinterpret_cast<uintptr_t>(funcs.CharAnimUpdate);
            DWORD oldProtect;
            if (VirtualProtect(reinterpret_cast<void*>(hookAddr), 14, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                memcpy(reinterpret_cast<void*>(hookAddr), s_originalBytes, 14);
                VirtualProtect(reinterpret_cast<void*>(hookAddr), 14, oldProtect, &oldProtect);
            }
        }
        VirtualFree(s_trampolineAlloc, 0, MEM_RELEASE);
        s_trampolineAlloc = nullptr;
    }
}

} // namespace kmp::char_tracker_hooks
```

- [ ] **Step 3: Add to CMakeLists.txt and build**

- [ ] **Step 4: Commit**

```bash
git add KenshiMP.Core/hooks/char_tracker_hooks.h KenshiMP.Core/hooks/char_tracker_hooks.cpp KenshiMP.Core/CMakeLists.txt
git commit -m "feat: add character tracker via animation update inline hook"
```

### Task 4: Integrate New Hooks into Core Lifecycle

**Files:**
- Modify: `KenshiMP.Core/core.cpp` — Initialize new hooks, wire up spawn queue
- Modify: `KenshiMP.Core/core.h` — Add includes

- [ ] **Step 1: Add includes and Install calls in Core::Initialize()**

In the hook initialization section of `core.cpp` (after existing Install calls), add:

```cpp
#include "hooks/squad_spawn_hooks.h"
#include "hooks/char_tracker_hooks.h"

// In InitHooks() or wherever hooks are installed:
squad_spawn_hooks::Install();
char_tracker_hooks::Install();
```

- [ ] **Step 2: Wire squad spawn into the existing spawn pipeline**

In the spawn manager or wherever S2C_EntitySpawn is handled, when a spawn request comes in AND FactoryCreate/NPC-Hijack isn't available, route through `squad_spawn_hooks::QueueSquadSpawn()`.

- [ ] **Step 3: Add Uninstall calls in Core::Shutdown()**

```cpp
squad_spawn_hooks::Uninstall();
char_tracker_hooks::Uninstall();
```

- [ ] **Step 4: Build, launch game, verify hooks install in logs**

Expected log output:
```
squad_spawn_hooks: Installed successfully
char_tracker: Inline hook installed at 0xXXXXXX -> trampoline 0xXXXXXX
char_tracker: NEW character 'Beep' at 0xXXXXXX
char_tracker: NEW character 'Player 1' at 0xXXXXXX
```

- [ ] **Step 5: Commit**

```bash
git add KenshiMP.Core/core.cpp KenshiMP.Core/core.h
git commit -m "feat: integrate squad spawn and char tracker hooks into core lifecycle"
```

---

## Chunk 2: Combat Crash Fix + Lobby System

### Task 5: Fix Combat Attack Crash

**Files:**
- Modify: `KenshiMP.Core/hooks/combat_hooks.cpp` — Add SEH protection to attack initiation
- Modify: `KenshiMP.Core/net/packet_handler.cpp` — Harden combat packet handlers

The user reports crashes when clicking "attack unprovoked". This is likely the StartAttack hook or the combat intent packet handler accessing invalid memory.

- [ ] **Step 1: Read and audit combat_hooks.cpp for crash sources**

Read `KenshiMP.Core/hooks/combat_hooks.cpp` to identify where the attack-unprovoked flow crashes. The likely culprit is either:
1. Hook_StartAttack accessing invalid target/weapon pointers
2. The C2S_AttackIntent packet handler trying to resolve entities that don't exist on the server
3. Missing SEH protection around the combat hook trampoline

- [ ] **Step 2: Add SEH protection to all combat hook trampolines**

Wrap every combat hook's original-function call in SEH protection:

```cpp
static void __fastcall Hook_StartAttack(void* attacker, void* target, void* weapon) {
    // Validate pointers BEFORE any game memory access
    uintptr_t atkAddr = reinterpret_cast<uintptr_t>(attacker);
    uintptr_t tgtAddr = reinterpret_cast<uintptr_t>(target);
    if (atkAddr < 0x10000 || atkAddr > 0x00007FFFFFFFFFFF ||
        tgtAddr < 0x10000 || tgtAddr > 0x00007FFFFFFFFFFF) {
        // Invalid pointers — call original without our logic
        __try { s_origStartAttack(attacker, target, weapon); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
        return;
    }

    // Send combat intent to server ONLY if connected
    auto& core = Core::Get();
    if (core.IsConnected()) {
        __try {
            // ... entity lookup and packet send ...
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            spdlog::warn("combat_hooks: SEH caught in StartAttack network logic");
        }
    }

    // Call original with SEH
    __try {
        s_origStartAttack(attacker, target, weapon);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        spdlog::error("combat_hooks: StartAttack trampoline CRASHED");
    }
}
```

- [ ] **Step 3: Guard packet handler combat methods**

In `packet_handler.cpp`, wrap `HandleCombatHit`, `HandleCombatDeath`, `HandleCombatKO` handlers with null checks on game object pointers before calling native functions.

- [ ] **Step 4: Build, test attack in game (both connected and solo)**

Expected: No crash on "attack unprovoked" — combat intent is sent to server or silently handled.

- [ ] **Step 5: Commit**

```bash
git add KenshiMP.Core/hooks/combat_hooks.cpp KenshiMP.Core/net/packet_handler.cpp
git commit -m "fix: add SEH protection to combat hooks preventing attack-crash"
```

### Task 6: Create Lobby Manager

**Files:**
- Create: `KenshiMP.Core/game/lobby_manager.h`
- Create: `KenshiMP.Core/game/lobby_manager.cpp`
- Modify: `KenshiMP.Common/include/kmp/protocol.h` — Add lobby message types
- Modify: `KenshiMP.Server/server.cpp` — Add faction assignment logic

- [ ] **Step 1: Add lobby message types to protocol**

In `protocol.h`, add new message types:

```cpp
    // ── Lobby ──
    S2C_FactionAssignment = 0xD0,  // Server assigns faction string to client
    C2S_LobbyReady       = 0xD1,  // Client confirms ready with faction loaded
    S2C_LobbyStart       = 0xD2,  // Server tells all clients to start/load
```

- [ ] **Step 2: Create lobby_manager.h**

```cpp
// KenshiMP.Core/game/lobby_manager.h
#pragma once
#include "kmp/types.h"
#include <string>
#include <atomic>

namespace kmp {

class LobbyManager {
public:
    // Called when server assigns our faction string (e.g. "10-kenshi-online.mod")
    void OnFactionAssigned(const std::string& factionString, int playerSlot);

    // Apply the faction string to game memory (must be called BEFORE save load)
    bool ApplyFactionPatch();

    // Check if faction has been assigned
    bool HasFaction() const { return m_hasAssignment; }

    // Get assigned player slot (0-based)
    int GetPlayerSlot() const { return m_playerSlot; }

    // Get assigned faction string
    const std::string& GetFactionString() const { return m_factionString; }

private:
    std::string m_factionString;
    int m_playerSlot = -1;
    bool m_hasAssignment = false;

    // Find the faction string address in game memory
    uintptr_t FindFactionStringAddress();
};

} // namespace kmp
```

- [ ] **Step 3: Create lobby_manager.cpp**

```cpp
// KenshiMP.Core/game/lobby_manager.cpp
#include "lobby_manager.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <Windows.h>

namespace kmp {

void LobbyManager::OnFactionAssigned(const std::string& factionString, int playerSlot) {
    m_factionString = factionString;
    m_playerSlot = playerSlot;
    m_hasAssignment = true;
    spdlog::info("LobbyManager: Assigned faction '{}' slot {}", factionString, playerSlot);
}

uintptr_t LobbyManager::FindFactionStringAddress() {
    // The faction string is stored in .rdata at a hardcoded offset.
    // Research mod: GOG offset 0x16C2F68
    // On Steam, we search for known faction string patterns.
    uintptr_t moduleBase = Memory::GetModuleBase();

    // Strategy 1: Try known offsets
    uintptr_t candidates[] = {
        moduleBase + 0x16C2F68,  // GOG offset
    };

    for (auto addr : candidates) {
        __try {
            // Read 17 bytes and check if it looks like a faction ID string
            // Format: "NNN-something.ext" (e.g. "204-gamedata.base")
            char buf[20] = {};
            memcpy(buf, reinterpret_cast<void*>(addr), 17);
            // Check if it contains a dash and a dot (faction ID format)
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
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
    }

    // Strategy 2: Pattern search in .rdata for "204-gamedata.base" (default faction)
    // This is 17 bytes: "204-gamedata.base"
    const char* searchStr = "204-gamedata.base";
    size_t searchLen = 17;

    // Get .rdata section bounds from PE header
    auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(moduleBase);
    auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(moduleBase + dosHeader->e_lfanew);
    auto* sections = IMAGE_FIRST_SECTION(ntHeaders);

    for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
        if (strncmp(reinterpret_cast<const char*>(sections[i].Name), ".rdata", 6) == 0) {
            uintptr_t start = moduleBase + sections[i].VirtualAddress;
            uintptr_t end = start + sections[i].Misc.VirtualSize;

            for (uintptr_t addr = start; addr < end - searchLen; addr++) {
                __try {
                    if (memcmp(reinterpret_cast<void*>(addr), searchStr, searchLen) == 0) {
                        spdlog::info("LobbyManager: Found faction string via search at 0x{:X}", addr);
                        return addr;
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    break;
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

    // The faction string is in non-writable memory (.rdata)
    // Use VirtualProtect to make it writable, like the research mod does
    DWORD oldProtect;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), m_factionString.size() + 1,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) {
        spdlog::error("LobbyManager: VirtualProtect failed for faction string at 0x{:X}", addr);
        return false;
    }

    // Write the new faction string (must be exactly 17 chars, padded with nulls)
    char factionBuf[18] = {};
    size_t copyLen = std::min(m_factionString.size(), (size_t)17);
    memcpy(factionBuf, m_factionString.c_str(), copyLen);
    memcpy(reinterpret_cast<void*>(addr), factionBuf, 17);

    // Restore original protection
    VirtualProtect(reinterpret_cast<void*>(addr), m_factionString.size() + 1,
                   oldProtect, &oldProtect);

    spdlog::info("LobbyManager: Patched faction string at 0x{:X} to '{}'",
                 addr, m_factionString);
    return true;
}

} // namespace kmp
```

- [ ] **Step 4: Add S2C_FactionAssignment handler to packet_handler.cpp**

```cpp
case MessageType::S2C_FactionAssignment: {
    uint16_t strLen = 0;
    if (!reader.ReadU16(strLen) || strLen > 20) return;
    std::string factionStr(strLen, '\0');
    if (!reader.ReadRaw(factionStr.data(), strLen)) return;
    int32_t slot = 0;
    reader.ReadI32(slot);
    Core::Get().GetLobbyManager().OnFactionAssigned(factionStr, slot);
    // Apply immediately if we're still on main menu
    if (Core::Get().GetPhase() == ClientPhase::MainMenu) {
        Core::Get().GetLobbyManager().ApplyFactionPatch();
    }
    break;
}
```

- [ ] **Step 5: Add faction assignment to server handshake**

In `KenshiMP.Server/server.cpp` `HandleHandshake()`, after assigning player ID, send the faction string:

```cpp
// Assign faction based on player slot
// Player 1 → "10-kenshi-online.mod", Player 2 → "12-kenshi-online.mod", etc.
// (These must match the FCS IDs in kenshi-online.mod)
std::string factionStrings[] = {
    "10-kenshi-online",  // Slot 0 (Player 1)
    "12-kenshi-online",  // Slot 1 (Player 2)
    // Add more for 3-16 player support
};
int slot = player.id - 1;
if (slot >= 0 && slot < sizeof(factionStrings)/sizeof(factionStrings[0])) {
    PacketWriter factionPkt;
    factionPkt.WriteHeader(MessageType::S2C_FactionAssignment);
    factionPkt.WriteU16(static_cast<uint16_t>(factionStrings[slot].size()));
    factionPkt.WriteRaw(factionStrings[slot].data(), factionStrings[slot].size());
    factionPkt.WriteI32(slot);
    SendTo(player.id, factionPkt.Data(), factionPkt.Size(),
           KMP_CHANNEL_RELIABLE_ORDERED, ENET_PACKET_FLAG_RELIABLE);
}
```

- [ ] **Step 6: Build and verify compilation**

- [ ] **Step 7: Commit**

```bash
git add KenshiMP.Core/game/lobby_manager.h KenshiMP.Core/game/lobby_manager.cpp \
        KenshiMP.Common/include/kmp/protocol.h KenshiMP.Server/server.cpp \
        KenshiMP.Core/net/packet_handler.cpp
git commit -m "feat: add lobby manager with faction assignment and pre-load memory patching"
```

---

## Chunk 3: End-to-End Integration + Verification

### Task 7: Wire Everything Together for Two-Player Flow

**Files:**
- Modify: `KenshiMP.Core/core.cpp` — Add LobbyManager, wire spawn pipeline
- Modify: `KenshiMP.Core/core.h` — Add LobbyManager member

- [ ] **Step 1: Add LobbyManager to Core**

```cpp
// In core.h:
#include "game/lobby_manager.h"
// Add member:
LobbyManager m_lobbyManager;
// Add getter:
LobbyManager& GetLobbyManager() { return m_lobbyManager; }
```

- [ ] **Step 2: Apply faction patch before save load**

In `Core::OnLoadGame()` or wherever the save load is detected, call:
```cpp
if (m_lobbyManager.HasFaction()) {
    m_lobbyManager.ApplyFactionPatch();
}
```

- [ ] **Step 3: Use char_tracker to find remote players for position writes**

In the position update rendering code (sync_orchestrator or core.cpp where interpolated positions are applied), use `char_tracker_hooks::FindByName()` to locate the game object for remote players:

```cpp
// When applying interpolated position for remote entity:
auto* tracked = char_tracker_hooks::FindByName(remoteName);
if (tracked && tracked->animClassPtr) {
    // Write position through the AnimClass → CharMovement → position chain
    uintptr_t animClass = reinterpret_cast<uintptr_t>(tracked->animClassPtr);
    uintptr_t charMovement = 0;
    Memory::Read(animClass + 0xC0, charMovement);  // AnimClass → CharMovement
    if (charMovement) {
        uintptr_t posAddr = charMovement + 0x320 + 0x20;  // CharMovement → writable pos
        Memory::Write(posAddr, interpPos.x);
        Memory::Write(posAddr + 4, interpPos.y);
        Memory::Write(posAddr + 8, interpPos.z);
    }
}
```

- [ ] **Step 4: Build full solution**

- [ ] **Step 5: Commit**

```bash
git add KenshiMP.Core/core.cpp KenshiMP.Core/core.h
git commit -m "feat: wire lobby manager and char tracker into core lifecycle"
```

### Task 8: Integration Testing Checklist

This is not automated testing — it requires running the game. Each step verifies a piece of the pipeline.

- [ ] **Step 1: Solo launch test**
  - Start KenshiMP.Server.exe
  - Launch Kenshi with Core.dll loaded
  - Load a save
  - Check logs for:
    - `squad_spawn_hooks: Installed successfully` (or warning if address not found)
    - `char_tracker: NEW character 'X'` for each visible NPC
    - `char_tracker: X tracked characters` (should be >0)

- [ ] **Step 2: Connect test**
  - Type `/connect 127.0.0.1` in chat
  - Check logs for:
    - `LobbyManager: Assigned faction '10-kenshi-online' slot 0`
    - Handshake success

- [ ] **Step 3: Two-client test**
  - Run TestClient or second Kenshi instance
  - Connect second client to same server
  - Check logs for:
    - S2C_EntitySpawn received
    - SpawnManager attempts
    - Either NPC Hijack or squad bypass spawn succeeds
    - `char_tracker: NEW character 'Player 2'` appears

- [ ] **Step 4: Position sync test**
  - Move local character
  - Check server logs for position updates
  - Check remote client logs for S2C_PositionUpdate received
  - Verify remote character moves in game world

- [ ] **Step 5: Combat test**
  - Click "attack unprovoked" on an NPC
  - Verify NO crash
  - Check logs for C2S_AttackIntent sent, S2C_CombatHit received

- [ ] **Step 6: Document results**

Create a test results log noting what worked and what didn't, with specific error messages for any failures.

---

## Known Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Squad bypass pattern doesn't match Steam binary | Spawning falls back to NPC Hijack | Use SpawnCheck string anchor to find nearby code, manual RE |
| CharAnimUpdate is mid-function — inline hook may crash | Character tracking unavailable | Fall back to entity_hooks tracking (existing system) |
| Faction string offset differs on Steam | Lobby system can't assign factions | Pattern search for "204-gamedata.base" in .rdata |
| activePlatoon struct offsets differ on Steam | Squad bypass writes to wrong memory | Validate offsets by reading known values (check1 should be bool 0/1) |
| Combat hook trampoline has mov-rax-rsp issue | StartAttack crash persists | Use safe_hook.h wrappers or disable hook entirely during combat |

## Architecture Diagram

```
Server                                Client A                          Client B
  |                                       |                                |
  |<-- C2S_Handshake --------------------|                                |
  |--- S2C_FactionAssignment("10-ko") -->|                                |
  |--- S2C_HandshakeAck --------------->|                                |
  |                                       |-- ApplyFactionPatch() ------->|
  |                                       |   (patches .rdata faction)    |
  |<-- C2S_Handshake ---------------------------------------------------|
  |--- S2C_FactionAssignment("12-ko") --------------------------------->|
  |--- S2C_HandshakeAck ---------------------------------------------->|
  |                                       |                                |
  |--- S2C_EntitySpawn(B's char) ------->|                                |
  |                                       |-- QueueSpawn() ------------->  |
  |                                       |   squad_spawn or NPC hijack   |
  |                                       |-- char_tracker finds char --> |
  |                                       |                                |
  |<-- C2S_PositionUpdate(A's pos) ------|                                |
  |--- S2C_PositionUpdate(A) ------------------------------------------>|
  |                                       |                                |-- WritePosition()
  |                                       |                                |   (via AnimClass chain)
```
