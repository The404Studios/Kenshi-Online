#include "building_hooks.h"
#include "kmp/hook_manager.h"
#include "kmp/patterns.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/memory.h"
#include "../core.h"
#include "../game/game_types.h"
#include <spdlog/spdlog.h>

namespace kmp::building_hooks {

// ── Function typedefs ──
using BuildingPlaceFn     = void(__fastcall*)(void* world, void* building, float x, float y, float z);
using BuildingDestroyedFn = void(__fastcall*)(void* building);
using BuildingDismantleFn = void(__fastcall*)(void* building);
using BuildingConstructFn = void(__fastcall*)(void* building, float progress);
using BuildingRepairFn    = void(__fastcall*)(void* building, float amount);

// ── State ──
static BuildingPlaceFn     s_origBuildingPlace     = nullptr;
static BuildingDestroyedFn s_origBuildingDestroyed = nullptr;
static BuildingDismantleFn s_origBuildingDismantle = nullptr;
static BuildingConstructFn s_origBuildingConstruct = nullptr;
static BuildingRepairFn    s_origBuildingRepair    = nullptr;
static int s_placeCount = 0;
static int s_destroyCount = 0;
static int s_dismantleCount = 0;
static int s_constructCount = 0;
static int s_repairCount = 0;
static bool s_loading = false;

// Crash counters — auto-disable hooks after 3 crashes (wrong function matched by scanner)
static int s_placeCrashCount = 0;
static int s_destroyCrashCount = 0;
static int s_dismantleCrashCount = 0;
static int s_constructCrashCount = 0;
static int s_repairCrashCount = 0;
static constexpr int MAX_CRASHES = 3;

// ── SEH wrappers (no C++ objects with destructors) ──

static bool SEH_BuildingPlace(void* world, void* building, float x, float y, float z) {
    __try {
        s_origBuildingPlace(world, building, x, y, z);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SEH_BuildingDestroyed(void* building) {
    __try {
        s_origBuildingDestroyed(building);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SEH_BuildingDismantle(void* building) {
    __try {
        s_origBuildingDismantle(building);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SEH_BuildingConstruct(void* building, float progress) {
    __try {
        s_origBuildingConstruct(building, progress);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SEH_BuildingRepair(void* building, float amount) {
    __try {
        s_origBuildingRepair(building, amount);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ── Hooks ──

static void __fastcall Hook_BuildingPlace(void* world, void* building, float x, float y, float z) {
    s_placeCount++;

    if (!SEH_BuildingPlace(world, building, x, y, z)) {
        if (++s_placeCrashCount <= MAX_CRASHES) {
            spdlog::error("building_hooks: BuildingPlace crashed ({}/{})", s_placeCrashCount, MAX_CRASHES);
        }
        if (s_placeCrashCount == MAX_CRASHES) {
            spdlog::warn("building_hooks: BuildingPlace crashed {} times — disabling hook (wrong function matched)", MAX_CRASHES);
            HookManager::Get().Remove("BuildingPlace");
            s_origBuildingPlace = nullptr;
        }
        return;
    }

    if (s_loading) return;

    auto& core = Core::Get();
    if (!core.IsConnected()) return;

    spdlog::info("building_hooks: BuildingPlace #{} (bld=0x{:X}, pos=[{:.1f},{:.1f},{:.1f}])",
                  s_placeCount, (uintptr_t)building, x, y, z);

    // Extract templateId from building's GameData backpointer
    uint32_t templateId = 0;
    uintptr_t bldPtr = reinterpret_cast<uintptr_t>(building);
    auto& bldOffsets = game::GetOffsets().building;

    // GameData* is typically at +0x28 (same as character pattern)
    uintptr_t gameData = 0;
    if (Memory::Read(bldPtr + 0x28, gameData) && gameData != 0 && gameData > 0x10000) {
        Memory::Read(gameData + 0x08, templateId);
    }

    // Extract rotation from building struct
    uint32_t compQuat = 0;
    if (bldOffsets.rotation >= 0) {
        Quat rot;
        if (Memory::Read(bldPtr + bldOffsets.rotation, rot)) {
            compQuat = rot.Compress();
        }
    }

    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_BuildRequest);
    MsgBuildRequest msg{};
    msg.templateId = templateId;
    msg.posX = x;
    msg.posY = y;
    msg.posZ = z;
    msg.compressedQuat = compQuat;
    writer.WriteRaw(&msg, sizeof(msg));
    core.GetClient().SendReliable(writer.Data(), writer.Size());
}

static void __fastcall Hook_BuildingDestroyed(void* building) {
    s_destroyCount++;

    if (!SEH_BuildingDestroyed(building)) {
        if (++s_destroyCrashCount <= MAX_CRASHES) {
            spdlog::error("building_hooks: BuildingDestroyed crashed ({}/{})", s_destroyCrashCount, MAX_CRASHES);
        }
        if (s_destroyCrashCount == MAX_CRASHES) {
            spdlog::warn("building_hooks: BuildingDestroyed disabled (wrong function)");
            HookManager::Get().Remove("BuildingDestroyed");
            s_origBuildingDestroyed = nullptr;
        }
        return;
    }

    if (s_loading) return;

    auto& core = Core::Get();
    if (!core.IsConnected()) return;

    spdlog::info("building_hooks: BuildingDestroyed #{} (bld=0x{:X})",
                  s_destroyCount, (uintptr_t)building);

    auto& registry = core.GetEntityRegistry();
    EntityID netId = registry.GetNetId(building);
    if (netId != INVALID_ENTITY) {
        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_EntityDespawnReq);
        writer.WriteU32(netId);
        writer.WriteU8(1); // reason: destroyed
        core.GetClient().SendReliable(writer.Data(), writer.Size());
    }
}

static void __fastcall Hook_BuildingDismantle(void* building) {
    s_dismantleCount++;

    if (!SEH_BuildingDismantle(building)) {
        if (++s_dismantleCrashCount <= MAX_CRASHES) {
            spdlog::error("building_hooks: BuildingDismantle crashed ({}/{})", s_dismantleCrashCount, MAX_CRASHES);
        }
        if (s_dismantleCrashCount == MAX_CRASHES) {
            spdlog::warn("building_hooks: BuildingDismantle disabled (wrong function)");
            HookManager::Get().Remove("BuildingDismantle");
            s_origBuildingDismantle = nullptr;
        }
        return;
    }

    if (s_loading) return;

    auto& core = Core::Get();
    if (!core.IsConnected()) return;

    spdlog::info("building_hooks: BuildingDismantle #{} (bld=0x{:X})",
                  s_dismantleCount, (uintptr_t)building);

    auto& registry = core.GetEntityRegistry();
    EntityID netId = registry.GetNetId(building);
    if (netId != INVALID_ENTITY) {
        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_BuildDismantle);
        MsgBuildDismantle msg{};
        msg.buildingId = netId;
        msg.dismantlerId = core.GetLocalPlayerId();
        writer.WriteRaw(&msg, sizeof(msg));
        core.GetClient().SendReliable(writer.Data(), writer.Size());
    }
}

static void __fastcall Hook_BuildingConstruct(void* building, float progress) {
    s_constructCount++;

    if (!SEH_BuildingConstruct(building, progress)) {
        if (++s_constructCrashCount <= MAX_CRASHES) {
            spdlog::error("building_hooks: BuildingConstruct crashed ({}/{})", s_constructCrashCount, MAX_CRASHES);
        }
        if (s_constructCrashCount == MAX_CRASHES) {
            spdlog::warn("building_hooks: BuildingConstruct disabled (wrong function)");
            HookManager::Get().Remove("BuildingConstruct");
            s_origBuildingConstruct = nullptr;
        }
        return;
    }

    if (s_loading) return;

    auto& core = Core::Get();
    if (!core.IsConnected()) return;

    if (s_constructCount % 50 == 0) {
        spdlog::debug("building_hooks: BuildingConstruct #{} (bld=0x{:X}, progress={:.2f})",
                       s_constructCount, (uintptr_t)building, progress);
    }
}

static void __fastcall Hook_BuildingRepair(void* building, float amount) {
    s_repairCount++;

    if (!SEH_BuildingRepair(building, amount)) {
        if (++s_repairCrashCount <= MAX_CRASHES) {
            spdlog::error("building_hooks: BuildingRepair crashed ({}/{})", s_repairCrashCount, MAX_CRASHES);
        }
        if (s_repairCrashCount == MAX_CRASHES) {
            spdlog::warn("building_hooks: BuildingRepair disabled (wrong function)");
            HookManager::Get().Remove("BuildingRepair");
            s_origBuildingRepair = nullptr;
        }
        return;
    }

    if (s_loading) return;

    auto& core = Core::Get();
    if (!core.IsConnected()) return;

    if (s_repairCount % 50 == 0) {
        spdlog::debug("building_hooks: BuildingRepair #{} (bld=0x{:X}, amount={:.2f})",
                       s_repairCount, (uintptr_t)building, amount);
    }
}

// ── Install / Uninstall ──

bool Install() {
    auto& funcs = Core::Get().GetGameFunctions();
    auto& hooks = HookManager::Get();
    int installed = 0;

    if (funcs.BuildingPlace) {
        if (hooks.InstallAt("BuildingPlace", reinterpret_cast<uintptr_t>(funcs.BuildingPlace),
                            &Hook_BuildingPlace, &s_origBuildingPlace)) {
            installed++;
        }
    }

    if (funcs.BuildingDestroyed) {
        if (hooks.InstallAt("BuildingDestroyed", reinterpret_cast<uintptr_t>(funcs.BuildingDestroyed),
                            &Hook_BuildingDestroyed, &s_origBuildingDestroyed)) {
            installed++;
        }
    }

    if (funcs.BuildingDismantle) {
        if (hooks.InstallAt("BuildingDismantle", reinterpret_cast<uintptr_t>(funcs.BuildingDismantle),
                            &Hook_BuildingDismantle, &s_origBuildingDismantle)) {
            installed++;
        }
    }

    if (funcs.BuildingConstruct) {
        if (hooks.InstallAt("BuildingConstruct", reinterpret_cast<uintptr_t>(funcs.BuildingConstruct),
                            &Hook_BuildingConstruct, &s_origBuildingConstruct)) {
            installed++;
        }
    }

    if (funcs.BuildingRepair) {
        if (hooks.InstallAt("BuildingRepair", reinterpret_cast<uintptr_t>(funcs.BuildingRepair),
                            &Hook_BuildingRepair, &s_origBuildingRepair)) {
            installed++;
        }
    }

    spdlog::info("building_hooks: {}/5 hooks installed", installed);
    return installed > 0;
}

void Uninstall() {
    auto& hooks = HookManager::Get();
    if (s_origBuildingPlace)     hooks.Remove("BuildingPlace");
    if (s_origBuildingDestroyed) hooks.Remove("BuildingDestroyed");
    if (s_origBuildingDismantle) hooks.Remove("BuildingDismantle");
    if (s_origBuildingConstruct) hooks.Remove("BuildingConstruct");
    if (s_origBuildingRepair)    hooks.Remove("BuildingRepair");
    s_origBuildingPlace = nullptr;
    s_origBuildingDestroyed = nullptr;
    s_origBuildingDismantle = nullptr;
    s_origBuildingConstruct = nullptr;
    s_origBuildingRepair = nullptr;
}

void SetLoading(bool loading) {
    s_loading = loading;
}

} // namespace kmp::building_hooks
