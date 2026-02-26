#include "entity_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "kmp/hook_manager.h"
#include "kmp/protocol.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>

namespace kmp::entity_hooks {

// ── Function Types ──
// These signatures are approximations - exact signatures determined via RE.
// __fastcall is the x64 calling convention (RCX, RDX, R8, R9, stack)

using CharacterCreateFn = void*(__fastcall*)(void* factory, void* templateData);
using CharacterDestroyFn = void(__fastcall*)(void* character);

static CharacterCreateFn  s_origCreate  = nullptr;
static CharacterDestroyFn s_origDestroy = nullptr;

// ── Hooks ──

static void* __fastcall Hook_CharacterCreate(void* factory, void* templateData) {
    void* character = s_origCreate(factory, templateData);

    if (character) {
        auto& core = Core::Get();
        if (core.IsConnected()) {
            // Register new entity with the network
            EntityID netId = core.GetEntityRegistry().Register(
                character, EntityType::NPC);

            spdlog::debug("entity_hooks: Character created, netId={}", netId);

            // If we're the host/server, broadcast spawn to other players
            if (core.IsHost()) {
                // Read character data using CharacterAccessor
                game::CharacterAccessor accessor(character);

                Vec3 pos = accessor.GetPosition();
                Quat rot = accessor.GetRotation();
                uint32_t compQuat = rot.Compress();

                // Extract faction ID from the faction pointer
                uintptr_t factionPtr = accessor.GetFactionPtr();
                uint32_t factionId = 0;
                if (factionPtr != 0) {
                    // Faction ID is typically at offset +0x08 in the faction object
                    Memory::Read(factionPtr + 0x08, factionId);
                }

                // Try to extract template ID from the templateData parameter
                uint32_t templateId = 0;
                if (templateData) {
                    // Template ID is typically at offset +0x00 or +0x08
                    Memory::Read(reinterpret_cast<uintptr_t>(templateData) + 0x08, templateId);
                }

                // Build spawn message
                PacketWriter writer;
                writer.WriteHeader(MessageType::S2C_EntitySpawn);
                writer.WriteU32(netId);
                writer.WriteU8(static_cast<uint8_t>(EntityType::NPC));
                writer.WriteU32(0); // server-owned
                writer.WriteU32(templateId);
                writer.WriteF32(pos.x);
                writer.WriteF32(pos.y);
                writer.WriteF32(pos.z);
                writer.WriteU32(compQuat);
                writer.WriteU32(factionId);

                core.GetClient().SendReliable(writer.Data(), writer.Size());
            }
        }
    }

    return character;
}

static void __fastcall Hook_CharacterDestroy(void* character) {
    auto& core = Core::Get();
    if (core.IsConnected()) {
        EntityID netId = core.GetEntityRegistry().GetNetId(character);
        if (netId != INVALID_ENTITY) {
            spdlog::debug("entity_hooks: Character destroyed, netId={}", netId);

            // Notify network
            if (core.IsHost()) {
                PacketWriter writer;
                writer.WriteHeader(MessageType::S2C_EntityDespawn);
                writer.WriteU32(netId);
                writer.WriteU8(0); // reason: normal

                core.GetClient().SendReliable(writer.Data(), writer.Size());
            }

            core.GetEntityRegistry().Unregister(netId);
        }
    }

    s_origDestroy(character);
}

// ── Install/Uninstall ──

bool Install() {
    auto& core = Core::Get();
    auto& hookMgr = HookManager::Get();
    auto& funcs = core.GetGameFunctions();

    bool success = true;

    if (funcs.CharacterSpawn) {
        if (!hookMgr.InstallAt("CharacterCreate",
                               reinterpret_cast<uintptr_t>(funcs.CharacterSpawn),
                               &Hook_CharacterCreate, &s_origCreate)) {
            spdlog::error("entity_hooks: Failed to hook CharacterCreate");
            success = false;
        }
    }

    if (funcs.CharacterDestroy) {
        if (!hookMgr.InstallAt("CharacterDestroy",
                               reinterpret_cast<uintptr_t>(funcs.CharacterDestroy),
                               &Hook_CharacterDestroy, &s_origDestroy)) {
            spdlog::error("entity_hooks: Failed to hook CharacterDestroy");
            success = false;
        }
    }

    spdlog::info("entity_hooks: Installed (create={}, destroy={})",
                 funcs.CharacterSpawn != nullptr, funcs.CharacterDestroy != nullptr);
    return success;
}

void Uninstall() {
    HookManager::Get().Remove("CharacterCreate");
    HookManager::Get().Remove("CharacterDestroy");
}

} // namespace kmp::entity_hooks
