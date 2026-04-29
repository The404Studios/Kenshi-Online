#include "combat_hooks.h"
#include "ai_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "kmp/hook_manager.h"
#include "kmp/protocol.h"
#include "kmp/safe_hook.h"
#include <spdlog/spdlog.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace kmp::combat_hooks {

// ── Function Types ──
using CharacterDeathFn = void(__fastcall*)(void* character, void* killer);
using CharacterKOFn = void(__fastcall*)(void* character, void* attacker, int reason);

static CharacterDeathFn s_origCharDeath    = nullptr;
static CharacterKOFn    s_origCharKO       = nullptr;

// ── Hook Health ──
static HookHealth s_deathHealth{"CharacterDeath"};
static HookHealth s_koHealth{"CharacterKO"};

// ── Diagnostic Counters ──
static std::atomic<int> s_deathCount{0};
static std::atomic<int> s_koCount{0};

// ── Echo suppression flags ──
// Set by packet_handler before calling native CharacterDeath/CharacterKO from
// server-sourced events (S2C_CombatDeath, S2C_CombatKO). When set, the hook
// skips pushing to the deferred queue, preventing C2S→S2C→C2S echo loops.
static std::atomic<bool> s_serverSourcedDeath{false};
static std::atomic<bool> s_serverSourcedKO{false};

// ═══════════════════════════════════════════════════════════════════════════
//  DEFERRED EVENT QUEUE
//
//  Combat hooks fire inside MovRaxRsp naked detours where:
//  - spdlog calls allocate from heap → corrupt the 4KB stack gap
//  - SendReliable acquires ENet mutex → deadlock if game thread holds it
//  - PacketWriter has a destructor → stack unwinding crashes in detour context
//  - CharacterAccessor reads game memory → AV in wrong stack context
//
//  SOLUTION: The hook body does ONLY the minimum (call original + capture IDs
//  into a lock-free queue), then ProcessDeferredEvents() runs from the safe
//  OnGameTick context to do logging, packet building, and network sends.
// ═══════════════════════════════════════════════════════════════════════════

enum class CombatEventType : uint8_t { Death, KO };

struct DeferredCombatEvent {
    CombatEventType type;
    uint32_t entityId;
    uint32_t otherId;   // killer for death, attacker for KO
    uint8_t  reason;    // KO reason
};

static constexpr int MAX_DEFERRED_EVENTS = 256;
static DeferredCombatEvent s_eventRing[MAX_DEFERRED_EVENTS];
static std::atomic<int> s_eventWriteIdx{0};
static std::atomic<int> s_eventReadIdx{0};
static std::atomic<int> s_dropCount{0}; // Tracks events dropped due to full buffer

// Lock-free single-producer (hook) single-consumer (game tick) ring buffer.
// Safe because: hooks run on game thread (single producer), ProcessDeferredEvents
// runs on game thread (single consumer), and both are the SAME thread.
static bool PushEvent(const DeferredCombatEvent& evt) {
    int writeIdx = s_eventWriteIdx.load(std::memory_order_relaxed);
    int nextIdx = (writeIdx + 1) % MAX_DEFERRED_EVENTS;
    if (nextIdx == s_eventReadIdx.load(std::memory_order_acquire)) {
        s_dropCount.fetch_add(1, std::memory_order_relaxed);
        return false; // Full — drop event (better than crash)
    }
    s_eventRing[writeIdx] = evt;
    s_eventWriteIdx.store(nextIdx, std::memory_order_release);
    return true;
}

static bool PopEvent(DeferredCombatEvent& out) {
    int readIdx = s_eventReadIdx.load(std::memory_order_relaxed);
    if (readIdx == s_eventWriteIdx.load(std::memory_order_acquire)) {
        return false; // Empty
    }
    out = s_eventRing[readIdx];
    s_eventReadIdx.store((readIdx + 1) % MAX_DEFERRED_EVENTS, std::memory_order_release);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  HOOK BODIES — MINIMAL WORK ONLY
//  No spdlog. No PacketWriter. No SendReliable. No CharacterAccessor.
//  Just: call original + push event IDs to ring buffer.
// ═══════════════════════════════════════════════════════════════════════════

static void __fastcall Hook_CharacterDeath(void* character, void* killer) {
    s_deathCount.fetch_add(1, std::memory_order_relaxed);

    // Call original FIRST (SEH-protected) — game death logic must always run
    SafeCall_Void_PtrPtr(reinterpret_cast<void*>(s_origCharDeath),
                          character, killer, &s_deathHealth);

    // Skip if this death was triggered by the packet handler applying a server
    // event (S2C_CombatDeath). Without this, receiving a remote death would
    // push a new C2S_CombatDeath → server re-broadcasts → infinite echo.
    if (s_serverSourcedDeath.load(std::memory_order_acquire)) return;

    // Capture entity IDs (cheap pointer lookups, no allocation)
    auto& core = Core::Get();
    if (!core.IsConnected()) return;

    EntityID entityId = core.GetEntityRegistry().GetNetId(character);
    if (entityId == INVALID_ENTITY) return;

    EntityID killerId = killer ? core.GetEntityRegistry().GetNetId(killer) : INVALID_ENTITY;

    // Defer all heavy work (logging, packet send) to ProcessDeferredEvents
    DeferredCombatEvent evt{};
    evt.type = CombatEventType::Death;
    evt.entityId = entityId;
    evt.otherId = killerId;
    PushEvent(evt);
}

static void __fastcall Hook_CharacterKO(void* character, void* attacker, int reason) {
    s_koCount.fetch_add(1, std::memory_order_relaxed);

    // Call original FIRST (SEH-protected) — game KO logic must always run
    SafeCall_Void_PtrPtrI(reinterpret_cast<void*>(s_origCharKO),
                           character, attacker, reason, &s_koHealth);

    // Skip if this KO was triggered by the packet handler applying a server
    // event (S2C_CombatKO). Prevents infinite echo loops.
    if (s_serverSourcedKO.load(std::memory_order_acquire)) return;

    // Capture entity IDs (cheap pointer lookups, no allocation)
    auto& core = Core::Get();
    if (!core.IsConnected()) return;

    EntityID entityId = core.GetEntityRegistry().GetNetId(character);
    if (entityId == INVALID_ENTITY) return;

    EntityID attackerId = attacker ? core.GetEntityRegistry().GetNetId(attacker) : INVALID_ENTITY;

    // Defer all heavy work to ProcessDeferredEvents
    DeferredCombatEvent evt{};
    evt.type = CombatEventType::KO;
    evt.entityId = entityId;
    evt.otherId = attackerId;
    evt.reason = static_cast<uint8_t>(reason);
    PushEvent(evt);
}

// ── SEH wrapper for reading health (no C++ objects allowed in __try) ──
static float SEH_ReadChestHealth(void* gameObj) {
    __try {
        game::CharacterAccessor accessor(gameObj);
        return accessor.GetHealth(BodyPart::Chest);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -100.f; // Assume dead on read failure
    }
}

// ── SEH wrapper for reading all 7 limb health values ──
static bool SEH_ReadAllLimbHealth(void* gameObj, float outHealth[7]) {
    __try {
        game::CharacterAccessor accessor(gameObj);
        for (int i = 0; i < static_cast<int>(BodyPart::Count); i++) {
            outHealth[i] = accessor.GetHealth(static_cast<BodyPart>(i));
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        for (int i = 0; i < 7; i++) outHealth[i] = -100.f;
        return false;
    }
}

// ── Helper: send C2S_LimbHealth for an entity after a combat event ──
static void SendLimbHealthUpdate(EntityID entityId, void* gameObj) {
    if (!gameObj) return;
    float limbHealth[7];
    if (!SEH_ReadAllLimbHealth(gameObj, limbHealth)) return;

    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_LimbHealth);
    writer.WriteU32(entityId);
    for (int i = 0; i < 7; i++) {
        writer.WriteF32(limbHealth[i]);
    }
    Core::Get().GetClient().SendReliable(writer.Data(), writer.Size());
}

// ═══════════════════════════════════════════════════════════════════════════
//  DEFERRED PROCESSING — called from Core::OnGameTick (safe context)
//  Here we can safely: log, build packets, send via ENet, read game memory.
// ═══════════════════════════════════════════════════════════════════════════

void ProcessDeferredEvents() {
    auto& core = Core::Get();
    if (!core.IsConnected()) {
        // Drain queue without processing if disconnected
        DeferredCombatEvent discard;
        while (PopEvent(discard)) {}
        return;
    }

    // Report and reset drop counter (can't log from hook context, so we log here)
    int dropped = s_dropCount.exchange(0, std::memory_order_relaxed);
    if (dropped > 0) {
        spdlog::warn("combat_hooks: {} combat events DROPPED (ring buffer was full)", dropped);
    }

    DeferredCombatEvent evt;
    int processed = 0;
    while (PopEvent(evt) && processed < 64) { // Cap per tick to avoid stalls
        processed++;

        // Send events for ANY registered entity (not just owned).
        // Cross-player combat: when player A kills player B's character,
        // the death fires on A's machine and must be reported to the server.
        // Echo suppression is handled by s_serverSourcedDeath/KO flags in
        // the hook body — events that arrive here are genuine local combat.
        auto infoCopy = core.GetEntityRegistry().GetInfo(evt.entityId);
        if (!infoCopy) continue;

        if (evt.type == CombatEventType::Death) {
            spdlog::info("combat_hooks: [deferred] Death — entity {} killer {}",
                         evt.entityId, evt.otherId);

            PacketWriter writer;
            writer.WriteHeader(MessageType::C2S_CombatDeath);
            writer.WriteU32(evt.entityId);
            writer.WriteU32(evt.otherId);
            core.GetClient().SendReliable(writer.Data(), writer.Size());

            // Piggyback limb health snapshot after death event
            void* deathObj = core.GetEntityRegistry().GetGameObject(evt.entityId);
            SendLimbHealthUpdate(evt.entityId, deathObj);

        } else if (evt.type == CombatEventType::KO) {
            spdlog::info("combat_hooks: [deferred] KO — entity {} attacker {} reason {}",
                         evt.entityId, evt.otherId, evt.reason);

            PacketWriter writer;
            writer.WriteHeader(MessageType::C2S_CombatKO);
            writer.WriteU32(evt.entityId);
            writer.WriteU32(evt.otherId);
            writer.WriteU8(evt.reason);

            // Read health safely from game tick context (not hook context)
            void* gameObj = core.GetEntityRegistry().GetGameObject(evt.entityId);
            float chestHealth = gameObj ? SEH_ReadChestHealth(gameObj) : -100.f;
            writer.WriteF32(chestHealth);
            core.GetClient().SendReliable(writer.Data(), writer.Size());

            // Piggyback limb health snapshot after KO event
            SendLimbHealthUpdate(evt.entityId, gameObj);
        }
    }
}

// ── Install/Uninstall ──

bool Install() {
    auto& core = Core::Get();
    auto& hookMgr = HookManager::Get();
    auto& funcs = core.GetGameFunctions();

    // ═══ DO NOT HOOK ApplyDamage ═══
    // ApplyDamage (0x7A33A0) uses `mov rax, rsp` prologue and fires hundreds
    // of times per combat tick. The MovRaxRsp wrapper's global RSP slots corrupt
    // under rapid-fire calls → deterministic crash on "attack unprovoked".
    // Combat damage sync uses death/KO hooks + health polling instead.
    if (funcs.ApplyDamage) {
        spdlog::info("combat_hooks: ApplyDamage at 0x{:X} — NOT hooked (mov rax,rsp crash risk)",
                     reinterpret_cast<uintptr_t>(funcs.ApplyDamage));
    }

    if (funcs.CharacterDeath) {
        hookMgr.InstallAt("CharacterDeath",
                          reinterpret_cast<uintptr_t>(funcs.CharacterDeath),
                          &Hook_CharacterDeath, &s_origCharDeath);
    }

    if (funcs.CharacterKO) {
        hookMgr.InstallAt("CharacterKO",
                          reinterpret_cast<uintptr_t>(funcs.CharacterKO),
                          &Hook_CharacterKO, &s_origCharKO);
    }

    spdlog::info("combat_hooks: Installed (damage=SKIPPED, death={}, ko={})",
                 funcs.CharacterDeath != nullptr, funcs.CharacterKO != nullptr);
    return true;
}

void Uninstall() {
    HookManager::Get().Remove("CharacterDeath");
    HookManager::Get().Remove("CharacterKO");
    spdlog::info("combat_hooks: Uninstalled");
}

void SetServerSourcedDeath(bool active) {
    s_serverSourcedDeath.store(active, std::memory_order_release);
}

void SetServerSourcedKO(bool active) {
    s_serverSourcedKO.store(active, std::memory_order_release);
}

} // namespace kmp::combat_hooks
