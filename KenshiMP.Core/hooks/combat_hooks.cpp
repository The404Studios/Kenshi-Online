#include "combat_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "kmp/hook_manager.h"
#include "kmp/protocol.h"
#include "kmp/safe_hook.h"
#include <spdlog/spdlog.h>

namespace kmp::combat_hooks {

// ── Function Types ──
using ApplyDamageFn = void(__fastcall*)(void* target, void* attacker,
                                        int bodyPart, float cut, float blunt, float pierce);
using CharacterDeathFn = void(__fastcall*)(void* character, void* killer);

static ApplyDamageFn    s_origApplyDamage  = nullptr;
static CharacterDeathFn s_origCharDeath    = nullptr;

// ── Hook Health ──
static HookHealth s_damageHealth{"ApplyDamage"};
static HookHealth s_deathHealth{"CharacterDeath"};

// ── Diagnostic Counters ──
static std::atomic<int> s_damageCount{0};
static std::atomic<int> s_deathCount{0};

// ── Hooks ──

static void __fastcall Hook_ApplyDamage(void* target, void* attacker,
                                         int bodyPart, float cut, float blunt, float pierce) {
    int callNum = s_damageCount.fetch_add(1) + 1;
    spdlog::debug("combat_hooks: ApplyDamage #{} target=0x{:X} attacker=0x{:X} part={} cut={:.1f} blunt={:.1f} pierce={:.1f}",
                  callNum, reinterpret_cast<uintptr_t>(target),
                  reinterpret_cast<uintptr_t>(attacker),
                  bodyPart, cut, blunt, pierce);

    auto& core = Core::Get();

    if (core.IsConnected()) {
        // Apply damage locally for responsiveness (SEH-protected)
        if (!SafeCall_Void_PtrPtrIFFF(reinterpret_cast<void*>(s_origApplyDamage),
                                       target, attacker, bodyPart, cut, blunt, pierce,
                                       &s_damageHealth)) {
            if (s_damageHealth.trampolineFailed.load()) {
                spdlog::error("combat_hooks: ApplyDamage trampoline CRASHED! Hook disabled.");
            }
            return;
        }

        // Send attack intent to server for our own characters
        EntityID targetId = core.GetEntityRegistry().GetNetId(target);
        EntityID attackerId = attacker ? core.GetEntityRegistry().GetNetId(attacker) : INVALID_ENTITY;

        auto* info = attackerId != INVALID_ENTITY
            ? core.GetEntityRegistry().GetInfo(attackerId)
            : nullptr;

        if (info && info->ownerPlayerId == core.GetLocalPlayerId() &&
            targetId != INVALID_ENTITY) {
            PacketWriter writer;
            writer.WriteHeader(MessageType::C2S_AttackIntent);
            writer.WriteU32(attackerId);
            writer.WriteU32(targetId);
            writer.WriteU8(0); // melee

            core.GetClient().SendReliable(writer.Data(), writer.Size());
        }
    } else {
        // Not connected - normal single-player behavior (SEH-protected)
        if (!SafeCall_Void_PtrPtrIFFF(reinterpret_cast<void*>(s_origApplyDamage),
                                       target, attacker, bodyPart, cut, blunt, pierce,
                                       &s_damageHealth)) {
            if (s_damageHealth.trampolineFailed.load()) {
                spdlog::error("combat_hooks: ApplyDamage trampoline CRASHED! Hook disabled.");
            }
        }
    }
}

static void __fastcall Hook_CharacterDeath(void* character, void* killer) {
    int callNum = s_deathCount.fetch_add(1) + 1;
    spdlog::info("combat_hooks: CharacterDeath #{} char=0x{:X} killer=0x{:X}",
                 callNum, reinterpret_cast<uintptr_t>(character),
                 reinterpret_cast<uintptr_t>(killer));

    auto& core = Core::Get();

    if (core.IsConnected()) {
        EntityID entityId = core.GetEntityRegistry().GetNetId(character);
        EntityID killerId = killer ? core.GetEntityRegistry().GetNetId(killer) : INVALID_ENTITY;

        if (entityId != INVALID_ENTITY) {
            spdlog::debug("combat_hooks: Character death netId={}, killer={}", entityId, killerId);
        }
    }

    // SEH-protected trampoline call
    if (!SafeCall_Void_PtrPtr(reinterpret_cast<void*>(s_origCharDeath),
                               character, killer, &s_deathHealth)) {
        if (s_deathHealth.trampolineFailed.load()) {
            spdlog::error("combat_hooks: CharacterDeath trampoline CRASHED! Hook disabled.");
        }
    }
}

// ── Install/Uninstall ──

bool Install() {
    auto& core = Core::Get();
    auto& hookMgr = HookManager::Get();
    auto& funcs = core.GetGameFunctions();

    if (funcs.ApplyDamage) {
        hookMgr.InstallAt("ApplyDamage",
                          reinterpret_cast<uintptr_t>(funcs.ApplyDamage),
                          &Hook_ApplyDamage, &s_origApplyDamage);
    }

    if (funcs.CharacterDeath) {
        hookMgr.InstallAt("CharacterDeath",
                          reinterpret_cast<uintptr_t>(funcs.CharacterDeath),
                          &Hook_CharacterDeath, &s_origCharDeath);
    }

    spdlog::info("combat_hooks: Installed (damage={}, death={})",
                 funcs.ApplyDamage != nullptr, funcs.CharacterDeath != nullptr);
    return true;
}

void Uninstall() {
    HookManager::Get().Remove("ApplyDamage");
    HookManager::Get().Remove("CharacterDeath");
}

} // namespace kmp::combat_hooks
