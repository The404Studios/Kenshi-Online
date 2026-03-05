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

// ── Hooks ──

// ── Request struct handling ──
// The factory's second parameter is a STACK-ALLOCATED REQUEST STRUCT, not a GameData*.
// The struct contains internal pointers relative to the caller's stack frame.
// To spawn remote characters, we RESTORE the pre-call struct to the SAME stack address
// and replay the factory call. This keeps all internal pointers valid.
static constexpr size_t REQUEST_STRUCT_SIZE = 1024; // Capture enough for self-ref at +0x230
static uint8_t s_preCallStruct[REQUEST_STRUCT_SIZE] = {};
static bool s_havePreCallData = false;
static void* s_savedFactory = nullptr;

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
    // ── Re-entrancy guard ──
    // The MovRaxRsp wrapper uses global state slots (captured_rsp, stub_rsp,
    // saved_game_ret). If CharacterSpawn calls itself internally (e.g., spawning
    // squad members or mounts), the nested wrapper call would overwrite the outer
    // call's slots, corrupting its stack swap context on return → crash.
    //
    // Fix: For reentrant calls, use the RAW MinHook trampoline which starts with
    // `mov rax, rsp` and does NOT touch any global slots. The raw trampoline is
    // just the original function's copied prologue + jmp back — completely safe.
    static thread_local int s_hookDepth = 0;
    struct DepthGuard { int& d; DepthGuard(int& d_) : d(d_) { d++; } ~DepthGuard() { d--; } };
    if (s_hookDepth > 0) {
        // Use raw trampoline — no global slot manipulation, safe for nesting
        if (s_rawCreateTrampoline) {
            return s_rawCreateTrampoline(factory, templateData);
        }
        // Fallback: shouldn't happen, but try wrapper with SEH protection
        return SEH_CallOriginalCreate(s_origCreate, factory, templateData);
    }
    DepthGuard _guard(s_hookDepth);

    // ── Immediate log on FIRST call to prove the hook fires ──
    {
        static bool s_firstCall = true;
        if (s_firstCall) {
            s_firstCall = false;
            spdlog::info("entity_hooks: *** FIRST CharacterCreate call *** factory=0x{:X} template=0x{:X}",
                         reinterpret_cast<uintptr_t>(factory),
                         reinterpret_cast<uintptr_t>(templateData));
        }
    }

    // ── Direct spawn bypass ──
    if (s_directSpawnBypass.load(std::memory_order_acquire)) {
        return SEH_CallOriginalCreate(s_origCreate, factory, templateData);
    }

    int createNum = s_totalCreates.fetch_add(1) + 1;
    g_lastCharacterCreateNum = createNum;  // VEH crash handler reads this

    // ── DEBUG: Log creates at #1-10, then every 10th ──
    if (createNum <= 10 || createNum % 10 == 0) {
        spdlog::info("entity_hooks: CharacterCreate #{} factory=0x{:X} template=0x{:X}",
                      createNum,
                      reinterpret_cast<uintptr_t>(factory),
                      reinterpret_cast<uintptr_t>(templateData));
    }

    // ═══ SAVE PRE-CALL request struct (BEFORE factory modifies it) ═══
    // Only needed for the first few calls (position detection + template capture)
    // and when connected (for in-place spawn replay). Skip the expensive 1024-byte
    // stack alloc + memcpy for the common non-connected loading path.
    auto& coreRef = Core::Get();
    bool needPreCallCapture = !s_havePreCallData ||
                              (s_positionOffsetInStruct == -1 && s_positionDetectAttempts < 10) ||
                              (coreRef.IsConnected() && coreRef.IsGameLoaded());

    uint8_t localPreCall[REQUEST_STRUCT_SIZE];
    bool haveLocalPreCall = false;

    if (needPreCallCapture && templateData) {
        memset(localPreCall, 0, REQUEST_STRUCT_SIZE);
        if (SEH_MemcpySafe(localPreCall, templateData, REQUEST_STRUCT_SIZE)) {
            haveLocalPreCall = true;
        } else {
            static int s_memcpyCrash = 0;
            if (++s_memcpyCrash <= 5) {
                OutputDebugStringA("KMP: CharacterCreate — SEH caught AV on memcpy of request struct\n");
            }
        }

        // Save a persistent copy from the first call
        if (!s_havePreCallData) {
            SEH_MemcpySafe(s_preCallStruct, templateData, REQUEST_STRUCT_SIZE);
            s_havePreCallData = true;
            s_savedFactory = factory;
            uintptr_t origAddr = reinterpret_cast<uintptr_t>(templateData);
            spdlog::info("entity_hooks: Saved PRE-CALL request struct ({} bytes) from call #{} at 0x{:X}",
                         REQUEST_STRUCT_SIZE, createNum, origAddr);

            Core::Get().GetSpawnManager().SetPreCallData(
                s_preCallStruct, REQUEST_STRUCT_SIZE, origAddr);
            Core::Get().GetSpawnManager().SetSavedRequestStruct(
                s_preCallStruct, REQUEST_STRUCT_SIZE);
        }
    }

    // Dump the request struct (first 3 calls only)
    if (createNum <= 3 && templateData) {
        uintptr_t reqAddr = reinterpret_cast<uintptr_t>(templateData);
        spdlog::info("entity_hooks: PRE-CALL STRUCT #{} at 0x{:X}:", createNum, reqAddr);
        for (int off = 0; off < 128; off += 8) {
            uintptr_t val = 0;
            Memory::Read(reqAddr + off, val);
            spdlog::info("  req+0x{:02X}: 0x{:016X}", off, val);
        }
    }

    // ═══ CALL ORIGINAL FUNCTION ═══
    void* character = SEH_CallOriginalCreate(s_origCreate, factory, templateData);

    if (character && haveLocalPreCall) {
        // ═══ POSITION OFFSET DETECTION ═══
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
                        spdlog::info("entity_hooks: POSITION OFFSET FOUND at struct+0x{:X}! "
                                     "struct=({:.1f},{:.1f},{:.1f}) char=({:.1f},{:.1f},{:.1f})",
                                     off, sx, sy, sz, cx, cy, cz);
                        break;
                    }
                }
            }
        }

        // ═══ IN-PLACE SPAWN REPLAY ═══
        if (templateData && coreRef.IsConnected() && coreRef.IsGameLoaded()) {
            bool canSpawn = AssetFacilitator::Get().CanSpawn();
            if (canSpawn) {
                auto& spawnMgr = coreRef.GetSpawnManager();

                constexpr int MAX_REPLAYS_PER_CALL = 1;
                for (int replayIdx = 0; replayIdx < MAX_REPLAYS_PER_CALL; replayIdx++) {
                    SpawnRequest spawnReq;
                    if (!spawnMgr.PopNextSpawn(spawnReq)) break;

                    int& playerSpawns = s_spawnsPerPlayer[spawnReq.owner];
                    if (playerSpawns >= MAX_SPAWNS_PER_PLAYER) {
                        spdlog::warn("entity_hooks: SPAWN CAP reached for player {} ({}/{}) — dropping entity {}",
                                     spawnReq.owner, playerSpawns, MAX_SPAWNS_PER_PLAYER, spawnReq.netId);
                        continue;
                    }

                    spdlog::info("entity_hooks: IN-PLACE SPAWN #{} for entity {} owner={} at ({:.0f},{:.0f},{:.0f})",
                                 replayIdx + 1, spawnReq.netId, spawnReq.owner,
                                 spawnReq.position.x, spawnReq.position.y, spawnReq.position.z);

                    void* newChar = SEH_ReplayFactory(
                        s_origCreate, factory, templateData,
                        localPreCall, REQUEST_STRUCT_SIZE);

                    if (newChar) {
                        Vec3 spawnPos = spawnReq.position;
                        bool setupOk = SEH_PostSpawnSetup(
                            newChar, spawnReq.netId, spawnReq.owner,
                            spawnPos, factory, templateData);

                        s_inPlaceSpawnCount.fetch_add(1);
                        s_lastInPlaceSpawnTime = std::chrono::steady_clock::now();
                        playerSpawns++;

                        if (setupOk) {
                            spdlog::info("entity_hooks: Entity {} spawned OK (total: {})",
                                         spawnReq.netId, s_inPlaceSpawnCount.load());
                            coreRef.GetNativeHud().AddSystemMessage("Character spawned for remote player!");
                        } else {
                            spdlog::error("entity_hooks: Post-spawn setup CRASHED for entity {}",
                                         spawnReq.netId);
                        }
                    } else {
                        spdlog::error("entity_hooks: IN-PLACE SPAWN FAILED for entity {}",
                                     spawnReq.netId);
                        spawnReq.retryCount++;
                        if (spawnReq.retryCount < MAX_SPAWN_RETRIES) {
                            spawnMgr.RequeueSpawn(spawnReq);
                        }
                    }
                }
            }
        }
    }

    if (!character) {
        return nullptr;
    }

    // ═══ EARLY ANIMCLASS PROBE ═══
    // Schedule the first few naturally-created characters for AnimClass offset discovery.
    // This gives us the offset BEFORE any network spawns happen.
    // Only schedule the first 5 characters (don't spam during loading).
    {
        static int s_earlyProbeCount = 0;
        if (s_earlyProbeCount < 5 && character) {
            game::ScheduleDeferredAnimClassProbe(reinterpret_cast<uintptr_t>(character));
            s_earlyProbeCount++;
        }
    }

    // ═══ CACHE CHARACTER DATA DURING LOADING ═══
    // Save character pointer + faction for SendExistingEntitiesToServer fallback.
    // CharacterIterator depends on PlayerBase/GameWorld which may fail on Steam.
    // This cache provides an alternative character source.
    if (character && s_loadingCharacters.size() < MAX_CACHED_CHARACTERS) {
        SEH_CharData cacheData = SEH_ReadCharacterData(character);
        if (cacheData.valid) {
            CachedCharacter cc;
            cc.gameObj    = character;
            cc.factionPtr = cacheData.factionPtr;
            cc.factionId  = cacheData.factionId;
            cc.x          = cacheData.position.x;
            cc.y          = cacheData.position.y;
            cc.z          = cacheData.position.z;
            s_loadingCharacters.push_back(cc);

            // Capture faction from first character with a valid faction
            if (s_capturedFaction == 0 && cacheData.factionPtr != 0) {
                s_capturedFaction = cacheData.factionPtr;
                spdlog::info("entity_hooks: LOADING CACHE — captured faction 0x{:X} (id={}) from char #{} at ({:.0f},{:.0f},{:.0f})",
                             cacheData.factionPtr, cacheData.factionId, createNum,
                             cacheData.position.x, cacheData.position.y, cacheData.position.z);
            }

            if (createNum <= 10 || s_loadingCharacters.size() % 50 == 0) {
                spdlog::debug("entity_hooks: Cached loading char #{} (total cached: {})",
                              createNum, s_loadingCharacters.size());
            }
        }
    }

    // Feed SpawnManager for template/factory capture.
    // Rate-limit during loading burst: after factory is captured and we have enough templates,
    // skip the intensive memory scan (~63 reads per character) to avoid AVs during rapid loading.
    bool shouldCapture = true;
    if (IsInLoadingBurst() && coreRef.GetSpawnManager().IsReady()) {
        shouldCapture = false;
    }
    if (shouldCapture) {
        SEH_FeedSpawnManager(factory, templateData, character);
    }

    // Track creation rate
    TrackCreationRate();

    // ── Notify Core that the game world is loaded ──
    if (!coreRef.IsGameLoaded() &&
        coreRef.GetSpawnManager().IsReady() &&
        createNum >= MIN_CREATES_BEFORE_READY) {
        spdlog::info("entity_hooks: Triggering OnGameLoaded (connected={}, creates={})",
                     coreRef.IsConnected(), createNum);
        coreRef.OnGameLoaded();
    }

    // Register characters from the PLAYER'S FACTION in the EntityRegistry.
    // Only squad members should be synced — random NPCs like bandits/traders stay local.
    // MUST check IsGameLoaded — the client can be connected before the game world exists
    // (user clicks Host on main menu, then starts a new game).
    if (coreRef.IsConnected() && coreRef.IsGameLoaded()) {
        // SEH-protected character data read — prevents AV from crashing the game
        SEH_CharData charData = SEH_ReadCharacterData(character);
        if (!charData.valid) {
            return character;
        }

        Vec3 pos = charData.position;

        if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) {
            return character;
        }

        uintptr_t playerFaction = coreRef.GetPlayerController().GetLocalFactionPtr();

        if (playerFaction == 0) {
            if (charData.factionPtr != 0) {
                spdlog::info("entity_hooks: FACTION BOOTSTRAP — captured faction 0x{:X} (id={}) from character at ({:.0f},{:.0f},{:.0f})",
                             charData.factionPtr, charData.factionId, pos.x, pos.y, pos.z);

                const_cast<PlayerController&>(coreRef.GetPlayerController())
                    .SetLocalFactionPtr(charData.factionPtr);
                playerFaction = charData.factionPtr;

                coreRef.RequestEntityRescan();
            } else {
                return character;
            }
        }

        if (charData.factionPtr != playerFaction) {
            return character;
        }

        PlayerID owner = coreRef.GetLocalPlayerId();
        EntityID netId = coreRef.GetEntityRegistry().Register(
            character, EntityType::NPC, owner);
        coreRef.GetEntityRegistry().UpdatePosition(netId, pos);

        if (IsInLoadingBurst()) {
            return character;
        }

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
            if (nameLen > 0) {
                writer.WriteRaw(templateName.data(), nameLen);
            }

            coreRef.GetClient().SendReliable(writer.Data(), writer.Size());
        }
    }

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

    // Clear stale loading cache from any previous game load — prevents stale
    // game object pointers from being used in SendExistingEntitiesToServer fallback.
    s_loadingCharacters.clear();
    s_capturedFaction = 0;

    // Re-enable CharacterCreate hook for multiplayer.
    // It was disabled after loading to prevent the MovRaxRsp silent crash.
    // Now that we're connected, we need it to register new characters.
    if (HookManager::Get().Enable("CharacterCreate")) {
        spdlog::info("entity_hooks: ResumeForNetwork — CharacterCreate RE-ENABLED for multiplayer");
    } else {
        spdlog::warn("entity_hooks: ResumeForNetwork — CharacterCreate enable failed (may not be installed)");
    }

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
