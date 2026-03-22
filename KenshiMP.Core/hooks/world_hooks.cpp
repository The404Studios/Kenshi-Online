#include "world_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "kmp/hook_manager.h"
#include "kmp/protocol.h"
#include "kmp/memory.h"
#include "kmp/safe_hook.h"
#include <spdlog/spdlog.h>
#include <atomic>

namespace kmp::world_hooks {

using ZoneLoadFn = void(__fastcall*)(void* zoneMgr, int zoneX, int zoneY);
using ZoneUnloadFn = void(__fastcall*)(void* zoneMgr, int zoneX, int zoneY);

static ZoneLoadFn      s_origZoneLoad   = nullptr;
static ZoneUnloadFn    s_origZoneUnload = nullptr;

// ── Hook Health ──
static HookHealth s_zoneLoadHealth{"ZoneLoad"};
static HookHealth s_zoneUnloadHealth{"ZoneUnload"};

// ── Diagnostic Counters ──
static std::atomic<int> s_zoneLoadCount{0};
static std::atomic<int> s_zoneUnloadCount{0};

// ═══════════════════════════════════════════════════════════════════════════
//  DEFERRED ZONE EVENT QUEUE
//  Zone hooks may fire inside MovRaxRsp detours. spdlog + PacketWriter +
//  SendReliable inside the detour causes heap corruption / deadlock.
//  Defer all heavy work to ProcessDeferredZoneEvents() called from OnGameTick.
// ═══════════════════════════════════════════════════════════════════════════

enum class ZoneEventType : uint8_t { Load, Unload };

struct DeferredZoneEvent {
    ZoneEventType type;
    int32_t zoneX;
    int32_t zoneY;
};

static constexpr int ZONE_RING_SIZE = 32;
static DeferredZoneEvent s_zoneRing[ZONE_RING_SIZE];
static std::atomic<int> s_zoneWriteIdx{0};
static std::atomic<int> s_zoneReadIdx{0};

static bool PushZoneEvent(const DeferredZoneEvent& evt) {
    int writeIdx = s_zoneWriteIdx.load(std::memory_order_relaxed);
    int nextIdx = (writeIdx + 1) % ZONE_RING_SIZE;
    if (nextIdx == s_zoneReadIdx.load(std::memory_order_acquire)) return false;
    s_zoneRing[writeIdx] = evt;
    s_zoneWriteIdx.store(nextIdx, std::memory_order_release);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  HOOK BODIES — minimal work only. No spdlog, no PacketWriter, no SendReliable.
// ═══════════════════════════════════════════════════════════════════════════

static void __fastcall Hook_ZoneLoad(void* zoneMgr, int zoneX, int zoneY) {
    s_zoneLoadCount.fetch_add(1, std::memory_order_relaxed);

    // SEH-protected trampoline call — game zone loading must always proceed
    SafeCall_Void_PtrII(reinterpret_cast<void*>(s_origZoneLoad),
                         zoneMgr, zoneX, zoneY, &s_zoneLoadHealth);

    // Queue zone load event for deferred processing (no heap allocation)
    if (Core::Get().IsConnected()) {
        DeferredZoneEvent evt{ZoneEventType::Load, zoneX, zoneY};
        PushZoneEvent(evt);
    }
}

static void __fastcall Hook_ZoneUnload(void* zoneMgr, int zoneX, int zoneY) {
    s_zoneUnloadCount.fetch_add(1, std::memory_order_relaxed);

    // SEH-protected trampoline call — must run BEFORE removing entities
    SafeCall_Void_PtrII(reinterpret_cast<void*>(s_origZoneUnload),
                         zoneMgr, zoneX, zoneY, &s_zoneUnloadHealth);

    // Queue zone unload event for deferred processing
    if (Core::Get().IsConnected()) {
        DeferredZoneEvent evt{ZoneEventType::Unload, zoneX, zoneY};
        PushZoneEvent(evt);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  DEFERRED PROCESSING — called from Core::OnGameTick (safe context)
// ═══════════════════════════════════════════════════════════════════════════

void ProcessDeferredZoneEvents() {
    auto& core = Core::Get();
    if (!core.IsConnected()) {
        // Drain without processing
        DeferredZoneEvent discard;
        int readIdx = s_zoneReadIdx.load(std::memory_order_relaxed);
        while (readIdx != s_zoneWriteIdx.load(std::memory_order_acquire)) {
            readIdx = (readIdx + 1) % ZONE_RING_SIZE;
        }
        s_zoneReadIdx.store(readIdx, std::memory_order_release);
        return;
    }

    int processed = 0;
    while (processed < 8) {
        int readIdx = s_zoneReadIdx.load(std::memory_order_relaxed);
        if (readIdx == s_zoneWriteIdx.load(std::memory_order_acquire)) break;
        DeferredZoneEvent evt = s_zoneRing[readIdx];
        s_zoneReadIdx.store((readIdx + 1) % ZONE_RING_SIZE, std::memory_order_release);
        processed++;

        if (evt.type == ZoneEventType::Load) {
            spdlog::info("world_hooks: [deferred] ZoneLoad ({}, {})", evt.zoneX, evt.zoneY);
            PacketWriter writer;
            writer.WriteHeader(MessageType::C2S_ZoneRequest);
            writer.WriteI32(evt.zoneX);
            writer.WriteI32(evt.zoneY);
            core.GetClient().SendReliable(writer.Data(), writer.Size());
        } else {
            spdlog::info("world_hooks: [deferred] ZoneUnload ({}, {})", evt.zoneX, evt.zoneY);
            core.GetEntityRegistry().RemoveEntitiesInZone(ZoneCoord(evt.zoneX, evt.zoneY));
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
    // BuildingPlace hook DISABLED — signature unverified, zone-load crashes
    spdlog::info("world_hooks: Installed (zone hooks only, building SKIPPED)");
    return true;
}

void Uninstall() {
    HookManager::Get().Remove("ZoneLoad");
    HookManager::Get().Remove("ZoneUnload");
}

} // namespace kmp::world_hooks
