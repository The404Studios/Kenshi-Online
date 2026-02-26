#include "world_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "kmp/hook_manager.h"
#include "kmp/protocol.h"
#include <spdlog/spdlog.h>

namespace kmp::world_hooks {

using ZoneLoadFn = void(__fastcall*)(void* zoneMgr, int zoneX, int zoneY);
using ZoneUnloadFn = void(__fastcall*)(void* zoneMgr, int zoneX, int zoneY);
using BuildingPlaceFn = void(__fastcall*)(void* world, void* building, float x, float y, float z);

static ZoneLoadFn      s_origZoneLoad   = nullptr;
static ZoneUnloadFn    s_origZoneUnload = nullptr;
static BuildingPlaceFn s_origBuildPlace = nullptr;

static void __fastcall Hook_ZoneLoad(void* zoneMgr, int zoneX, int zoneY) {
    s_origZoneLoad(zoneMgr, zoneX, zoneY);

    auto& core = Core::Get();
    if (core.IsConnected()) {
        spdlog::debug("world_hooks: Zone loaded ({}, {})", zoneX, zoneY);
        // Request entity data for this zone from server
        PacketWriter writer;
        writer.WriteHeader(MessageType::S2C_ZoneData);
        writer.WriteI32(zoneX);
        writer.WriteI32(zoneY);
        core.GetClient().SendReliable(writer.Data(), writer.Size());
    }
}

static void __fastcall Hook_ZoneUnload(void* zoneMgr, int zoneX, int zoneY) {
    auto& core = Core::Get();
    if (core.IsConnected()) {
        spdlog::debug("world_hooks: Zone unloading ({}, {})", zoneX, zoneY);
        // Clean up network entities in this zone
        core.GetEntityRegistry().RemoveEntitiesInZone(ZoneCoord(zoneX, zoneY));
    }

    s_origZoneUnload(zoneMgr, zoneX, zoneY);
}

static void __fastcall Hook_BuildingPlace(void* world, void* building,
                                           float x, float y, float z) {
    auto& core = Core::Get();

    if (core.IsConnected()) {
        // Extract building template ID and rotation from the building object
        uint32_t templateId = 0;
        uint32_t compQuat = 0;

        if (building) {
            // Template ID is typically at offset +0x08 in the building object
            Memory::Read(reinterpret_cast<uintptr_t>(building) + 0x08, templateId);

            // Rotation: read Quat from building object (typically at +0x20 or +0x30)
            Quat rot;
            if (Memory::Read(reinterpret_cast<uintptr_t>(building) + 0x20, rot)) {
                compQuat = rot.Compress();
            }
        }

        // Send build request to server
        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_BuildRequest);
        writer.WriteU32(templateId);
        writer.WriteF32(x);
        writer.WriteF32(y);
        writer.WriteF32(z);
        writer.WriteU32(compQuat);

        core.GetClient().SendReliable(writer.Data(), writer.Size());
    }

    s_origBuildPlace(world, building, x, y, z);
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
    if (funcs.BuildingPlace) {
        hookMgr.InstallAt("BuildingPlace",
                          reinterpret_cast<uintptr_t>(funcs.BuildingPlace),
                          &Hook_BuildingPlace, &s_origBuildPlace);
    }

    spdlog::info("world_hooks: Installed");
    return true;
}

void Uninstall() {
    HookManager::Get().Remove("ZoneLoad");
    HookManager::Get().Remove("ZoneUnload");
    HookManager::Get().Remove("BuildingPlace");
}

} // namespace kmp::world_hooks
