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

static void __fastcall Hook_SaveGame(void* saveManager, const char* saveName) {
    auto& core = Core::Get();
    if (core.IsConnected() && !core.IsHost()) {
        // Clients don't save in multiplayer - server owns the world state
        spdlog::info("save_hooks: Blocked client save (server-authoritative)");
        return;
    }

    // Host or single-player: allow save
    s_origSave(saveManager, saveName);

    if (core.IsConnected() && core.IsHost()) {
        spdlog::info("save_hooks: Host saved game as '{}'", saveName ? saveName : "unnamed");
    }
}

static void __fastcall Hook_LoadGame(void* saveManager, const char* saveName) {
    auto& core = Core::Get();
    if (core.IsConnected() && !core.IsHost()) {
        // Client: block local load in multiplayer.
        // The server already sends a world snapshot on join (HandleHandshake),
        // so there's no need to request one here.
        spdlog::info("save_hooks: Blocked client load (server sends snapshot on join)");
        return;
    }

    s_origLoad(saveManager, saveName);
}

bool Install() {
    auto& funcs = Core::Get().GetGameFunctions();
    auto& hookMgr = HookManager::Get();

    if (funcs.SaveGame) {
        hookMgr.InstallAt("SaveGame",
                          reinterpret_cast<uintptr_t>(funcs.SaveGame),
                          &Hook_SaveGame, &s_origSave);
    }
    if (funcs.LoadGame) {
        hookMgr.InstallAt("LoadGame",
                          reinterpret_cast<uintptr_t>(funcs.LoadGame),
                          &Hook_LoadGame, &s_origLoad);
    }

    spdlog::info("save_hooks: Installed");
    return true;
}

void Uninstall() {
    HookManager::Get().Remove("SaveGame");
    HookManager::Get().Remove("LoadGame");
}

} // namespace kmp::save_hooks
