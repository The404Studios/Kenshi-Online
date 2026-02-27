#include "world_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "kmp/hook_manager.h"
#include "kmp/protocol.h"
#include "kmp/memory.h"
#include "kmp/safe_hook.h"
#include <spdlog/spdlog.h>

namespace kmp::world_hooks {

using ZoneLoadFn = void(__fastcall*)(void* zoneMgr, int zoneX, int zoneY);
using ZoneUnloadFn = void(__fastcall*)(void* zoneMgr, int zoneX, int zoneY);
using BuildingPlaceFn = void(__fastcall*)(void* world, void* building, float x, float y, float z);

static ZoneLoadFn      s_origZoneLoad   = nullptr;
static ZoneUnloadFn    s_origZoneUnload = nullptr;
static BuildingPlaceFn s_origBuildPlace = nullptr;

// ── Hook Health ──
static HookHealth s_zoneLoadHealth{"ZoneLoad"};
static HookHealth s_zoneUnloadHealth{"ZoneUnload"};
static HookHealth s_buildPlaceHealth{"BuildingPlace"};

// ── Diagnostic Counters ──
static std::atomic<int> s_zoneLoadCount{0};
static std::atomic<int> s_zoneUnloadCount{0};
static std::atomic<int> s_buildPlaceCount{0};

static void __fastcall Hook_ZoneLoad(void* zoneMgr, int zoneX, int zoneY) {
    int callNum = s_zoneLoadCount.fetch_add(1) + 1;
    spdlog::info("world_hooks: ZoneLoad #{} zone=({},{}) mgr=0x{:X}",
                 callNum, zoneX, zoneY, reinterpret_cast<uintptr_t>(zoneMgr));
    // SEH-protected trampoline call
    if (!SafeCall_Void_PtrII(reinterpret_cast<void*>(s_origZoneLoad),
                              zoneMgr, zoneX, zoneY, &s_zoneLoadHealth)) {
        if (s_zoneLoadHealth.trampolineFailed.load()) {
            spdlog::error("world_hooks: ZoneLoad trampoline CRASHED! Hook disabled.");
        }
        return;
    }

    auto& core = Core::Get();
    if (core.IsConnected()) {
        spdlog::debug("world_hooks: Zone loaded ({}, {})", zoneX, zoneY);
        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_ZoneRequest);
        writer.WriteI32(zoneX);
        writer.WriteI32(zoneY);
        core.GetClient().SendReliable(writer.Data(), writer.Size());
    }
}

static void __fastcall Hook_ZoneUnload(void* zoneMgr, int zoneX, int zoneY) {
    int callNum = s_zoneUnloadCount.fetch_add(1) + 1;
    spdlog::info("world_hooks: ZoneUnload #{} zone=({},{}) mgr=0x{:X}",
                 callNum, zoneX, zoneY, reinterpret_cast<uintptr_t>(zoneMgr));

    auto& core = Core::Get();
    if (core.IsConnected()) {
        spdlog::debug("world_hooks: Zone unloading ({}, {})", zoneX, zoneY);
        core.GetEntityRegistry().RemoveEntitiesInZone(ZoneCoord(zoneX, zoneY));
    }

    // SEH-protected trampoline call
    if (!SafeCall_Void_PtrII(reinterpret_cast<void*>(s_origZoneUnload),
                              zoneMgr, zoneX, zoneY, &s_zoneUnloadHealth)) {
        if (s_zoneUnloadHealth.trampolineFailed.load()) {
            spdlog::error("world_hooks: ZoneUnload trampoline CRASHED! Hook disabled.");
        }
    }
}

static void __fastcall Hook_BuildingPlace(void* world, void* building,
                                           float x, float y, float z) {
    int callNum = s_buildPlaceCount.fetch_add(1) + 1;
    spdlog::info("world_hooks: BuildingPlace #{} world=0x{:X} building=0x{:X} pos=({:.1f},{:.1f},{:.1f})",
                 callNum, reinterpret_cast<uintptr_t>(world),
                 reinterpret_cast<uintptr_t>(building), x, y, z);

    auto& core = Core::Get();

    if (core.IsConnected()) {
        uint32_t templateId = 0;
        uint32_t compQuat = 0;

        if (building) {
            Memory::Read(reinterpret_cast<uintptr_t>(building) + 0x08, templateId);
            Quat rot;
            if (Memory::Read(reinterpret_cast<uintptr_t>(building) + 0x20, rot)) {
                compQuat = rot.Compress();
            }
        }

        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_BuildRequest);
        writer.WriteU32(templateId);
        writer.WriteF32(x);
        writer.WriteF32(y);
        writer.WriteF32(z);
        writer.WriteU32(compQuat);

        core.GetClient().SendReliable(writer.Data(), writer.Size());
    }

    // SEH-protected trampoline call
    if (!SafeCall_Void_PtrPtrFFF(reinterpret_cast<void*>(s_origBuildPlace),
                                  world, building, x, y, z, &s_buildPlaceHealth)) {
        if (s_buildPlaceHealth.trampolineFailed.load()) {
            spdlog::error("world_hooks: BuildingPlace trampoline CRASHED! Hook disabled.");
        }
    }
}

bool Install() {
    auto& funcs = Core::Get().GetGameFunctions();
    auto& hookMgr = HookManager::Get();

    if (funcs.ZoneLoad) {
        hookMgr.InstallAt("ZoneLoad",
                          reinterpret_cast<uintptr_t>(funcs.ZoneLoad),
                          &Hook_ZoneLoad, &s_origZoneLoad);
    }
    if (funcs.ZoneUnload) {
        hookMgr.InstallAt("ZoneUnload",
                          reinterpret_cast<uintptr_t>(funcs.ZoneUnload),
                          &Hook_ZoneUnload, &s_origZoneUnload);
    }
    // BuildingPlace hook DISABLED — function signature not verified.
    // During save load, createBuilding fires for ALL buildings (~300+) with
    // unknown parameter layout, causing garbage coordinates and crashes.
    // Building sync is not needed for MVP.
    spdlog::info("world_hooks: BuildingPlace hook SKIPPED (signature unverified)");

    spdlog::info("world_hooks: Installed");
    return true;
}

void Uninstall() {
    HookManager::Get().Remove("ZoneLoad");
    HookManager::Get().Remove("ZoneUnload");
    HookManager::Get().Remove("BuildingPlace");
}

} // namespace kmp::world_hooks
