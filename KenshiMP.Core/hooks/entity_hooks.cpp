#include "entity_hooks.h"
#include "save_hooks.h"
#include "ai_hooks.h"
#include "squad_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "../game/spawn_manager.h"
#include "../game/player_controller.h"
#include "../game/asset_facilitator.h"
#include "../sync/pipeline_state.h"
#include "kmp/hook_manager.h"
#include "kmp/protocol.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <chrono>
#include <cmath>
#include <unordered_map>

// Declared in core.cpp — updated here so VEH crash handler shows which create# crashed
namespace kmp { extern volatile int g_lastCharacterCreateNum; }

namespace kmp::entity_hooks {

// ── Function Types ──
// CharacterSpawn prologue: mov rax,rsp + 7 pushes + lea rbp,[rax-0x158] + sub rsp,0x220
// Confirmed 2-param: RCX=factory(this), RDX=GameData*
// RBP is derived from RAX, so ALL [rbp+XX] locals depend on RAX being correct.
// HookManager builds a custom caller stub that sets RAX = real RSP, then jumps
// to original+3 (past the original's `mov rax, rsp`). This bypasses MinHook's
// trampoline entirely for the "call original" path.
using CharacterCreateFn = void*(__fastcall*)(void* factory, void* templateData);
using CharacterDestroyFn = void(__fastcall*)(void* character);

// Store the ORIGINAL function addresses (NOT trampolines)
static uintptr_t s_createTargetAddr = 0;
static uintptr_t s_destroyTargetAddr = 0;

// s_origCreate: Points to the TRAMPOLINE WRAPPER built by MovRaxRsp fix.
// The wrapper restores RAX to the game caller's RSP (captured by the naked
// detour at hook entry), then jumps to trampoline+3 (past `mov rax, rsp`).
// This keeps RAX correct so `lea rbp, [rax-0x158]` computes the right frame
// pointer. The stack swap ensures push/pop slots are at correct offsets.
static CharacterCreateFn  s_origCreate  = nullptr;
static CharacterDestroyFn s_origDestroy = nullptr;

// s_rawTrampoline: MinHook's raw trampoline (starts with `mov rax, rsp`).
// Used for REENTRANT calls to avoid corrupting the MovRaxRsp wrapper's global
// data slots (captured_rsp, stub_rsp, saved_game_ret). When s_hookDepth > 0,
// we call through the raw trampoline which does NOT manipulate any global state.
static CharacterCreateFn  s_rawCreateTrampoline = nullptr;

// Whether CharacterDestroy hook is actually installed (may be skipped if wrong function found)
static bool s_destroyHookInstalled = false;

// ── Diagnostic Counters ──
static std::atomic<int> s_totalCreates{0};
static std::atomic<int> s_totalDestroys{0};

// ── Loading Burst Detection ──
static std::atomic<int>  s_burstCreateCount{0};
static std::atomic<bool> s_inBurst{false};
static auto s_lastCreateTime = std::chrono::steady_clock::now();
static constexpr int    BURST_THRESHOLD = 5;
static constexpr int    BURST_WINDOW_MS = 500;
static constexpr int    MIN_CREATES_BEFORE_READY = 30;

// ── Loading Completion Gate ──
// Unlike IsInLoadingBurst() which is reactive (needs 5+ creates to trigger),
// this flag is only set AFTER a burst has ended. Spawns are blocked until then.
// This prevents spawns at CharacterCreate #1-#10 before burst detection kicks in.
static std::atomic<bool> s_loadingComplete{false};
static auto s_burstEndTime = std::chrono::steady_clock::now();
static constexpr int BURST_SETTLE_SECONDS = 3;

static bool IsInLoadingBurst() {
    return s_inBurst.load();
}

static void TrackCreationRate() {
    // Once game is loaded, burst detection is irrelevant — NPCs spawning in new zones
    // are normal gameplay, not a loading burst. Don't re-trigger the loading guard.
    if (Core::Get().IsGameLoaded()) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastCreateTime);

    if (elapsed.count() < BURST_WINDOW_MS) {
        int count = s_burstCreateCount.fetch_add(1) + 1;
        if (count >= BURST_THRESHOLD && !s_inBurst.load()) {
            s_inBurst.store(true);
            s_loadingComplete.store(false); // Block spawns until burst finishes
            // Signal Core that game is actually loading (enables Overlay polling)
            Core::Get().SetLoading(true);
            // Notify LoadingOrchestrator + PipelineOrchestrator
            Core::Get().GetLoadingOrch().OnBurstDetected(count);
            Core::Get().GetPipelineOrch().RecordEvent(
                PipelineEventType::BurstDetected, 1, 0, count,
                "Burst: " + std::to_string(count) + " creates in " + std::to_string(elapsed.count()) + "ms");
            spdlog::info("entity_hooks: BURST DETECTED ({} creates in {}ms) — loading guard ON, spawns BLOCKED",
                         count, elapsed.count());
        }
    } else {
        s_burstCreateCount.store(1);
        s_lastCreateTime = now;

        if (s_inBurst.load()) {
            s_inBurst.store(false);
            s_burstEndTime = std::chrono::steady_clock::now();

            // Loading complete once a burst has ended with enough total creates
            if (!s_loadingComplete.load() && s_totalCreates.load() >= MIN_CREATES_BEFORE_READY) {
                s_loadingComplete.store(true);
                spdlog::info("entity_hooks: LOADING COMPLETE — spawns now allowed after {}s settle (total creates={})",
                             BURST_SETTLE_SECONDS, s_totalCreates.load());
            }

            // Notify LoadingOrchestrator + PipelineOrchestrator
            Core::Get().GetLoadingOrch().OnBurstEnded(s_totalCreates.load());
            Core::Get().GetPipelineOrch().RecordEvent(
                PipelineEventType::BurstEnded, 0, 0, s_totalCreates.load(),
                "Burst ended, total=" + std::to_string(s_totalCreates.load()));

            spdlog::info("entity_hooks: BURST ENDED — total creates={}, destroys={}, loadingComplete={}",
                         s_totalCreates.load(), s_totalDestroys.load(), s_loadingComplete.load());
        }
    }
}

// ── Position offset in request struct ──
// Detected by scanning the pre-call struct for float values matching the
// character's actual spawn position. Once found, we can write desired
// positions into the struct before replay to spawn at the right location.
static int s_positionOffsetInStruct = -1;  // -1 = not yet detected
static int s_positionDetectAttempts = 0;

// ── Direct spawn bypass ──
// When SpawnManager::SpawnCharacterDirect() calls the factory from GameFrameUpdate,
// the call triggers Hook_CharacterCreate (because the function is hooked).
// This flag tells the hook to skip all spawn/registration logic and just pass through.
static std::atomic<bool> s_directSpawnBypass{false};

// ── In-place replay tracking ──
// Tracks successful in-place replays so game_tick_hooks can avoid competing.
static std::atomic<int> s_inPlaceSpawnCount{0};
static auto s_lastInPlaceSpawnTime = std::chrono::steady_clock::now();

// ── Per-player spawn cap ──
// Prevents any single remote player from overwhelming the engine with spawns.
// Without this, a host with 40+ NPCs registered would crash the joiner.
static constexpr int MAX_SPAWNS_PER_PLAYER = 4;
static std::unordered_map<PlayerID, int> s_spawnsPerPlayer;

// ── Loading-phase character cache ──
// Characters seen during game loading (before connect). Used as a fallback
// by SendExistingEntitiesToServer when CharacterIterator can't find characters
// (PlayerBase/GameWorld not resolved on Steam).
static std::vector<CachedCharacter> s_loadingCharacters;
static uintptr_t s_capturedFaction = 0;
static constexpr int MAX_CACHED_CHARACTERS = 200;

// ── SEH-safe memcpy for request struct capture ──
// templateData may point to a stack-allocated struct near a page boundary.
// Reading 1024 bytes past the struct's actual size could fault on uncommitted memory.
static bool SEH_MemcpySafe(void* dst, const void* src, size_t size) {
    __try {
        memcpy(dst, src, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ── SEH-safe character memory reads ──
// CharacterAccessor reads follow pointers in game memory. During loading bursts,
// characters may be partially initialized or freed. These wrappers prevent AVs
// from propagating up and crashing the game (which triggers exit(1) → atexit → crash).

struct SEH_CharData {
    Vec3 position;
    Quat rotation;
    uintptr_t factionPtr;
    uint32_t factionId;
    bool valid;
};

static SEH_CharData SEH_ReadCharacterData(void* character) {
    SEH_CharData result{};
    result.valid = false;
    __try {
        uintptr_t charPtr = reinterpret_cast<uintptr_t>(character);
        if (charPtr < 0x10000 || charPtr > 0x00007FFFFFFFFFFF) return result;

        // Position at +0x48
        Memory::Read(charPtr + 0x48, result.position.x);
        Memory::Read(charPtr + 0x4C, result.position.y);
        Memory::Read(charPtr + 0x50, result.position.z);

        // Rotation at +0x58
        Memory::Read(charPtr + 0x58, result.rotation.w);
        Memory::Read(charPtr + 0x5C, result.rotation.x);
        Memory::Read(charPtr + 0x60, result.rotation.y);
        Memory::Read(charPtr + 0x64, result.rotation.z);

        // Faction at +0x10 (pointer)
        Memory::Read(charPtr + 0x10, result.factionPtr);
        if (result.factionPtr > 0x10000 && result.factionPtr < 0x00007FFFFFFFFFFF) {
            Memory::Read(result.factionPtr + 0x08, result.factionId);
        }

        result.valid = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_readCrash = 0;
        if (++s_readCrash <= 5) {
            OutputDebugStringA("KMP: SEH_ReadCharacterData CRASHED — character memory invalid\n");
        }
    }
    return result;
}

static bool SEH_FeedSpawnManager(void* factory, void* templateData, void* character) {
    __try {
        Core::Get().GetSpawnManager().OnGameCharacterCreated(factory, templateData, character);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_feedCrash = 0;
        if (++s_feedCrash <= 5) {
            OutputDebugStringA("KMP: SEH_FeedSpawnManager CRASHED — template scan hit bad memory\n");
        }
        return false;
    }
}

// ── SEH-protected post-spawn setup ──
// After in-place replay creates a character, we need to:
//   1. Link game object to EntityRegistry (SAFE — our own code, done FIRST)
//   2. Teleport it to the remote player's position (WritePosition follows pointer chains)
//   3. Feed SpawnManager for template capture (follows game pointers)
//   4. Set name + faction via PlayerController (WriteName/WriteFaction follow pointers)
//
// Steps 2-4 follow game memory pointers that may be invalid for freshly-spawned
// characters (partially initialized, physics not ready, etc.).
// An AV here propagates up → Kenshi exception handler → exit(1) → atexit crash.
//
// CRITICAL ORDERING: SetGameObject MUST happen BEFORE the SEH-protected block.
// If WritePosition AVs, we still need the game object linked so that
// ApplyRemotePositions() can write interpolated positions on subsequent frames.
// Without this, a single AV permanently freezes the character (gameObject=nullptr).
//
// This function MUST NOT have C++ objects with non-trivial destructors on the stack
// (MSVC restriction: __try can't coexist with unwinding). CharacterAccessor and Vec3
// are both trivially destructible so they're safe.
static bool SEH_PostSpawnSetup(void* newChar, EntityID netId, PlayerID owner,
                                Vec3 spawnPos, void* factory, void* templateData) {
    // ── SAFE PHASE (no SEH needed) ──
    // Link game object to EntityRegistry FIRST. This is our own code (just sets a
    // pointer in a map) and cannot AV. Even if the dangerous phase below crashes,
    // the entity is linked and ApplyRemotePositions() will move it next frame.
    Core::Get().GetEntityRegistry().SetGameObject(netId, newChar);
    Core::Get().GetEntityRegistry().UpdatePosition(netId, spawnPos);

    // ── DANGEROUS PHASE (SEH-protected) ──
    // These all follow chains of game memory pointers that may be invalid.
    __try {
        // 1. Teleport to desired position
        game::CharacterAccessor accessor(newChar);
        if (spawnPos.x != 0.f || spawnPos.y != 0.f || spawnPos.z != 0.f) {
            accessor.WritePosition(spawnPos);
        }

        // 2. Feed SpawnManager for template capture
        Core::Get().GetSpawnManager().OnGameCharacterCreated(factory, templateData, newChar);

        // 3. Set up character name + faction
        Core::Get().GetPlayerController().OnRemoteCharacterSpawned(netId, newChar, owner);

        // 4. Mark as remote-controlled for AI decision override
        ai_hooks::MarkRemoteControlled(newChar);

        // 5. Inject into local player's squad (engine exploit)
        // This makes the character selectable, orderable, visible in squad panel.
        squad_hooks::AddCharacterToLocalSquad(newChar);

        // 6. Set isPlayerControlled flag (engine exploit — Phase 4)
        // Makes the engine treat this character as player-owned, enabling
        // selection via click, order giving, and full UI interaction.
        game::WritePlayerControlled(reinterpret_cast<uintptr_t>(newChar), true);

        // 7. Schedule deferred AnimClass probe (Phase 3)
        game::ScheduleDeferredAnimClassProbe(reinterpret_cast<uintptr_t>(newChar));

        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_postSpawnCrash = 0;
        if (++s_postSpawnCrash <= 10) {
            char buf[256];
            sprintf_s(buf, "KMP: SEH_PostSpawnSetup CRASHED for entity %u "
                      "(AV caught — gameObject linked, will receive position updates)\n", netId);
            OutputDebugStringA(buf);
        }
        return false;
    }
}

void SetDirectSpawnBypass(bool bypass) {
    s_directSpawnBypass.store(bypass, std::memory_order_release);
}

// Hook suspension is NO LONGER NEEDED. HookManager builds a custom caller stub
// for mov-rax-rsp functions that bypasses MinHook's trampoline entirely.
// The hook stays active for the lifetime of the session, enabling real-time
// entity registration and in-place spawn replay for multiplayer.

// ── SEH wrapper for calling the original CharacterCreate ──
// s_origCreate now points to the MovRaxRsp trampoline wrapper, which:
//   1. Reads the captured RSP (saved by naked detour at hook entry)
//   2. Swaps to the game caller's stack
//   3. Sets RAX = captured RSP (what the function expects)
//   4. Pushes a return address and JMPs to trampoline+3
//   5. Original function executes with correct RAX/RBP/RSP
//   6. Returns to wrapper, which restores our stack and returns here
static void* SEH_CallOriginalCreate(CharacterCreateFn trampoline, void* factory, void* templateData) {
    __try {
        return trampoline(factory, templateData);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_trampolineCrash = 0;
        if (++s_trampolineCrash <= 5) {
            OutputDebugStringA("KMP: CharacterCreate trampoline CRASHED (SEH caught)\n");
        }
        return nullptr;
    }
}

// ── Request struct handling ──
// The factory's second parameter is a STACK-ALLOCATED REQUEST STRUCT, not a GameData*.
// The struct contains internal pointers relative to the caller's stack frame.
// To spawn remote characters, we RESTORE the pre-call struct to the SAME stack address
// and replay the factory call. This keeps all internal pointers valid.
static constexpr size_t REQUEST_STRUCT_SIZE = 1024; // Capture enough for self-ref at +0x230
static uint8_t s_preCallStruct[REQUEST_STRUCT_SIZE] = {};
static bool s_havePreCallData = false;
static void* s_savedFactory = nullptr;

// ── SEH helpers for lightweight loading path (no C++ objects allowed in __try) ──

// Captures pre-call struct + feeds SpawnManager. Returns true if successful.
static bool SEH_CaptureFactoryData(void* factory, void* templateData, void* character) {
    __try {
        memcpy(s_preCallStruct, templateData, REQUEST_STRUCT_SIZE);
        s_havePreCallData = true;
        Core::Get().GetSpawnManager().SetPreCallData(
            s_preCallStruct, REQUEST_STRUCT_SIZE,
            reinterpret_cast<uintptr_t>(templateData));
        Core::Get().GetSpawnManager().SetSavedRequestStruct(
            s_preCallStruct, REQUEST_STRUCT_SIZE);
        Core::Get().GetSpawnManager().OnGameCharacterCreated(
            factory, templateData, character);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Reads faction pointer from a character. Returns 0 on failure.
static uintptr_t SEH_ReadFactionPtr(void* character) {
    __try {
        uintptr_t charPtr = reinterpret_cast<uintptr_t>(character);
        uintptr_t fPtr = 0;
        Memory::Read(charPtr + 0x10, fPtr);
        if (fPtr > 0x10000 && fPtr < 0x00007FFFFFFFFFFF)
            return fPtr;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Reads position from a character into out_x/y/z. Returns true on success.
static bool SEH_ReadCharPos(void* character, float& out_x, float& out_y, float& out_z) {
    __try {
        uintptr_t charPtr = reinterpret_cast<uintptr_t>(character);
        Memory::Read(charPtr + 0x48, out_x);
        Memory::Read(charPtr + 0x4C, out_y);
        Memory::Read(charPtr + 0x50, out_z);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out_x = out_y = out_z = 0;
        return false;
    }
}

// Reads faction ID from a faction pointer. Returns 0 on failure.
static uint32_t SEH_ReadFactionId(uintptr_t factionPtr) {
    __try {
        uint32_t fId = 0;
        Memory::Read(factionPtr + 0x08, fId);
        return fId;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Triggers OnGameLoaded(). SEH-protected because it does a LOT of work.
static bool SEH_TriggerGameLoaded() {
    __try {
        Core::Get().OnGameLoaded();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: OnGameLoaded() crashed from lightweight path\n");
        return false;
    }
}

// ── Hooks ──

// SEH wrapper for in-place spawn replay via trampoline (no C++ objects allowed).
// Uses a LOCAL buffer for post-call state to avoid corruption when multiple
// replays happen in the same Hook_CharacterCreate call (MAX_REPLAYS_PER_CALL=1).
static void* SEH_ReplayFactory(CharacterCreateFn trampoline, void* factory, void* reqStruct,
                                const uint8_t* preCallData, size_t structSize) {
    __try {
        // 1. Save post-call state to LOCAL buffer (not global — avoids multi-replay corruption)
        uint8_t postCallBuffer[REQUEST_STRUCT_SIZE];
        memcpy(postCallBuffer, reqStruct, structSize);

        // 2. Restore pre-call data to the ORIGINAL stack address
        memcpy(reqStruct, preCallData, structSize);

        // 3. Call factory via trampoline
        void* result = trampoline(factory, reqStruct);

        // 4. Restore post-call state so the game caller sees expected data
        memcpy(reqStruct, postCallBuffer, structSize);

        return result;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static void* __fastcall Hook_CharacterCreate(void* factory, void* templateData) {
    // ── Re-entrancy guard (no C++ destructors — manual increment/decrement) ──
    static thread_local int s_hookDepth = 0;
    if (s_hookDepth > 0) {
        if (s_rawCreateTrampoline)
            return s_rawCreateTrampoline(factory, templateData);
        return SEH_CallOriginalCreate(s_origCreate, factory, templateData);
    }
    s_hookDepth++;

    // Direct spawn bypass (SpawnManager calling factory — skip all logic)
    if (s_directSpawnBypass.load(std::memory_order_acquire)) {
        void* r = SEH_CallOriginalCreate(s_origCreate, factory, templateData);
        s_hookDepth--;
        return r;
    }

    int createNum = s_totalCreates.fetch_add(1) + 1;
    g_lastCharacterCreateNum = createNum;

    // One-time factory pointer save (no heap allocation)
    if (!s_savedFactory) s_savedFactory = factory;

    // ═══════════════════════════════════════════════════════════════════════
    //  LIGHTWEIGHT LOADING PATH — when NOT connected to a server
    //  The MovRaxRsp naked detour + heavy hook body (spdlog, vector, memcpy
    //  on every call) causes cumulative heap corruption when firing 130+ times
    //  during game loading. This path does ABSOLUTE MINIMUM work:
    //    - Calls 1-2: capture pre-call struct + feed SpawnManager (one-time)
    //    - Calls 3+: pure passthrough (just call original, zero extra work)
    //    - Call MIN_CREATES_BEFORE_READY: trigger OnGameLoaded (once)
    //  NO spdlog, NO vector push_back, NO 1KB stack alloc on every call.
    // ═══════════════════════════════════════════════════════════════════════
    if (!Core::Get().IsConnected()) {
        void* character = nullptr;

        // First 2 calls only: capture factory data for SpawnManager
        if (createNum <= 2 && templateData && !s_havePreCallData) {
            character = SEH_CallOriginalCreate(s_origCreate, factory, templateData);
            SEH_CaptureFactoryData(factory, templateData, character);
        } else {
            // Pure passthrough — zero extra work
            character = SEH_CallOriginalCreate(s_origCreate, factory, templateData);
        }

        // Cache ALL loading characters (cheap: just pointer + faction + position)
        // SendExistingEntitiesToServer needs this as fallback when CharacterIterator
        // fails on Steam (PlayerBase/GameWorld not resolved).
        // Only operations: 3 Memory::Read + 1 vector push_back — no spdlog, no memcpy.
        if (character && s_loadingCharacters.size() < MAX_CACHED_CHARACTERS) {
            uintptr_t fPtr = SEH_ReadFactionPtr(character);
            // Validate faction pointer is in user-mode range (below 0x7FFFFFFFFFFF)
            if (fPtr != 0 && fPtr > 0x10000 && fPtr < 0x00007FFFFFFFFFFF) {
                if (s_capturedFaction == 0) {
                    s_capturedFaction = fPtr;
                }
                float cx = 0, cy = 0, cz = 0;
                SEH_ReadCharPos(character, cx, cy, cz);
                uint32_t fId = SEH_ReadFactionId(fPtr);
                CachedCharacter cc;
                cc.gameObj = character;
                cc.factionPtr = fPtr;
                cc.factionId = fId;
                cc.x = cx; cc.y = cy; cc.z = cz;
                s_loadingCharacters.push_back(cc);
            }
        }

        // Trigger OnGameLoaded exactly once at the threshold
        if (createNum == MIN_CREATES_BEFORE_READY) {
            SEH_TriggerGameLoaded();
        }

        s_hookDepth--;
        return character;
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  CONNECTED PATH — multiplayer hook body
    //  Runs after connection when CharacterCreate is re-enabled.
    //
    //  ZONE-LOAD GUARD: During zone loads, 90+ characters spawn in one frame.
    //  Running the full hook body (1KB memcpy, spdlog, Memory::Read, vector ops,
    //  SpawnManager feed) on each call through the MovRaxRsp naked detour causes
    //  cumulative heap corruption. Detect rapid-fire creates and use lightweight
    //  passthrough for all but the first few.
    // ═══════════════════════════════════════════════════════════════════════

    auto& coreRef = Core::Get();

    // Debug: log ALL connected creates for first 100
    static int s_connectedCreateNum = 0;
    s_connectedCreateNum++;
    if (s_connectedCreateNum <= 100 || s_connectedCreateNum % 50 == 0) {
        spdlog::info("entity_hooks: CONNECTED CharacterCreate #{} factory=0x{:X} template=0x{:X}",
                     s_connectedCreateNum,
                     reinterpret_cast<uintptr_t>(factory),
                     reinterpret_cast<uintptr_t>(templateData));
    }

    // Rapid-fire detection: if >5 creates in the same millisecond, go lightweight
    static auto s_connectedBurstStart = std::chrono::steady_clock::now();
    static int s_connectedBurstCount = 0;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_connectedBurstStart);
    if (elapsed.count() < 100) {
        s_connectedBurstCount++;
    } else {
        s_connectedBurstStart = now;
        s_connectedBurstCount = 1;
    }

    // During zone-load burst (>5 creates in 100ms): pure passthrough
    if (s_connectedBurstCount > 5) {
        if (s_connectedBurstCount <= 10 || s_connectedBurstCount % 50 == 0) {
            spdlog::debug("entity_hooks: BURST passthrough #{}", s_connectedBurstCount);
        }
        void* character = SEH_CallOriginalCreate(s_origCreate, factory, templateData);
        s_hookDepth--;
        return character;
    }

    // Pre-call struct capture (needed for in-place spawn replay)
    uint8_t localPreCall[REQUEST_STRUCT_SIZE];
    bool haveLocalPreCall = false;
    if (templateData) {
        memset(localPreCall, 0, REQUEST_STRUCT_SIZE);
        if (SEH_MemcpySafe(localPreCall, templateData, REQUEST_STRUCT_SIZE)) {
            haveLocalPreCall = true;
        }
        if (!s_havePreCallData) {
            SEH_MemcpySafe(s_preCallStruct, templateData, REQUEST_STRUCT_SIZE);
            s_havePreCallData = true;
            coreRef.GetSpawnManager().SetPreCallData(
                s_preCallStruct, REQUEST_STRUCT_SIZE,
                reinterpret_cast<uintptr_t>(templateData));
            coreRef.GetSpawnManager().SetSavedRequestStruct(
                s_preCallStruct, REQUEST_STRUCT_SIZE);
        }
    }

    // ═══ CALL ORIGINAL FUNCTION ═══
    if (s_connectedCreateNum <= 20) {
        spdlog::info("entity_hooks: Connected create #{} — calling original function", s_connectedCreateNum);
    }
    void* character = SEH_CallOriginalCreate(s_origCreate, factory, templateData);
    if (s_connectedCreateNum <= 20) {
        spdlog::info("entity_hooks: Connected create #{} — original returned 0x{:X}",
                     s_connectedCreateNum, reinterpret_cast<uintptr_t>(character));
    }

    if (character && haveLocalPreCall) {
        // Position offset detection
        if (s_positionOffsetInStruct == -1 && s_positionDetectAttempts < 10) {
            s_positionDetectAttempts++;
            SEH_CharData charData = SEH_ReadCharacterData(character);
            float cx = charData.position.x, cy = charData.position.y, cz = charData.position.z;
            if (cx != 0.f || cy != 0.f || cz != 0.f) {
                for (int off = 0; off < (int)REQUEST_STRUCT_SIZE - 12; off += 4) {
                    float sx, sy, sz;
                    memcpy(&sx, &localPreCall[off], 4);
                    memcpy(&sy, &localPreCall[off + 4], 4);
                    memcpy(&sz, &localPreCall[off + 8], 4);
                    if (fabsf(sx - cx) < 2.0f && fabsf(sy - cy) < 2.0f && fabsf(sz - cz) < 2.0f) {
                        s_positionOffsetInStruct = off;
                        spdlog::info("entity_hooks: POSITION OFFSET at struct+0x{:X}", off);
                        break;
                    }
                }
            }
        }

        // In-place spawn replay
        if (templateData && coreRef.IsGameLoaded()) {
            bool canSpawn = AssetFacilitator::Get().CanSpawn();
            if (canSpawn) {
                auto& spawnMgr = coreRef.GetSpawnManager();
                SpawnRequest spawnReq;
                if (spawnMgr.PopNextSpawn(spawnReq)) {
                    int& playerSpawns = s_spawnsPerPlayer[spawnReq.owner];
                    if (playerSpawns < MAX_SPAWNS_PER_PLAYER) {
                        spdlog::info("entity_hooks: IN-PLACE SPAWN for entity {} owner={}",
                                     spawnReq.netId, spawnReq.owner);
                        void* newChar = SEH_ReplayFactory(
                            s_origCreate, factory, templateData,
                            localPreCall, REQUEST_STRUCT_SIZE);
                        if (newChar) {
                            Vec3 spawnPos = spawnReq.position;
                            SEH_PostSpawnSetup(newChar, spawnReq.netId, spawnReq.owner,
                                               spawnPos, factory, templateData);
                            s_inPlaceSpawnCount.fetch_add(1);
                            s_lastInPlaceSpawnTime = std::chrono::steady_clock::now();
                            playerSpawns++;
                        } else {
                            spawnReq.retryCount++;
                            if (spawnReq.retryCount < MAX_SPAWN_RETRIES)
                                spawnMgr.RequeueSpawn(spawnReq);
                        }
                    }
                }
            }
        }
    }

    if (!character) {
        s_hookDepth--;
        return nullptr;
    }

    // AnimClass probe (first 5 only)
    {
        static int s_earlyProbeCount = 0;
        if (s_earlyProbeCount < 5) {
            game::ScheduleDeferredAnimClassProbe(reinterpret_cast<uintptr_t>(character));
            s_earlyProbeCount++;
        }
    }

    // Feed SpawnManager (skip during burst)
    SEH_FeedSpawnManager(factory, templateData, character);

    // Register player faction characters in EntityRegistry
    if (coreRef.IsGameLoaded()) {
        SEH_CharData charData = SEH_ReadCharacterData(character);
        if (s_connectedCreateNum <= 20) {
            spdlog::info("entity_hooks: Connected #{} charData valid={} pos=({:.1f},{:.1f},{:.1f}) faction=0x{:X}",
                         s_connectedCreateNum, charData.valid,
                         charData.position.x, charData.position.y, charData.position.z,
                         charData.factionPtr);
        }
        if (!charData.valid || (charData.position.x == 0.f && charData.position.y == 0.f && charData.position.z == 0.f)) {
            s_hookDepth--;
            return character;
        }

        Vec3 pos = charData.position;
        uintptr_t playerFaction = coreRef.GetPlayerController().GetLocalFactionPtr();

        if (playerFaction == 0) {
            // Validate faction pointer is in user-mode range (0x10000..0x7FFFFFFFFFFF)
            if (charData.factionPtr > 0x10000 && charData.factionPtr < 0x00007FFFFFFFFFFF) {
                spdlog::info("entity_hooks: FACTION BOOTSTRAP — faction 0x{:X}", charData.factionPtr);
                const_cast<PlayerController&>(coreRef.GetPlayerController())
                    .SetLocalFactionPtr(charData.factionPtr);
                playerFaction = charData.factionPtr;
                coreRef.RequestEntityRescan();
            } else {
                if (s_connectedCreateNum <= 20) {
                    spdlog::debug("entity_hooks: Connected #{} — invalid faction 0x{:X}, skipping",
                                  s_connectedCreateNum, charData.factionPtr);
                }
                s_hookDepth--;
                return character;
            }
        }

        if (charData.factionPtr != playerFaction) {
            if (s_connectedCreateNum <= 20) {
                spdlog::debug("entity_hooks: Connected #{} — faction mismatch (0x{:X} vs 0x{:X}), skipping",
                              s_connectedCreateNum, charData.factionPtr, playerFaction);
            }
            s_hookDepth--;
            return character;
        }

        PlayerID owner = coreRef.GetLocalPlayerId();
        EntityID netId = coreRef.GetEntityRegistry().Register(
            character, EntityType::NPC, owner);
        coreRef.GetEntityRegistry().UpdatePosition(netId, pos);

        {
            uint32_t compQuat = charData.rotation.Compress();
            uint32_t templateId = 0;
            std::string templateName;
            if (templateData) {
                Memory::Read(reinterpret_cast<uintptr_t>(templateData) + 0x08, templateId);
                templateName = SpawnManager::ReadKenshiString(
                    reinterpret_cast<uintptr_t>(templateData) + 0x28);
            }

            PacketWriter writer;
            writer.WriteHeader(MessageType::C2S_EntitySpawnReq);
            writer.WriteU32(netId);
            writer.WriteU8(static_cast<uint8_t>(EntityType::NPC));
            writer.WriteU32(owner);
            writer.WriteU32(templateId);
            writer.WriteF32(pos.x);
            writer.WriteF32(pos.y);
            writer.WriteF32(pos.z);
            writer.WriteU32(compQuat);
            writer.WriteU32(charData.factionId);
            uint16_t nameLen = static_cast<uint16_t>(
                std::min<size_t>(templateName.size(), 255));
            writer.WriteU16(nameLen);
            if (nameLen > 0)
                writer.WriteRaw(templateName.data(), nameLen);

            coreRef.GetClient().SendReliable(writer.Data(), writer.Size());
        }
    }

    s_hookDepth--;
    return character;
}

static void __fastcall Hook_CharacterDestroy(void* character) {
    int destroyNum = s_totalDestroys.fetch_add(1) + 1;

    // Validate pointer before doing anything
    uintptr_t charAddr = reinterpret_cast<uintptr_t>(character);
    if (charAddr < 0x10000 || charAddr > 0x00007FFFFFFFFFFF) {
        s_origDestroy(character);
        return;
    }

    auto& core = Core::Get();
    if (core.IsConnected() && !IsInLoadingBurst()) {
        EntityID netId = core.GetEntityRegistry().GetNetId(character);
        if (netId != INVALID_ENTITY) {
            auto* info = core.GetEntityRegistry().GetInfo(netId);
            bool isOurs = info && info->ownerPlayerId == core.GetLocalPlayerId();

            if (isOurs) {
                PacketWriter writer;
                writer.WriteHeader(MessageType::C2S_EntityDespawnReq);
                writer.WriteU32(netId);
                writer.WriteU8(0);
                core.GetClient().SendReliable(writer.Data(), writer.Size());
            }

            core.GetEntityRegistry().Unregister(netId);
        }
    }

    // CharacterDestroy prologue: 48 89 5C 24 08 (saves RBX to shadow space)
    // This does NOT start with mov rax,rsp — standard trampoline works.
    s_origDestroy(character);
}

// ── Install/Uninstall ──

bool Install() {
    auto& core = Core::Get();
    auto& hookMgr = HookManager::Get();
    auto& funcs = core.GetGameFunctions();

    bool success = true;

    if (funcs.CharacterSpawn) {
        s_createTargetAddr = reinterpret_cast<uintptr_t>(funcs.CharacterSpawn);

        char buf[128];
        sprintf_s(buf, "KMP: entity_hooks — hooking CharacterCreate at 0x%llX\n",
                  (unsigned long long)s_createTargetAddr);
        OutputDebugStringA(buf);

        if (!hookMgr.InstallAt("CharacterCreate",
                               s_createTargetAddr,
                               &Hook_CharacterCreate, &s_origCreate)) {
            spdlog::error("entity_hooks: Failed to hook CharacterCreate");
            OutputDebugStringA("KMP: entity_hooks — InstallAt FAILED\n");
            success = false;
        } else {
            // Get raw trampoline for reentrant calls (avoids MovRaxRsp wrapper corruption)
            void* rawTramp = hookMgr.GetRawTrampoline("CharacterCreate");
            if (rawTramp) {
                s_rawCreateTrampoline = reinterpret_cast<CharacterCreateFn>(rawTramp);
                spdlog::info("entity_hooks: Raw trampoline at 0x{:X} (for reentrant calls)",
                             reinterpret_cast<uintptr_t>(rawTramp));
            } else {
                spdlog::warn("entity_hooks: No raw trampoline — reentrant calls will use wrapper (crash risk)");
            }

            core.GetSpawnManager().SetOrigProcess(
                reinterpret_cast<FactoryProcessFn>(s_createTargetAddr));
            spdlog::info("entity_hooks: CharacterCreate hooked (MovRaxRsp fix — always active)");
            OutputDebugStringA("KMP: entity_hooks — CharacterCreate hooked OK (MovRaxRsp fix)\n");
        }
    }

    // CharacterDestroy hook NOT installed — despawn is handled by network events
    // (S2C_EntityDespawn → packet_handler → EntityRegistry::Unregister).
    // The resolved pattern was NodeList::destroyNodesByBuilding (wrong function).
    s_destroyHookInstalled = false;

    spdlog::info("entity_hooks: Installed (create={}, destroy={})",
                 funcs.CharacterSpawn != nullptr, s_destroyHookInstalled);
    return success;
}

void Uninstall() {
    HookManager::Get().Remove("CharacterCreate");
    if (s_destroyHookInstalled) {
        HookManager::Get().Remove("CharacterDestroy");
    }
}

void ResumeForNetwork() {
    // Reset per-player spawn caps for new connection
    s_spawnsPerPlayer.clear();

    // KEEP loading cache — SendExistingEntitiesToServer needs it as fallback
    // when CharacterIterator fails on Steam. The cached void* pointers are still
    // valid (same game session, characters not despawned). SEH protects reads.
    // s_capturedFaction also kept — it's the faction from loading, still valid.
    spdlog::info("entity_hooks: ResumeForNetwork — keeping loading cache ({} chars, faction=0x{:X})",
                 s_loadingCharacters.size(), s_capturedFaction);

    // CharacterCreate hook stays DISABLED after loading.
    // Zone-load bursts (100+ calls in <1s) through the MovRaxRsp naked detour
    // cause cumulative heap corruption → delayed crash ~5s later.
    // The loading cache already captured our squad characters.
    // In-place spawn replay is not needed — HandleSpawnQueue uses SpawnCharacterDirect.
    spdlog::info("entity_hooks: ResumeForNetwork — CharacterCreate stays DISABLED (zone-load crash prevention)");

    spdlog::info("entity_hooks: ResumeForNetwork — total creates={}, loadingComplete={}",
                 s_totalCreates.load(), s_loadingComplete.load());
}

bool HasRecentInPlaceSpawn(int withinSeconds) {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - s_lastInPlaceSpawnTime);
    return elapsed.count() < withinSeconds;
}

int GetInPlaceSpawnCount() {
    return s_inPlaceSpawnCount.load();
}

uintptr_t GetCapturedFaction() {
    return s_capturedFaction;
}

const std::vector<CachedCharacter>& GetLoadingCharacters() {
    return s_loadingCharacters;
}

int GetTotalCreates() {
    return s_totalCreates.load();
}

int GetTotalDestroys() {
    return s_totalDestroys.load();
}

bool IsInBurst() {
    return s_inBurst.load();
}

bool IsLoadingComplete() {
    return s_loadingComplete.load();
}

} // namespace kmp::entity_hooks
