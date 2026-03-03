#include "save_hooks.h"
#include "../core.h"
#include "kmp/hook_manager.h"
#include "kmp/protocol.h"
#include <spdlog/spdlog.h>

namespace kmp::save_hooks {

using SaveGameFn = void(__fastcall*)(void* saveManager, const char* saveName);
using LoadGameFn = void(__fastcall*)(void* saveManager, const char* saveName);

static SaveGameFn s_origSave = nullptr;
static LoadGameFn s_origLoad = nullptr;
static std::atomic<bool> s_loading{false};

bool IsLoading() { return s_loading.load(); }

static void __fastcall Hook_SaveGame(void* saveManager, const char* saveName) {
    auto& core = Core::Get();

    // Always allow local saves. In multiplayer the local save acts as a
    // checkpoint for the player's own characters; the server independently
    // persists the authoritative world state.
    s_origSave(saveManager, saveName);

    if (core.IsConnected()) {
        spdlog::info("save_hooks: Local save '{}' completed (server state is separate)",
                     saveName ? saveName : "unnamed");
    }
}

static void __fastcall Hook_LoadGame(void* saveManager, const char* saveName) {
    // Set loading flag so entity/combat hooks skip network operations
    // during save load (characters aren't fully initialized yet).
    s_loading = true;
    spdlog::info("save_hooks: Loading local save (loading guard ON)");
    s_origLoad(saveManager, saveName);
    s_loading = false;
    spdlog::info("save_hooks: Save load complete (loading guard OFF)");
}

bool Install() {
    // Save/Load hooks are disabled — the function signatures are not fully
    // verified and calling through bad trampolines crashes during save load.
    // Local saves and loads pass through unmodified, which is the desired
    // behavior: players keep their own save files, multiplayer state is
    // layered on top via the server snapshot.
    spdlog::info("save_hooks: Skipped (pass-through mode)");
    return true;
}

void Uninstall() {
    HookManager::Get().Remove("SaveGame");
    HookManager::Get().Remove("LoadGame");
}

} // namespace kmp::save_hooks
