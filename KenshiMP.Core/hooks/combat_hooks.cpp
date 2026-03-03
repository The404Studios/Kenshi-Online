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
using CharacterKOFn = void(__fastcall*)(void* character, void* attacker, int reason);

static ApplyDamageFn    s_origApplyDamage  = nullptr;
static CharacterDeathFn s_origCharDeath    = nullptr;
static CharacterKOFn    s_origCharKO       = nullptr;

// ── Hook Health ──
static HookHealth s_damageHealth{"ApplyDamage"};
static HookHealth s_deathHealth{"CharacterDeath"};
static HookHealth s_koHealth{"CharacterKO"};

// ── Diagnostic Counters ──
static std::atomic<int> s_damageCount{0};
static std::atomic<int> s_deathCount{0};
static std::atomic<int> s_koCount{0};

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

// ── CharacterKO Hook ──
// Fires when a character is knocked unconscious (health drops below KO threshold).
// KO reason: 0=blood loss, 1=head trauma, 2=other

static void __fastcall Hook_CharacterKO(void* character, void* attacker, int reason) {
    int callNum = s_koCount.fetch_add(1) + 1;
    spdlog::info("combat_hooks: CharacterKO #{} char=0x{:X} attacker=0x{:X} reason={}",
                 callNum, reinterpret_cast<uintptr_t>(character),
                 reinterpret_cast<uintptr_t>(attacker), reason);

    auto& core = Core::Get();

    if (core.IsConnected()) {
        EntityID entityId = core.GetEntityRegistry().GetNetId(character);
        EntityID attackerId = attacker ? core.GetEntityRegistry().GetNetId(attacker) : INVALID_ENTITY;

        // Send KO event to server for entities we own
        auto* info = entityId != INVALID_ENTITY
            ? core.GetEntityRegistry().GetInfo(entityId)
            : nullptr;

        if (info && info->ownerPlayerId == core.GetLocalPlayerId()) {
            PacketWriter writer;
            writer.WriteHeader(MessageType::C2S_CombatStance); // Reuse combat channel
            writer.WriteU32(entityId);
            writer.WriteU32(attackerId);
            writer.WriteU8(static_cast<uint8_t>(reason));

            // Include current health of the chest (primary KO indicator)
            game::CharacterAccessor accessor(character);
            float chestHealth = accessor.GetHealth(BodyPart::Chest);
            writer.WriteF32(chestHealth);

            core.GetClient().SendReliable(writer.Data(), writer.Size());
            spdlog::debug("combat_hooks: Sent KO event for entity {} (attacker={})",
                          entityId, attackerId);
        }
    }

    // Call original KO handler (SEH-protected)
    if (!SafeCall_Void_PtrPtrI(reinterpret_cast<void*>(s_origCharKO),
                                character, attacker, reason, &s_koHealth)) {
        if (s_koHealth.trampolineFailed.load()) {
            spdlog::error("combat_hooks: CharacterKO trampoline CRASHED! Hook disabled.");
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

    if (funcs.CharacterKO) {
        hookMgr.InstallAt("CharacterKO",
                          reinterpret_cast<uintptr_t>(funcs.CharacterKO),
                          &Hook_CharacterKO, &s_origCharKO);
    }

    spdlog::info("combat_hooks: Installed (damage={}, death={}, ko={})",
                 funcs.ApplyDamage != nullptr, funcs.CharacterDeath != nullptr,
                 funcs.CharacterKO != nullptr);
    return true;
}

void Uninstall() {
    HookManager::Get().Remove("ApplyDamage");
    HookManager::Get().Remove("CharacterDeath");
    HookManager::Get().Remove("CharacterKO");
    spdlog::info("combat_hooks: Uninstalled");
}

} // namespace kmp::combat_hooks
