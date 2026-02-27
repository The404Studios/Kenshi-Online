#include "time_hooks.h"
#include "../core.h"
#include "kmp/hook_manager.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>

namespace kmp::time_hooks {

using TimeUpdateFn = void(__fastcall*)(void* timeManager, float deltaTime);

static TimeUpdateFn s_origTimeUpdate = nullptr;
static float s_serverTimeOfDay = 0.5f;
static float s_serverGameSpeed = 1.0f;
static bool  s_hasServerTime = false;

// Captured time manager pointer for direct writes
static void* s_timeManager = nullptr;

void SetServerTime(float timeOfDay, float gameSpeed) {
    s_serverTimeOfDay = timeOfDay;
    s_serverGameSpeed = gameSpeed;
    s_hasServerTime = true;

    // If we have the time manager pointer, write time directly
    if (s_timeManager) {
        // Write time of day to the time manager object.
        // Time of day is typically a float at a known offset in the manager.
        // Offset heuristic: timeOfDay is usually at +0x08 or +0x10 in the time manager.
        Memory::Write(reinterpret_cast<uintptr_t>(s_timeManager) + 0x08, timeOfDay);
        Memory::Write(reinterpret_cast<uintptr_t>(s_timeManager) + 0x10, gameSpeed);
    }
}

static void __fastcall Hook_TimeUpdate(void* timeManager, float deltaTime) {
    // Capture the time manager pointer on first call
    if (!s_timeManager) {
        s_timeManager = timeManager;
        spdlog::info("time_hooks: Captured time manager at 0x{:X}",
                     reinterpret_cast<uintptr_t>(timeManager));
    }

    auto& core = Core::Get();
    if (core.IsConnected() && !core.IsHost() && s_hasServerTime) {
        // Client: override delta time with server-controlled speed.
        // This keeps the client's time progression synced with the server.
        deltaTime *= s_serverGameSpeed;
    }

    s_origTimeUpdate(timeManager, deltaTime);

    // If connected, trigger the game tick
    if (core.IsConnected()) {
        core.OnGameTick(deltaTime);
    }
}

bool Install() {
    auto& funcs = Core::Get().GetGameFunctions();
    auto& hookMgr = HookManager::Get();

    if (funcs.TimeUpdate) {
        if (hookMgr.InstallAt("TimeUpdate",
                              reinterpret_cast<uintptr_t>(funcs.TimeUpdate),
                              &Hook_TimeUpdate, &s_origTimeUpdate)) {
            // Tell Core that we're driving OnGameTick so render_hooks doesn't double-call it
            Core::Get().SetTimeHookActive(true);
            spdlog::info("time_hooks: TimeUpdate hook active — driving OnGameTick");
        }
    }

    spdlog::info("time_hooks: Installed (TimeUpdate={})", funcs.TimeUpdate != nullptr);
    return true;
}

void Uninstall() {
    HookManager::Get().Remove("TimeUpdate");
}

} // namespace kmp::time_hooks
