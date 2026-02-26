#include "combat_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "kmp/hook_manager.h"
#include "kmp/protocol.h"
#include <spdlog/spdlog.h>

namespace kmp::combat_hooks {

// ── Function Types ──
// Damage function signature (approximate):
// void ApplyDamage(void* target, void* attacker, int bodyPart, float cut, float blunt, float pierce)
using ApplyDamageFn = void(__fastcall*)(void* target, void* attacker,
                                        int bodyPart, float cut, float blunt, float pierce);
using CharacterDeathFn = void(__fastcall*)(void* character, void* killer);

static ApplyDamageFn    s_origApplyDamage  = nullptr;
static CharacterDeathFn s_origCharDeath    = nullptr;

// ── Hooks ──

static void __fastcall Hook_ApplyDamage(void* target, void* attacker,
                                         int bodyPart, float cut, float blunt, float pierce) {
    auto& core = Core::Get();

    if (core.IsConnected() && core.IsHost()) {
        // Server-authoritative: apply damage and broadcast result
        s_origApplyDamage(target, attacker, bodyPart, cut, blunt, pierce);

        EntityID targetId = core.GetEntityRegistry().GetNetId(target);
        EntityID attackerId = attacker ? core.GetEntityRegistry().GetNetId(attacker) : INVALID_ENTITY;

        if (targetId != INVALID_ENTITY) {
            // Read result health from the character after damage was applied
            game::CharacterAccessor accessor(target);
            BodyPart hitPart = static_cast<BodyPart>(bodyPart);
            float resultHealth = accessor.GetHealth(hitPart);
            bool isAlive = accessor.IsAlive();

            // Check KO threshold: in Kenshi, KO occurs at -50 to -100 on any part
            bool wasKO = !isAlive && resultHealth > -100.f;

            PacketWriter writer;
            writer.WriteHeader(MessageType::S2C_CombatHit);
            writer.WriteU32(attackerId);
            writer.WriteU32(targetId);
            writer.WriteU8(static_cast<uint8_t>(bodyPart));
            writer.WriteF32(cut);
            writer.WriteF32(blunt);
            writer.WriteF32(pierce);
            writer.WriteF32(resultHealth);
            writer.WriteU8(0);    // wasBlocked
            writer.WriteU8(wasKO ? 1 : 0);

            core.GetClient().SendReliable(writer.Data(), writer.Size());
        }
    } else if (core.IsConnected()) {
        // Client: only apply if server told us to
        // Send attack intent instead
        EntityID targetId = core.GetEntityRegistry().GetNetId(target);
        EntityID attackerId = attacker ? core.GetEntityRegistry().GetNetId(attacker) : INVALID_ENTITY;

        // Check if attacker is our character
        auto* info = attackerId != INVALID_ENTITY
            ? core.GetEntityRegistry().GetInfo(attackerId)
            : nullptr;

        if (info && info->ownerPlayerId == core.GetLocalPlayerId()) {
            PacketWriter writer;
            writer.WriteHeader(MessageType::C2S_AttackIntent);
            writer.WriteU32(attackerId);
            writer.WriteU32(targetId);
            writer.WriteU8(0); // melee

            core.GetClient().SendReliable(writer.Data(), writer.Size());
        }

        // Still apply locally for responsiveness
        s_origApplyDamage(target, attacker, bodyPart, cut, blunt, pierce);
    } else {
        // Not connected - normal single-player behavior
        s_origApplyDamage(target, attacker, bodyPart, cut, blunt, pierce);
    }
}

static void __fastcall Hook_CharacterDeath(void* character, void* killer) {
    auto& core = Core::Get();

    if (core.IsConnected()) {
        EntityID entityId = core.GetEntityRegistry().GetNetId(character);
        EntityID killerId = killer ? core.GetEntityRegistry().GetNetId(killer) : INVALID_ENTITY;

        if (entityId != INVALID_ENTITY && core.IsHost()) {
            PacketWriter writer;
            writer.WriteHeader(MessageType::S2C_CombatDeath);
            writer.WriteU32(entityId);
            writer.WriteU32(killerId);

            core.GetClient().SendReliable(writer.Data(), writer.Size());
        }
    }

    s_origCharDeath(character, killer);
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
