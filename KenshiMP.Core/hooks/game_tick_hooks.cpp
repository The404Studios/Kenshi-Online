#include "game_tick_hooks.h"
#include "entity_hooks.h"
#include "ai_hooks.h"
#include "squad_hooks.h"
#include "../core.h"
#include "../game/spawn_manager.h"
#include "../game/game_types.h"
#include "../game/player_controller.h"
#include "kmp/hook_manager.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <Windows.h>

namespace kmp::game_tick_hooks {

// GameFrameUpdate starts with `mov rax, rsp` (48 8B C4).
// The raw MinHook trampoline works correctly: it copies the instruction verbatim,
// so saves via [rax+XX] and restores via [rax+XX] use the same addresses.
// Previously used HookBypass (MH_DisableHook/MH_EnableHook per tick) which
// freezes ALL threads twice per game tick — this caused crashes during loading.
using GameFrameUpdateFn = void(__fastcall*)(void* rcx, void* rdx);

static GameFrameUpdateFn s_originalFn = nullptr; // trampoline — USED for calling
static uintptr_t s_targetAddr = 0;               // real function address (for diagnostics)

// ── SEH wrapper for calling original GameFrameUpdate via TRAMPOLINE ──
static void SEH_CallOriginal(GameFrameUpdateFn trampoline, void* rcx, void* rdx) {
    __try {
        trampoline(rcx, rdx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_crashCount = 0;
        if (++s_crashCount <= 5) {
            char buf[128];
            sprintf_s(buf, "KMP: GameFrameUpdate TRAMPOLINE CRASHED #%d\n", s_crashCount);
            OutputDebugStringA(buf);
        }
    }
}

// ── SEH wrapper for direct spawn post-setup ──
// Extracted from Hook_GameFrameUpdate because MSVC forbids __try in functions
// with C++ objects that need unwinding (HookBypass has a destructor).
static bool SEH_DirectSpawnPostSetup(void* newChar, EntityID netId, PlayerID owner,
                                      Vec3 pos) {
    __try {
        auto& core = Core::Get();
        game::CharacterAccessor accessor(newChar);
        if (pos.x != 0.f || pos.y != 0.f || pos.z != 0.f) {
            accessor.WritePosition(pos);
        }

        // Set name + faction
        core.GetPlayerController().OnRemoteCharacterSpawned(netId, newChar, owner);

        // Mark as remote-controlled (AI decisions overridden)
        ai_hooks::MarkRemoteControlled(newChar);

        // Squad injection (engine exploit)
        squad_hooks::AddCharacterToLocalSquad(newChar);

        // Set isPlayerControlled flag
        game::WritePlayerControlled(reinterpret_cast<uintptr_t>(newChar), true);

        // Schedule deferred AnimClass probe
        game::ScheduleDeferredAnimClassProbe(reinterpret_cast<uintptr_t>(newChar));

        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        spdlog::error("game_tick_hooks: Direct spawn post-setup crashed for entity {} "
                       "(char exists, registry linked)", netId);
        return false;
    }
}

static std::atomic<int> s_tickCount{0};

static void __fastcall Hook_GameFrameUpdate(void* rcx, void* rdx) {
    int tick = s_tickCount.fetch_add(1) + 1;

    // ── DEBUG: Log every step for first 5 ticks ──
    if (tick <= 5) {
        char buf[256];
        sprintf_s(buf, "KMP: GameFrameUpdate ENTER tick #%d rcx=0x%p rdx=0x%p\n", tick, rcx, rdx);
        OutputDebugStringA(buf);
        spdlog::info("game_tick_hooks: ENTER tick #{} rcx=0x{:X} rdx=0x{:X}",
                     tick, (uintptr_t)rcx, (uintptr_t)rdx);
    }

    // ═══ DIRECT SPAWN FALLBACK ═══
    // The in-place replay (entity_hooks) piggybacks on natural CharacterCreate events.
    // After world loading completes, these events stop. If spawn requests have been
    // pending for too long, use SpawnCharacterDirect as a fallback.
    // With Phase 1-5 fixes (valid AI, squad injection, player controlled flag, MoveTo blocked),
    // direct-spawned characters are now stable enough for multiplayer use.
    {
        auto& core = Core::Get();
        bool connected = core.IsConnected();
        auto& spawnMgr = core.GetSpawnManager();

        if (connected && core.IsGameLoaded() && spawnMgr.HasPendingSpawns()) {
            static auto s_firstPendingTime = std::chrono::steady_clock::now();
            static bool s_timerStarted = false;

            if (!s_timerStarted) {
                s_firstPendingTime = std::chrono::steady_clock::now();
                s_timerStarted = true;
                spdlog::info("game_tick_hooks: Spawn queue has pending requests — "
                             "waiting for in-place replay or fallback in 3s");
            }

            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - s_firstPendingTime);

            // After 3 seconds of waiting, use direct spawn fallback
            // Process ONE spawn per tick to avoid overwhelming the engine
            if (elapsed.count() >= 3 && spawnMgr.HasPreCallData()) {
                SpawnRequest req;
                if (spawnMgr.PopNextSpawn(req)) {
                    spdlog::info("game_tick_hooks: DIRECT SPAWN FALLBACK for entity {} "
                                 "owner={} at ({:.0f},{:.0f},{:.0f})",
                                 req.netId, req.owner,
                                 req.position.x, req.position.y, req.position.z);

                    Vec3 pos = req.position;
                    void* newChar = spawnMgr.SpawnCharacterDirect(&pos);

                    if (newChar) {
                        spdlog::info("game_tick_hooks: DIRECT SPAWN SUCCESS! char=0x{:X} entity={}",
                                     (uintptr_t)newChar, req.netId);

                        // 1. Link to EntityRegistry (safe — our own code)
                        core.GetEntityRegistry().SetGameObject(req.netId, newChar);
                        core.GetEntityRegistry().UpdatePosition(req.netId, pos);

                        // 2-7. Apply all Phase 1-5 post-spawn setup (SEH-protected)
                        if (SEH_DirectSpawnPostSetup(newChar, req.netId, req.owner, pos)) {
                            core.GetNativeHud().AddSystemMessage(
                                "Remote player character spawned!");
                        }
                    } else {
                        spdlog::error("game_tick_hooks: DIRECT SPAWN FAILED for entity {}",
                                     req.netId);
                        req.retryCount++;
                        if (req.retryCount < MAX_SPAWN_RETRIES) {
                            spawnMgr.RequeueSpawn(req);
                        }
                    }
                }

                // Reset timer if queue is now empty
                if (!spawnMgr.HasPendingSpawns()) {
                    s_timerStarted = false;
                }
            }
        } else {
            // No pending spawns — reset the timer
            static bool s_wasTimerStarted = false;
            // Use a local flag to track if we need to reset
        }

        // Log spawn conditions every 3000 ticks (~20 seconds at 150 fps)
        if (tick % 3000 == 0 && connected) {
            size_t pendingCount = spawnMgr.GetPendingSpawnCount();
            int inPlaceCount = entity_hooks::GetInPlaceSpawnCount();
            spdlog::info("game_tick_hooks: tick={} pending={} inPlaceSpawns={}",
                         tick, pendingCount, inPlaceCount);
        }
    }

    if (tick <= 5) {
        char buf[128];
        sprintf_s(buf, "KMP: GameFrameUpdate tick #%d — about to call original (TRAMPOLINE)\n", tick);
        OutputDebugStringA(buf);
    }

    // Call original via MinHook TRAMPOLINE — no thread suspension needed.
    // The `mov rax, rsp` instruction is copied verbatim into the trampoline.
    // All [rax+XX] saves and restores are internally consistent (paired).
    // Previously used HookBypass (MH_DisableHook/MH_EnableHook) which froze
    // ALL threads twice per tick, causing crashes during loading.
    SEH_CallOriginal(s_originalFn, rcx, rdx);

    if (tick <= 5) {
        OutputDebugStringA("KMP: GameFrameUpdate — trampoline returned OK\n");
    }

    // ═══ DEFERRED PROBES (Phases 3 & 4) ═══
    // Process AnimClass offset discovery and player-controlled flag probing.
    // These run every tick until they succeed, then stop.
    {
        static bool s_animClassDone = false;
        static bool s_playerCtrlDone = false;

        // Phase 3: AnimClass offset probe (needs character with settled position)
        if (!s_animClassDone) {
            s_animClassDone = game::ProcessDeferredAnimClassProbes();
        }

        // Phase 4: Player controlled offset probe
        // Needs both a player-controlled character and an NPC to cross-validate.
        // Only attempt every 100 ticks to avoid per-frame overhead.
        if (!s_playerCtrlDone && tick % 100 == 50) {
            auto& core = Core::Get();
            if (core.IsGameLoaded()) {
                void* primaryChar = core.GetPlayerController().GetPrimaryCharacter();
                if (primaryChar) {
                    // Find an NPC character (not in our entity registry) for cross-validation
                    uintptr_t npcPtr = 0;
                    game::CharacterIterator iter;
                    while (iter.HasNext()) {
                        game::CharacterAccessor ch = iter.Next();
                        if (!ch.IsValid()) continue;
                        void* chPtr = reinterpret_cast<void*>(ch.GetPtr());
                        if (chPtr == primaryChar) continue; // Skip self

                        // Check if this character is NOT in our registry (= NPC)
                        EntityID netId = core.GetEntityRegistry().GetNetId(chPtr);
                        if (netId == INVALID_ENTITY) {
                            npcPtr = ch.GetPtr();
                            break;
                        }
                    }

                    if (npcPtr != 0) {
                        game::ProbePlayerControlledOffset(
                            reinterpret_cast<uintptr_t>(primaryChar), npcPtr);
                        if (game::GetOffsets().character.isPlayerControlled >= 0) {
                            s_playerCtrlDone = true;
                            spdlog::info("game_tick_hooks: Player controlled offset discovered!");
                        }
                    }
                }
            }
        }
    }

    if (tick <= 5) {
        char buf[128];
        sprintf_s(buf, "KMP: GameFrameUpdate tick #%d DONE\n", tick);
        OutputDebugStringA(buf);
        spdlog::info("game_tick_hooks: tick #{} DONE", tick);
    }
}

bool Install() {
    auto& core = Core::Get();
    auto& hookMgr = HookManager::Get();
    auto& funcs = core.GetGameFunctions();

    if (!funcs.GameFrameUpdate) {
        spdlog::warn("game_tick_hooks: GameFrameUpdate not found, skipping");
        return false;
    }

    s_targetAddr = reinterpret_cast<uintptr_t>(funcs.GameFrameUpdate);

    OutputDebugStringA("KMP: game_tick_hooks — calling InstallAt...\n");

    if (!hookMgr.InstallAt("GameFrameUpdate",
                            s_targetAddr,
                            &Hook_GameFrameUpdate, &s_originalFn)) {
        spdlog::error("game_tick_hooks: Failed to hook GameFrameUpdate");
        OutputDebugStringA("KMP: game_tick_hooks — InstallAt FAILED\n");
        return false;
    }

    char buf[128];
    sprintf_s(buf, "KMP: game_tick_hooks INSTALLED at 0x%llX\n", (unsigned long long)s_targetAddr);
    OutputDebugStringA(buf);
    spdlog::info("game_tick_hooks: Installed at 0x{:X} (trampoline mode — no thread suspension)", s_targetAddr);
    return true;
}

void Uninstall() {
    HookManager::Get().Remove("GameFrameUpdate");
}

} // namespace kmp::game_tick_hooks
