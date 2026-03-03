#include "entity_hooks.h"
#include "save_hooks.h"
#include "ai_hooks.h"
#include "squad_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "../game/spawn_manager.h"
#include "../game/player_controller.h"
#include "kmp/hook_manager.h"
#include "kmp/protocol.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <chrono>
#include <cmath>
#include <unordered_map>

namespace kmp::entity_hooks {

// ── Function Types ──
// CharacterSpawn prologue: mov rax,rsp + 7 pushes + 544 stack
// Confirmed 2-param: RCX=factory(this), RDX=GameData*
// Raw MinHook trampoline works: mov rax,rsp saves/restores are internally consistent.
using CharacterCreateFn = void*(__fastcall*)(void* factory, void* templateData);
using CharacterDestroyFn = void(__fastcall*)(void* character);

// Store the ORIGINAL function addresses (NOT trampolines)
static uintptr_t s_createTargetAddr = 0;
static uintptr_t s_destroyTargetAddr = 0;

// Trampoline pointers (set by MinHook but NOT used for calling CharacterCreate)
static CharacterCreateFn  s_origCreate  = nullptr;
static CharacterDestroyFn s_origDestroy = nullptr;

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
static constexpr int    MIN_CREATES_BEFORE_SUSPEND = 30;

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
            if (!s_loadingComplete.load() && s_totalCreates.load() >= MIN_CREATES_BEFORE_SUSPEND) {
                s_loadingComplete.store(true);
                spdlog::info("entity_hooks: LOADING COMPLETE — spawns now allowed after {}s settle (total creates={})",
                             BURST_SETTLE_SECONDS, s_totalCreates.load());
            }

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

// ── Hook suspension state ──
// After capturing the factory pointer, we permanently disable the CharacterCreate
// hook during loading to avoid the massive overhead of MH_DisableHook/MH_EnableHook
// (which freeze ALL threads on every call). The hook is re-enabled when connecting.
static std::atomic<bool> s_hookSuspended{false};

// Once ResumeForNetwork() is called, NEVER re-suspend.
// Without this flag, the hook re-suspends immediately because IsConnected() is
// still false during async connect (handshake hasn't completed yet).
static std::atomic<bool> s_resumedForNetwork{false};

// MIN_CREATES_BEFORE_SUSPEND defined above (near burst detection constants)

// ── SEH wrapper for calling the original CharacterCreate via TRAMPOLINE ──
// Uses MinHook's trampoline (s_origCreate) instead of HookBypass.
// The trampoline has the `mov rax, rsp` instruction from the original function,
// which captures the trampoline's RSP instead of the caller's. For this 2-param
// __fastcall function in Release builds, this is safe — RAX is only used for
// exception unwind info, not for parameter access.
// NO THREAD SUSPENSION needed — this is the key advantage over HookBypass.
static void* SEH_CallOriginalCreate(CharacterCreateFn trampoline, void* factory, void* templateData) {
    __try {
        return trampoline(factory, templateData);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_trampolineCrash = 0;
        if (++s_trampolineCrash <= 5) {
            OutputDebugStringA("KMP: CharacterCreate TRAMPOLINE CRASHED (SEH caught)\n");
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

// SEH wrapper for in-place spawn replay via TRAMPOLINE (no C++ objects allowed).
// Uses a LOCAL buffer for post-call state to avoid corruption when multiple
// replays happen in the same Hook_CharacterCreate call (MAX_REPLAYS_PER_CALL=3).
static void* SEH_ReplayFactory(CharacterCreateFn trampoline, void* factory, void* reqStruct,
                                const uint8_t* preCallData, size_t structSize) {
    __try {
        // 1. Save post-call state to LOCAL buffer (not global — avoids multi-replay corruption)
        uint8_t postCallBuffer[REQUEST_STRUCT_SIZE];
        memcpy(postCallBuffer, reqStruct, structSize);

        // 2. Restore pre-call data to the ORIGINAL stack address
        memcpy(reqStruct, preCallData, structSize);

        // 3. Call factory via trampoline (no thread suspension!)
        void* result = trampoline(factory, reqStruct);

        // 4. Restore post-call state so the game caller sees expected data
        memcpy(reqStruct, postCallBuffer, structSize);

        return result;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static void* __fastcall Hook_CharacterCreate(void* factory, void* templateData) {
    // ── Direct spawn bypass ──
    if (s_directSpawnBypass.load(std::memory_order_acquire)) {
        OutputDebugStringA("KMP: CharacterCreate — direct spawn bypass (trampoline)\n");
        return SEH_CallOriginalCreate(s_origCreate, factory, templateData);
    }

    int createNum = s_totalCreates.fetch_add(1) + 1;

    // ── DEBUG: Log first 10 creates with OutputDebugString + spdlog ──
    if (createNum <= 10 || createNum % 100 == 0) {
        char buf[256];
        sprintf_s(buf, "KMP: CharacterCreate #%d factory=0x%p template=0x%p\n",
                  createNum, factory, templateData);
        OutputDebugStringA(buf);
        spdlog::info("entity_hooks: CharacterCreate #{} factory=0x{:X} template=0x{:X}",
                      createNum,
                      reinterpret_cast<uintptr_t>(factory),
                      reinterpret_cast<uintptr_t>(templateData));
    }

    // ═══ SAVE PRE-CALL request struct (BEFORE factory modifies it) ═══
    // This is critical: the factory may consume/modify the struct during the call.
    // We need the pristine pre-call state to replay later.
    uint8_t localPreCall[REQUEST_STRUCT_SIZE] = {};
    if (templateData) {
        if (!SEH_MemcpySafe(localPreCall, templateData, REQUEST_STRUCT_SIZE)) {
            // templateData near page boundary — fall through with zeroed buffer
            static int s_memcpyCrash = 0;
            if (++s_memcpyCrash <= 5) {
                OutputDebugStringA("KMP: CharacterCreate — SEH caught AV on memcpy of request struct\n");
            }
        }

        // Also save a persistent copy from the first call
        if (!s_havePreCallData) {
            SEH_MemcpySafe(s_preCallStruct, templateData, REQUEST_STRUCT_SIZE);
            s_havePreCallData = true;
            s_savedFactory = factory;
            uintptr_t origAddr = reinterpret_cast<uintptr_t>(templateData);
            spdlog::info("entity_hooks: Saved PRE-CALL request struct ({} bytes) from call #{} at 0x{:X}",
                         REQUEST_STRUCT_SIZE, createNum, origAddr);

            // Pass to SpawnManager for standalone spawning from GameFrameUpdate
            Core::Get().GetSpawnManager().SetPreCallData(
                s_preCallStruct, REQUEST_STRUCT_SIZE, origAddr);
            // Also wire SetSavedRequestStruct so ProcessSpawnQueue can use it
            Core::Get().GetSpawnManager().SetSavedRequestStruct(
                s_preCallStruct, REQUEST_STRUCT_SIZE);
            OutputDebugStringA("KMP: CharacterCreate — saved request struct + pre-call data\n");
        }
    }

    // Dump the request struct (first 3 calls) to see layout
    if (createNum <= 3 && templateData) {
        uintptr_t reqAddr = reinterpret_cast<uintptr_t>(templateData);
        spdlog::info("entity_hooks: PRE-CALL STRUCT #{} at 0x{:X}:", createNum, reqAddr);
        for (int off = 0; off < 128; off += 8) {
            uintptr_t val = 0;
            Memory::Read(reqAddr + off, val);
            spdlog::info("  req+0x{:02X}: 0x{:016X}", off, val);
        }
    }

    // Call the ORIGINAL function via MinHook TRAMPOLINE.
    // No HookBypass / thread suspension — trampoline routes around the hook safely.
    // The `mov rax, rsp` prologue issue is benign for this 2-param function in Release.
    void* character = nullptr;
    {
        if (createNum <= 10) {
            OutputDebugStringA("KMP: CharacterCreate — calling original via TRAMPOLINE (no thread suspension)...\n");
        }

        character = SEH_CallOriginalCreate(s_origCreate, factory, templateData);

        if (createNum <= 10) {
            char buf[128];
            sprintf_s(buf, "KMP: CharacterCreate — trampoline returned 0x%p\n", character);
            OutputDebugStringA(buf);
        }

        // ═══ POSITION OFFSET DETECTION ═══
        // Scan the pre-call struct for float values matching the character's position.
        // This tells us which offset in the request struct controls spawn position.
        if (character && s_positionOffsetInStruct == -1 && s_positionDetectAttempts < 10) {
            s_positionDetectAttempts++;
            SEH_CharData charData = SEH_ReadCharacterData(character);
            float cx = charData.position.x, cy = charData.position.y, cz = charData.position.z;

            // Only scan if position looks valid (non-zero)
            if (cx != 0.f || cy != 0.f || cz != 0.f) {
                // Scan the pre-call struct for 3 consecutive floats matching position
                for (int off = 0; off < (int)REQUEST_STRUCT_SIZE - 12; off += 4) {
                    float sx, sy, sz;
                    memcpy(&sx, &localPreCall[off], 4);
                    memcpy(&sy, &localPreCall[off + 4], 4);
                    memcpy(&sz, &localPreCall[off + 8], 4);

                    // Check if these 3 floats match the character position (within tolerance)
                    if (fabsf(sx - cx) < 2.0f && fabsf(sy - cy) < 2.0f && fabsf(sz - cz) < 2.0f) {
                        s_positionOffsetInStruct = off;
                        spdlog::info("entity_hooks: POSITION OFFSET FOUND at struct+0x{:X}! "
                                     "struct=({:.1f},{:.1f},{:.1f}) char=({:.1f},{:.1f},{:.1f})",
                                     off, sx, sy, sz, cx, cy, cz);
                        break;
                    }
                }

                // If not found, log some candidate floats for debugging
                if (s_positionOffsetInStruct == -1 && s_positionDetectAttempts <= 3) {
                    spdlog::info("entity_hooks: Position offset NOT found (attempt {}). "
                                 "charPos=({:.1f},{:.1f},{:.1f}). Struct floats:",
                                 s_positionDetectAttempts, cx, cy, cz);
                    for (int off = 0; off < 256; off += 4) {
                        float val;
                        memcpy(&val, &localPreCall[off], 4);
                        // Log floats that look like coordinates (magnitude > 100)
                        if (fabsf(val) > 100.f && fabsf(val) < 200000.f) {
                            spdlog::info("  struct+0x{:02X}: {:.1f}", off, val);
                        }
                    }
                }
            }
        }

        // ═══ IN-PLACE SPAWN REPLAY ═══
        // The original factory call has completed. The request struct at `templateData`
        // may have been modified. We restore the PRE-CALL data to the SAME stack address
        // and call the factory again. All internal/stack-relative pointers remain valid
        // because we're using the original address.
        //
        // This is the PRIMARY spawn mechanism for remote players. It works because:
        // 1. The request struct is at the ORIGINAL stack address (all pointers valid)
        // 2. The factory creates a full game character (visual, physics, AI, etc.)
        // 3. We then teleport the character to the desired position
        if (character && templateData) {
            auto& coreRef = Core::Get();
            // Gate spawns on: connected + game loaded + loading burst finished + settle time elapsed
            // s_loadingComplete is only true AFTER a loading burst has ended (proactive guard).
            // The settle time gives the engine a few seconds to finish initializing after loading.
            bool settled = s_loadingComplete.load() && !IsInLoadingBurst();
            if (settled) {
                auto timeSinceBurst = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - s_burstEndTime);
                settled = (timeSinceBurst.count() >= BURST_SETTLE_SECONDS);
            }
            if (coreRef.IsConnected() && coreRef.IsGameLoaded() && settled) {
                auto& spawnMgr = coreRef.GetSpawnManager();
                auto& registry = coreRef.GetEntityRegistry();

                // Process ONE pending spawn per hook call.
                // Spreading spawns across multiple hook triggers gives the game engine
                // time to initialize each character (AI, pathfinding, physics) before
                // the next one. Doing 3+ at once during loading burst crashes the game.
                constexpr int MAX_REPLAYS_PER_CALL = 1;
                for (int replayIdx = 0; replayIdx < MAX_REPLAYS_PER_CALL; replayIdx++) {
                    SpawnRequest spawnReq;
                    if (!spawnMgr.PopNextSpawn(spawnReq)) break;

                    // ── Per-player spawn cap ──
                    // Prevents one remote player from flooding us with 40+ spawn requests
                    int& playerSpawns = s_spawnsPerPlayer[spawnReq.owner];
                    if (playerSpawns >= MAX_SPAWNS_PER_PLAYER) {
                        spdlog::warn("entity_hooks: SPAWN CAP reached for player {} ({}/{}) — dropping entity {}",
                                     spawnReq.owner, playerSpawns, MAX_SPAWNS_PER_PLAYER, spawnReq.netId);
                        continue; // Drop this spawn, don't requeue
                    }

                    spdlog::info("entity_hooks: IN-PLACE SPAWN #{} for entity {} owner={} ({}/{} cap) at ({:.0f},{:.0f},{:.0f})",
                                 replayIdx + 1, spawnReq.netId, spawnReq.owner,
                                 playerSpawns + 1, MAX_SPAWNS_PER_PLAYER,
                                 spawnReq.position.x, spawnReq.position.y, spawnReq.position.z);

                    // Replay factory call with UNMODIFIED pre-call data at ORIGINAL address.
                    // Position is NOT modified in the struct (crashes due to zone/terrain deps).
                    // We teleport the character after spawning instead.
                    void* newChar = SEH_ReplayFactory(
                        s_origCreate, factory, templateData,
                        localPreCall, REQUEST_STRUCT_SIZE);

                    if (newChar) {
                        uintptr_t newCharAddr = reinterpret_cast<uintptr_t>(newChar);
                        spdlog::info("entity_hooks: IN-PLACE SPAWN SUCCESS! char=0x{:X} for entity {}",
                                     newCharAddr, spawnReq.netId);

                        // ── SEH-protected post-spawn setup ──
                        // WritePosition, registry link, name/faction all follow game memory
                        // pointers that may be invalid for freshly-spawned characters.
                        // SEH catches any AV so the game doesn't call exit(1).
                        Vec3 spawnPos = spawnReq.position;
                        bool setupOk = SEH_PostSpawnSetup(
                            newChar, spawnReq.netId, spawnReq.owner,
                            spawnPos, factory, templateData);

                        if (setupOk) {
                            // Track success + per-player count
                            s_inPlaceSpawnCount.fetch_add(1);
                            s_lastInPlaceSpawnTime = std::chrono::steady_clock::now();
                            playerSpawns++;

                            spdlog::info("entity_hooks: Entity {} fully spawned and registered "
                                         "(total in-place spawns: {})",
                                         spawnReq.netId, s_inPlaceSpawnCount.load());

                            coreRef.GetNativeHud().AddSystemMessage("Character spawned for remote player!");
                        } else {
                            spdlog::error("entity_hooks: Post-spawn setup CRASHED for entity {} "
                                         "(SEH caught — char exists but may be broken)",
                                         spawnReq.netId);
                            // Still count it — the character exists in-game even if setup failed
                            s_inPlaceSpawnCount.fetch_add(1);
                            playerSpawns++;
                        }
                    } else {
                        spdlog::error("entity_hooks: IN-PLACE SPAWN FAILED for entity {} (SEH caught or null)",
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

    if (createNum <= 10) {
        OutputDebugStringA("KMP: CharacterCreate — trampoline call complete\n");
    }

    if (!character) {
        if (createNum <= 10) {
            OutputDebugStringA("KMP: CharacterCreate — original returned NULL\n");
        }
        return nullptr;
    }

    if (createNum <= 10) {
        OutputDebugStringA("KMP: CharacterCreate — feeding SpawnManager...\n");
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
    auto& core = Core::Get();
    bool shouldCapture = true;
    if (IsInLoadingBurst() && core.GetSpawnManager().IsReady()) {
        // Factory captured + enough templates — skip further scanning during burst
        shouldCapture = false;
    }
    if (shouldCapture) {
        SEH_FeedSpawnManager(factory, templateData, character);
    }

    // Track creation rate
    TrackCreationRate();

    // Once we've captured the factory and enough templates, disable this hook
    // during loading to eliminate the per-call overhead of MH_DisableHook/MH_EnableHook.
    // The hook will be re-enabled via ResumeForNetwork() when connecting.
    // NEVER re-suspend after ResumeForNetwork (IsConnected may still be false during async connect).
    if (!core.IsConnected() && core.GetSpawnManager().IsReady() &&
        !s_hookSuspended.load(std::memory_order_relaxed) &&
        !s_resumedForNetwork.load(std::memory_order_relaxed) &&
        createNum >= MIN_CREATES_BEFORE_SUSPEND) {
        s_hookSuspended.store(true, std::memory_order_relaxed);

        char buf[256];
        sprintf_s(buf, "KMP: CharacterCreate — SUSPENDING hook after %d creates\n", createNum);
        OutputDebugStringA(buf);

        HookManager::Get().Disable("CharacterCreate");
        spdlog::info("entity_hooks: Hook SUSPENDED during load (factory captured after {} creates, "
                     "factoryTemplates={}, characterTemplates={})",
                     createNum,
                     core.GetSpawnManager().GetFactoryTemplateCount(),
                     core.GetSpawnManager().GetCharacterTemplateCount());

        // Notify Core that the game world is loaded
        OutputDebugStringA("KMP: CharacterCreate — calling Core::OnGameLoaded()\n");
        core.OnGameLoaded();

        return character;
    }

    // Register characters from the PLAYER'S FACTION in the EntityRegistry.
    // Only squad members should be synced — random NPCs like bandits/traders stay local.
    // MUST check IsGameLoaded — the client can be connected before the game world exists
    // (user clicks Host on main menu, then starts a new game).
    if (core.IsConnected() && core.IsGameLoaded()) {
        // SEH-protected character data read — prevents AV from crashing the game
        SEH_CharData charData = SEH_ReadCharacterData(character);
        if (!charData.valid) {
            return character; // Bad memory — skip this character safely
        }

        Vec3 pos = charData.position;

        // Skip uninitialized characters (position 0,0,0)
        if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) {
            return character;
        }

        // Faction filter: only register characters from the player's faction.
        // This prevents syncing hundreds of random NPCs to the server.
        uintptr_t playerFaction = core.GetPlayerController().GetLocalFactionPtr();

        // If we don't have a faction yet, try to bootstrap from this character.
        // But NEVER register characters when faction is unknown — this was the
        // root cause of the 40+ NPC flood that crashed the joiner.
        if (playerFaction == 0) {
            if (charData.factionPtr != 0) {
                spdlog::info("entity_hooks: FACTION BOOTSTRAP — captured faction 0x{:X} (id={}) from character at ({:.0f},{:.0f},{:.0f})",
                             charData.factionPtr, charData.factionId, pos.x, pos.y, pos.z);

                const_cast<PlayerController&>(core.GetPlayerController())
                    .SetLocalFactionPtr(charData.factionPtr);
                playerFaction = charData.factionPtr;

                core.RequestEntityRescan();
            } else {
                return character;
            }
        }

        // Now we have a faction — filter strictly
        if (charData.factionPtr != playerFaction) {
            return character; // Not a squad member — skip
        }

        // Register in EntityRegistry with local player as owner (host owns squad entities)
        PlayerID owner = core.GetLocalPlayerId();
        EntityID netId = core.GetEntityRegistry().Register(
            character, EntityType::NPC, owner);
        core.GetEntityRegistry().UpdatePosition(netId, pos);

        // During burst loading, skip network send (too many at once)
        if (IsInLoadingBurst()) {
            return character;
        }

        // Report spawn to server (uses already-read SEH_CharData — no more raw reads)
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

            core.GetClient().SendReliable(writer.Data(), writer.Size());
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
            core.GetSpawnManager().SetOrigProcess(
                reinterpret_cast<FactoryProcessFn>(s_createTargetAddr));
            spdlog::info("entity_hooks: CharacterCreate hooked (disable-call-reenable mode)");
            OutputDebugStringA("KMP: entity_hooks — CharacterCreate hooked OK\n");
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
    // Set flag FIRST to prevent immediate re-suspension.
    // The hook would re-suspend because IsConnected() is false during async connect.
    s_resumedForNetwork.store(true, std::memory_order_relaxed);

    // Reset per-player spawn caps for new connection
    s_spawnsPerPlayer.clear();

    if (s_hookSuspended.load(std::memory_order_relaxed)) {
        HookManager::Get().Enable("CharacterCreate");
        s_hookSuspended.store(false, std::memory_order_relaxed);
        spdlog::info("entity_hooks: Hook RESUMED for network sync (total creates so far: {}, loadingComplete={})",
                     s_totalCreates.load(), s_loadingComplete.load());
    }
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

} // namespace kmp::entity_hooks
