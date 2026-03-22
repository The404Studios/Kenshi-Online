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
    void* gameData;
    Vec3 position;
};

static std::mutex s_queueMutex;
static std::queue<SquadSpawnRequest> s_spawnQueue;
static std::atomic<int> s_successCount{0};
static std::atomic<int> s_bypassCount{0};

// ── activePlatoon struct offsets (from research mod RE) ──
static constexpr int OFFSET_SKIP_CHECK_1 = 0xF0;   // bool: if 1, skip spawning
static constexpr int OFFSET_SKIP_CHECK_2 = 0x58;   // bool: if >0, also skip
static constexpr int OFFSET_SKIP_CHECK_3 = 0x250;  // void*: must be 0 for bypass
static constexpr int OFFSET_SQUAD_PTR    = 0x78;    // platoon*
static constexpr int OFFSET_LEADER       = 0xA0;    // CharacterHuman* leader

// ── Hook function type ──
using SquadSpawnCheckFn = void(__fastcall*)(void* context, void* activePlatoon);
static SquadSpawnCheckFn s_origSquadSpawnCheck = nullptr;

// ── SEH-safe struct reads/writes ──
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

// Saved state for restoration after bypass
struct SavedChecks {
    bool check1;
    bool check2;
    uintptr_t platoonAddr;
    bool valid;
};
static SavedChecks s_savedChecks = {};

static void __fastcall Hook_SquadSpawnCheck(void* context, void* activePlatoon) {
    uintptr_t apAddr = reinterpret_cast<uintptr_t>(activePlatoon);

    // Check BOTH queues: spawn_manager's network queue (primary) and our own (fallback).
    // Network packets queue to spawn_manager; entity_hooks hijacks NPCs from that queue.
    // Our job is to FORCE NPC creation when the game would otherwise skip spawning.
    bool hasPending = false;
    {
        auto& spawnMgr = Core::Get().GetSpawnManager();
        hasPending = spawnMgr.HasPendingSpawns();
    }
    if (!hasPending) {
        std::lock_guard lock(s_queueMutex);
        hasPending = !s_spawnQueue.empty();
    }

    if (hasPending && apAddr > 0x10000 && apAddr < 0x00007FFFFFFFFFFF) {
        bool check1 = false, check2 = false;
        uintptr_t check3 = 0;

        SEH_ReadBool(apAddr + OFFSET_SKIP_CHECK_1, check1);
        SEH_ReadBool(apAddr + OFFSET_SKIP_CHECK_2, check2);
        SEH_ReadPtr(apAddr + OFFSET_SKIP_CHECK_3, check3);

        // Research mod condition: check3 == 0 AND check1 == 1 (would normally skip)
        if (check3 == 0 && check1 == true) {
            int bypassNum = s_bypassCount.fetch_add(1) + 1;
            // NO spdlog inside detour — OutputDebugStringA only (stack buffer, no heap)
            {
                char dbg[128];
                sprintf_s(dbg, "KMP: squad bypass #%d platoon=0x%llX\n",
                          bypassNum, (unsigned long long)apAddr);
                OutputDebugStringA(dbg);
            }

            s_savedChecks.check1 = check1;
            s_savedChecks.check2 = check2;
            s_savedChecks.platoonAddr = apAddr;
            s_savedChecks.valid = true;

            // Flip checks to force spawn — entity_hooks NPC Hijack will grab
            // the resulting character from spawn_manager's queue
            SEH_WriteBool(apAddr + OFFSET_SKIP_CHECK_1, false);
            SEH_WriteBool(apAddr + OFFSET_SKIP_CHECK_2, false);

            // Drain our local queue if it had entries (legacy path)
            {
                std::lock_guard lock(s_queueMutex);
                if (!s_spawnQueue.empty()) {
                    s_spawnQueue.pop();
                }
            }
        }
    }

    // Call original — spawns if checks pass
    s_origSquadSpawnCheck(context, activePlatoon);

    // Restore original check values
    if (s_savedChecks.valid && s_savedChecks.platoonAddr == apAddr) {
        SEH_WriteBool(apAddr + OFFSET_SKIP_CHECK_1, s_savedChecks.check1);
        SEH_WriteBool(apAddr + OFFSET_SKIP_CHECK_2, s_savedChecks.check2);
        s_savedChecks.valid = false;
        s_successCount.fetch_add(1);
        OutputDebugStringA("KMP: squad bypass complete — NPC should exist for hijack\n");
    }
}

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
