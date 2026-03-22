#include "time_hooks.h"
#include "../core.h"
#include "kmp/hook_manager.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <Windows.h>

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
        Memory::Write(reinterpret_cast<uintptr_t>(s_timeManager) + 0x08, timeOfDay);
        Memory::Write(reinterpret_cast<uintptr_t>(s_timeManager) + 0x10, gameSpeed);
    }
}

float GetTimeOfDay() {
    if (!s_timeManager) return 0.5f;
    float tod = 0.5f;
    Memory::Read(reinterpret_cast<uintptr_t>(s_timeManager) + 0x08, tod);
    return tod;
}

float GetGameSpeed() {
    if (!s_timeManager) return 1.0f;
    float speed = 1.0f;
    Memory::Read(reinterpret_cast<uintptr_t>(s_timeManager) + 0x10, speed);
    return speed;
}

bool WriteTimeOfDay(float timeOfDay) {
    if (!s_timeManager) return false;
    return Memory::Write(reinterpret_cast<uintptr_t>(s_timeManager) + 0x08, timeOfDay);
}

bool HasTimeManager() {
    return s_timeManager != nullptr;
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

    __try {
        s_origTimeUpdate(timeManager, deltaTime);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        spdlog::error("time_hooks: TimeUpdate trampoline CRASHED!");
        return;
    }

    // If connected, trigger the game tick
    static int s_timeHookCallCount = 0;
    s_timeHookCallCount++;
    if (core.IsConnected()) {
        if (s_timeHookCallCount <= 5) {
            char buf[128];
            sprintf_s(buf, "KMP: Hook_TimeUpdate calling OnGameTick (call #%d, dt=%.4f)\n",
                      s_timeHookCallCount, deltaTime);
            OutputDebugStringA(buf);
        }
        core.OnGameTick(deltaTime);
    } else {
        // Log first 5 non-connected calls then every 3000th to confirm hook is alive
        if (s_timeHookCallCount <= 5 || s_timeHookCallCount % 3000 == 0) {
            char buf[128];
            sprintf_s(buf, "KMP: Hook_TimeUpdate #%d — NOT connected, skipping OnGameTick\n",
                      s_timeHookCallCount);
            OutputDebugStringA(buf);
        }
    }
}

bool Install() {
    auto& funcs = Core::Get().GetGameFunctions();
    auto& hookMgr = HookManager::Get();

    if (funcs.TimeUpdate) {
        if (hookMgr.InstallAt("TimeUpdate",
                              reinterpret_cast<uintptr_t>(funcs.TimeUpdate),
                              &Hook_TimeUpdate, &s_origTimeUpdate)) {
            // Do NOT set TimeHookActive — the function at RVA 0x214B50 is never called
            // by the game on Steam builds. render_hooks Present drives OnGameTick instead.
            // If TimeUpdate starts firing, the 4ms dedup guard in OnGameTick prevents
            // double-processing.
            spdlog::info("time_hooks: TimeUpdate hook installed (not claiming active — Present drives OnGameTick)");
        }
    }

    spdlog::info("time_hooks: Installed (TimeUpdate={})", funcs.TimeUpdate != nullptr);
    return true;
}

void Uninstall() {
    HookManager::Get().Remove("TimeUpdate");
}

} // namespace kmp::time_hooks
