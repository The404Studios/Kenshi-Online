#include "game_tick_hooks.h"
#include "entity_hooks.h"
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
// HookManager automatically applies the MovRaxRsp fix: a naked detour captures
// RSP at hook entry, and the trampoline wrapper restores RAX before entering
// the original function body. This ensures correct RBP derivation.
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

static std::atomic<int> s_tickCount{0};

static void __fastcall Hook_GameFrameUpdate(void* rcx, void* rdx) {
    int tick = s_tickCount.fetch_add(1) + 1;

    // ── DEBUG: Log every step for first 5 ticks ──
    if (tick <= 5) {
        char buf[256];
        sprintf_s(buf, "KMP: GameFrameUpdate ENTER tick #%d rcx=0x%p rdx=0x%p\n", tick, rcx, rdx);
        OutputDebugStringA(buf);
    }

    // ═══ SPAWN DIAGNOSTICS (no spawning here) ═══
    // Spawn fallback is handled ONLY in Core::HandleSpawnQueue (10s timeout).
    // Previously this hook also had a 3s direct spawn fallback, but it raced with
    // the safer in-place replay method in entity_hooks (which needs ~5s to settle
    // after loading burst). By removing the competing 3s spawner, in-place replay
    // gets first crack at the queue before the Core fallback kicks in at 10s.
    {
        auto& core = Core::Get();
        bool connected = core.IsConnected();

        // Log spawn conditions every 3000 ticks (~20 seconds at 150 fps)
        // NO spdlog inside MovRaxRsp detour — use OutputDebugStringA only
        if (tick % 3000 == 0 && connected) {
            auto& spawnMgr = core.GetSpawnManager();
            size_t pendingCount = spawnMgr.GetPendingSpawnCount();
            int inPlaceCount = entity_hooks::GetInPlaceSpawnCount();
            char diagBuf[128];
            sprintf_s(diagBuf, "KMP: tick=%d pending=%zu inPlace=%d\n",
                      tick, pendingCount, inPlaceCount);
            OutputDebugStringA(diagBuf);
        }
    }

    if (tick <= 5) {
        char buf[128];
        sprintf_s(buf, "KMP: GameFrameUpdate tick #%d — about to call original (TRAMPOLINE)\n", tick);
        OutputDebugStringA(buf);
    }

    // Call original via MovRaxRsp trampoline wrapper.
    // The wrapper swaps to the game caller's stack and restores RAX before
    // entering the original function body — correct RBP and stack layout.
    SEH_CallOriginal(s_originalFn, rcx, rdx);

    if (tick <= 5) {
        OutputDebugStringA("KMP: GameFrameUpdate — trampoline returned OK\n");
    }

    // ═══ DEFERRED PROBES — DISABLED ═══
    // AnimClass probing was flooding the log with failures every frame and never
    // succeeding. PlayerControlled probing relies on CharacterIterator which also
    // fails. Both are non-essential optimizations. Disabled to eliminate as crash source.

    if (tick <= 5) {
        char buf[128];
        sprintf_s(buf, "KMP: GameFrameUpdate tick #%d DONE\n", tick);
        OutputDebugStringA(buf);
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
