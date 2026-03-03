#include "player_controller.h"
#include "game_types.h"
#include "spawn_manager.h"
#include "../sync/entity_registry.h"
#include "../core.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>

namespace kmp {


void PlayerController::InitializeLocalPlayer(PlayerID localId, const std::string& playerName) {
    std::lock_guard lock(m_mutex);
    m_localPlayerId = localId;
    m_localPlayerName = playerName;
    m_initialized = true;

    spdlog::info("PlayerController: Initialized local player '{}' (ID: {})", playerName, localId);

    // Capture faction from the first local character we can find
    auto& registry = Core::Get().GetEntityRegistry();
    auto localEntities = registry.GetPlayerEntities(localId);
    for (EntityID eid : localEntities) {
        void* gameObj = registry.GetGameObject(eid);
        if (!gameObj) continue;

        game::CharacterAccessor accessor(gameObj);
        if (!accessor.IsValid()) continue;

        uintptr_t faction = accessor.GetFactionPtr();
        if (faction != 0 && m_localFactionPtr == 0) {
            m_localFactionPtr = faction;
            spdlog::info("PlayerController: Captured local faction ptr 0x{:X} from entity {}",
                         faction, eid);
        }
    }
}

std::vector<EntityID> PlayerController::GetLocalSquadEntities() const {
    return Core::Get().GetEntityRegistry().GetPlayerEntities(m_localPlayerId);
}

void* PlayerController::GetPrimaryCharacter() const {
    auto& registry = Core::Get().GetEntityRegistry();
    auto entities = registry.GetPlayerEntities(m_localPlayerId);
    if (entities.empty()) return nullptr;

    // Return the first entity with a valid game object
    for (EntityID eid : entities) {
        void* obj = registry.GetGameObject(eid);
        if (obj) return obj;
    }
    return nullptr;
}

void PlayerController::RegisterRemotePlayer(PlayerID id, const std::string& name) {
    // Guard against registering self as remote (defense-in-depth)
    if (id == m_localPlayerId) {
        spdlog::warn("PlayerController: Ignoring attempt to register self (ID: {}) as remote", id);
        return;
    }

    std::lock_guard lock(m_mutex);
    auto& state = m_remotePlayers[id];
    state.playerId = id;
    state.playerName = name;
    spdlog::info("PlayerController: Registered remote player '{}' (ID: {})", name, id);
}

void PlayerController::RemoveRemotePlayer(PlayerID id) {
    std::lock_guard lock(m_mutex);
    auto it = m_remotePlayers.find(id);
    if (it != m_remotePlayers.end()) {
        spdlog::info("PlayerController: Removed remote player '{}' (ID: {}, {} entities)",
                     it->second.playerName, id, it->second.entities.size());
        m_remotePlayers.erase(it);
    }
}

bool PlayerController::OnRemoteCharacterSpawned(EntityID entityId, void* gameObject, PlayerID owner) {
    if (!gameObject) return false;

    game::CharacterAccessor accessor(gameObject);
    if (!accessor.IsValid()) return false;

    // Find the remote player's name
    std::string displayName;
    {
        std::lock_guard lock(m_mutex);
        auto it = m_remotePlayers.find(owner);
        if (it != m_remotePlayers.end()) {
            displayName = it->second.playerName;
            it->second.hasSpawnedCharacter = true;
            it->second.entities.push_back(entityId);

            // Capture their faction from the first spawned character
            if (it->second.factionPtr == 0) {
                it->second.factionPtr = accessor.GetFactionPtr();
            }
        }
    }

    if (displayName.empty()) {
        displayName = "Player_" + std::to_string(owner);
    }

    // ── 1. Rename the character to the remote player's name ──
    {
        std::string safeName = displayName.substr(0, 15);
        if (accessor.WriteName(safeName)) {
            spdlog::info("PlayerController: Named entity {} -> '{}'", entityId, safeName);
        } else {
            spdlog::warn("PlayerController: WriteName failed for entity {}", entityId);
        }
    }

    // ── 2. Fix faction to match local player (so remote chars are allies) ──
    if (m_localFactionPtr != 0) {
        if (accessor.WriteFaction(m_localFactionPtr)) {
            spdlog::info("PlayerController: Set entity {} faction to local 0x{:X}",
                         entityId, m_localFactionPtr);
        } else {
            spdlog::warn("PlayerController: WriteFaction failed for entity {}", entityId);
        }
    } else {
        spdlog::warn("PlayerController: No local faction captured — remote entity {} keeps default faction", entityId);
    }

    spdlog::info("PlayerController: Remote character {} set up for player '{}' (named + faction set)",
                 entityId, displayName);
    return true;
}

const RemotePlayerState* PlayerController::GetRemotePlayer(PlayerID id) const {
    std::lock_guard lock(m_mutex);
    auto it = m_remotePlayers.find(id);
    return it != m_remotePlayers.end() ? &it->second : nullptr;
}

std::vector<RemotePlayerState> PlayerController::GetAllRemotePlayers() const {
    std::lock_guard lock(m_mutex);
    std::vector<RemotePlayerState> result;
    result.reserve(m_remotePlayers.size());
    for (auto& [_, state] : m_remotePlayers) {
        result.push_back(state);
    }
    return result;
}

int PlayerController::GatherLocalEntityUpdates(float deltaTime) {
    // This is a hook point for future optimization.
    // Currently, OnGameTick in core.cpp handles the actual sync loop.
    // This method can be used to pre-filter or batch updates.
    return 0;
}

void PlayerController::ApplyRemotePositionUpdate(EntityID entityId, const Vec3& pos,
                                                   const Quat& rot, uint8_t moveSpeed, uint8_t animState) {
    // Delegate to interpolation system
    float now = static_cast<float>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.f;
    Core::Get().GetInterpolation().AddSnapshot(entityId, now, pos, rot, moveSpeed, animState);
}

void PlayerController::OnGameWorldLoaded() {
    spdlog::info("PlayerController: Game world loaded");

    // Capture local faction from the first player-controlled character
    game::CharacterIterator iter;
    while (iter.HasNext()) {
        game::CharacterAccessor character = iter.Next();
        if (!character.IsValid()) continue;

        uintptr_t faction = character.GetFactionPtr();
        if (faction != 0 && m_localFactionPtr == 0) {
            // Read faction ID to verify it's a player faction
            uint32_t factionId = 0;
            Memory::Read(faction + 0x08, factionId);
            // Faction ID 0 or very low IDs are typically player factions in Kenshi
            m_localFactionPtr = faction;
            spdlog::info("PlayerController: Captured local faction 0x{:X} (id={}) from game world",
                         faction, factionId);
            break;
        }
    }
}

void PlayerController::OnWorldSnapshotReceived(int entityCount) {
    spdlog::info("PlayerController: World snapshot received with {} entities", entityCount);
}

void PlayerController::Reset() {
    std::lock_guard lock(m_mutex);
    m_remotePlayers.clear();
    m_localFactionPtr = 0;
    m_initialized = false;
    spdlog::info("PlayerController: State reset");
}

} // namespace kmp
